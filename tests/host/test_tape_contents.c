#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/tape_contents.h"
#define main existing_oled_ui_main
#include "test_oled_ui.c"
#undef main
uint8_t tape_analyzer_percent(void){return 90;}
void tape_analyzer_preview(program_metadata_t *m){memset(m,0,sizeof(*m));strcpy(m->title,"CANDIDATE");}
static void fits(const ui_model_t *m){for(unsigned i=0;i<8;++i)assert(m->line[i] && strlen(m->line[i])<=21);}
static void capture_model(const ui_model_t *m,const char *name) {
    ui_use_text_view();tick+=100;ui_render(m);
    expect_text(0,0,m->line[0],1);expect_text(0,56,m->line[7],1);artifact(name);
}
static void capture_transport(const tape_contents_t *v,const tape_index_t *idx,const char *name) {
    ui_transport_view_t view={.program=idx->programs[0].metadata,.filename="LONG WAVEFORM TITLE.wav",
        .tx={.fifo_used=2048,.fifo_capacity=4095},.status="PLAY",.seconds=3,.total_seconds=(uint32_t)(idx->waveform_us/1000000),
        .warning=NULL};
    tape_contents_apply_transport(v,idx,&view);
    assert(view.total_seconds==idx->waveform_us/1000000);
    if(v->page==TAPE_UI_SEEK){assert(view.seconds==v->seek_us/1000000);assert(strstr(view.status,"PAUSE") && view.warning);}
    else assert(view.seconds==3);
    ui_model_t model={{"","","","","","","",""}};
    ui_set_transport_view(false,&view);tick+=100;ui_render(&model);
    if(v->page==TAPE_UI_SEEK){assert(!view.program.valid && !view.preview_metadata);expect_text(0,8,"LONG WAVEFORM TITLE.w",1);}
    else expect_text(0,8,"FIRST PROGRAM TITLE I",1);
    expect_text(80,56,"BUF  50%",1);artifact(name);
}
int main(void) {
    oled_init();ui_init();
    tape_contents_t v;tape_contents_open(&v);
    tape_program_t programs[2]={0};
    tape_index_t idx={.program_count_recognized=2,.programs=programs,.waveform_us=6000000000ULL,.duration_known=true};
    strcpy(idx.source.path,"/sd/LONG WAVEFORM TITLE.wav");
    strcpy(programs[0].metadata.title,"FIRST PROGRAM TITLE IS LONG");
    programs[0].metadata.confidence=PROGRAM_PAYLOAD_VALID;programs[0].seekable=true;
    programs[0].metadata.valid=PROGRAM_TITLE|PROGRAM_SIZE|PROGRAM_LOAD|PROGRAM_EXEC|PROGRAM_RECORDED_MODE;
    programs[0].metadata.file_type=1;programs[0].metadata.load=0x1200;programs[0].metadata.exec=0x1200;
    programs[0].metadata.protocol=PROGRAM_SHARP;programs[0].validity=TAPE_VALIDITY_VERIFIED;
    programs[0].metadata.declared_size=65535;programs[0].range_flags=TAPE_RANGE_TIME_VALID;
    programs[0].chain.time_end_us=60000000;
    strcpy(programs[1].metadata.title,"UNCERTAIN PART");
    tape_transport_view_t tx={.user_paused=true,.total_us=6000000000ULL,.tape_position_us=3000000,.absolute_time=true};
    ui_model_t model;tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);
    assert(strstr(model.line[0],"90%") && strstr(model.line[4],"CANDIDATE"));
    capture_model(&model,"tape-analysis");
    assert(tape_contents_event(&v,&idx,BTN_STOP,0,&tx)==TAPE_ACTION_CANCEL_SCAN);
    v.page=TAPE_UI_LIST;idx.cache_hit=true;tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);
    assert(!model.line[6][0] && strstr(model.line[7],"OPEN") && !strstr(model.line[7],"CACHE"));
    capture_model(&model,"tape-contents");
    tape_program_t many[6]={0};tape_index_t list=idx;list.programs=many;list.program_count_recognized=6;list.truncated=true;
    for(unsigned i=0;i<6;++i)snprintf(many[i].metadata.title,sizeof(many[i].metadata.title),"PROGRAM %u",i+1);
    v.selected=3;tape_contents_render(&v,&list,&model,0,&tx);fits(&model);
    assert(strstr(model.line[2],"PROGRAM 1") && model.line[4][0]=='}' && strstr(model.line[6],"PROGRAM 5"));
    v.selected=6;tape_contents_render(&v,&list,&model,0,&tx);fits(&model);
    assert(strstr(model.line[2],"PROGRAM 2") && model.line[6][0]=='}' && strstr(model.line[6],"PROGRAM 6"));
    for(unsigned i=0;i<8;++i)assert(!strstr(model.line[i],"INDEX") && !strstr(model.line[i],"CACHE") && !strstr(model.line[i],"TRUNCATED"));
    v.selected=0;
    assert(tape_contents_event(&v,&idx,BTN_NEXT,0,&tx)==TAPE_ACTION_NONE && v.selected==1);
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_DETAIL);
    tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);assert(strstr(model.line[5],"1200"));
    assert(!strcmp(model.line[0],"PROGRAM 1/2"));
    capture_model(&model,"tape-details");
    for(unsigned validity=0;validity<3;++validity) {
        programs[0].validity=(tape_validity_t)validity;programs[0].seekable=validity==TAPE_VALIDITY_VERIFIED;
        tape_contents_render(&v,&idx,&model,0,&tx);assert(!strcmp(model.line[0],"PROGRAM 1/2"));
    }
    programs[0].validity=TAPE_VALIDITY_VERIFIED;programs[0].seekable=true;
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_START && v.selected==1);
    tx.playing=true;tx.user_paused=false;
    assert(tape_contents_event(&v,&idx,BTN_NEXT,0,&tx)==TAPE_ACTION_GAP_NEXT);
    assert(tape_contents_event(&v,&idx,BTN_PREV,0,&tx)==TAPE_ACTION_GAP_PREV);
    assert(tape_contents_event(&v,&idx,BTN_NEXT_LONG,0,&tx)==TAPE_ACTION_BEGIN_SEEK && v.page==TAPE_UI_TRANSPORT);
    /* The app must acknowledge physical pause, then replay the same long
     * gesture using the frozen connector clock, never a prefetched position. */
    tx.playing=false;tx.user_paused=true;
    assert(tape_contents_event(&v,&idx,BTN_NEXT,0,&tx)==TAPE_ACTION_GAP_NEXT);
    tx.waiting=true;assert(tape_contents_event(&v,&idx,BTN_NEXT,0,&tx)==TAPE_ACTION_NONE);tx.waiting=false;
    assert(tape_contents_event(&v,&idx,BTN_NEXT_LONG,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_SEEK && v.seek_origin_us==3000000);
    assert(tape_contents_event(&v,&idx,BTN_STOP,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_TRANSPORT);
    tx.playing=true;tx.user_paused=false;
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_PAUSE);
    tx.playing=false;tx.user_paused=true;
    assert(tape_contents_event(&v,&idx,BTN_NEXT_LONG,1000,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_SEEK);
    assert(v.seek_us==8000000); /* Original 3s + initial 5s. */
    assert(tape_contents_event(&v,&idx,BTN_NEXT_REPEAT,3000,&tx)==TAPE_ACTION_NONE && v.seek_us==23000000);
    assert(tape_contents_event(&v,&idx,BTN_NEXT_REPEAT,7000,&tx)==TAPE_ACTION_NONE && v.seek_us==83000000);
    assert(tape_contents_event(&v,&idx,BTN_NEXT_RELEASE,7100,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_SEEK);
    assert(!v.seek_direction);
    capture_transport(&v,&idx,"tape-seek");
    /* Target 83s is in the second entry, not selected entry 1. Only its
     * verified index range may supply preview metadata; no live state changes. */
    programs[1].metadata=programs[0].metadata;strcpy(programs[1].metadata.title,"SECOND PROGRAM");
    programs[1].range_flags=TAPE_RANGE_TIME_VALID;programs[1].chain.time_begin_us=60000000;programs[1].chain.time_end_us=120000000;
    programs[1].seekable=true;programs[1].validity=TAPE_VALIDITY_VERIFIED;
    ui_transport_view_t preview={.program=programs[0].metadata};
    tape_contents_apply_transport(&v,&idx,&preview);
    assert(v.selected==1 && preview.preview_metadata && preview.seek_preview && !strcmp(preview.program.title,"SECOND PROGRAM"));
    ui_set_transport_view(false,&preview);tick+=100;ui_render(&model);
    expect_text(0,8,"SECOND PROGRAM",1);expect_text(0,48,"INDEX PREVIEW",1);
    artifact("tape-seek");
    uint64_t target=v.seek_us;
    v.seek_us=120000000;tape_contents_apply_transport(&v,&idx,&preview);assert(!preview.program.valid && !preview.preview_metadata);
    v.seek_us=60000000;programs[1].validity=TAPE_VALIDITY_UNCERTAIN;
    tape_contents_apply_transport(&v,&idx,&preview);assert(!preview.program.valid && !preview.preview_metadata);
    programs[1].validity=TAPE_VALIDITY_VERIFIED;programs[0].chain.time_end_us=70000000;
    tape_contents_apply_transport(&v,&idx,&preview);assert(!preview.program.valid && !preview.preview_metadata); /* overlap */
    programs[0].chain.time_end_us=60000000;v.seek_us=3000000;
    tape_contents_apply_transport(&v,&idx,&preview);assert(preview.preview_metadata && !strcmp(preview.program.title,programs[0].metadata.title));
    tape_index_t thin=idx;thin.program_count_recognized=0;thin.programs=NULL;thin.time_only=true;
    tape_contents_apply_transport(&v,&thin,&preview);assert(!preview.program.valid && !preview.preview_metadata);
    v.seek_us=target;programs[1].seekable=false;programs[1].validity=TAPE_VALIDITY_UNCERTAIN;
    assert(tape_contents_event(&v,&idx,BTN_STOP,7200,&tx)==TAPE_ACTION_NONE && v.seek_us==3000000 && v.page==TAPE_UI_TRANSPORT);
    preview.program=programs[0].metadata;tape_contents_apply_transport(&v,&idx,&preview);
    assert(!strcmp(preview.program.title,programs[0].metadata.title)); /* cancel leaves live snapshot intact */
    tape_contents_event(&v,&idx,BTN_PREV_LONG,8000,&tx);assert(v.seek_us==0);
    assert(tape_contents_event(&v,&idx,BTN_PLAY,8100,&tx)==TAPE_ACTION_SEEK && v.page==TAPE_UI_TRANSPORT);
    capture_transport(&v,&idx,"tape-unverified-start");
    capture_transport(&v,&idx,"tape-play-time");
    assert(tape_contents_event(&v,&idx,BTN_STOP,0,&tx)==TAPE_ACTION_STOP && v.page==TAPE_UI_LIST);
    v.selected=2;tape_contents_event(&v,&idx,BTN_PLAY,0,&tx);tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);
    assert(strstr(model.line[7],"PLAY") && strstr(model.line[6],"~01:00"));
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_START_TIME && v.selected==2 && v.page==TAPE_UI_TRANSPORT && v.seek_us==60000000);
    tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);
    for(unsigned i=0;i<8;++i)assert(!strstr(model.line[i],"UNVERIFIED"));
    v.page=TAPE_UI_DETAIL;programs[1].range_flags=0;
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_DETAIL);
    tape_contents_render(&v,&idx,&model,0,&tx);assert(!strcmp(model.line[6],"START --"));
    assert(tape_contents_event(&v,&idx,BTN_STOP,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_LIST);
    tape_contents_event(&v,&idx,BTN_NEXT,0,&tx);assert(v.selected==0);
    tape_contents_event(&v,&idx,BTN_PLAY,0,&tx);
    assert(tape_contents_event(&v,&idx,BTN_PLAY,0,&tx)==TAPE_ACTION_START && v.selected==0);
    v.page=TAPE_UI_ERROR;tape_contents_render(&v,&idx,&model,0,&tx);fits(&model);
    assert(tape_contents_event(&v,&idx,BTN_STOP,0,&tx)==TAPE_ACTION_CLOSE);
    /* Unknown length must not clamp a live absolute clock to zero or invent
     * a total. The footer retains the physical byte percentage during preview. */
    tape_contents_open(&v);v.page=TAPE_UI_TRANSPORT;
    thin.duration_known=false;thin.waveform_us=thin.waveform_ticks16=0;
    thin.source.size_bytes=100000;
    thin.source.format=FILE_FORMAT_L16;strcpy(thin.source.path,"/sd/UNKNOWN.L16");
    tx.user_paused=true;tx.playing=tx.finished=false;tx.tape_position_us=7000000;tx.source_offset=37000;
    assert(tape_contents_event(&v,&thin,BTN_NEXT_LONG,0,&tx)==TAPE_ACTION_NONE && v.page==TAPE_UI_SEEK);
    assert(v.seek_by_byte && v.seek_origin_offset==37000 && v.seek_offset==38000);
    preview=(ui_transport_view_t){.progress=37,.filename="UNKNOWN.L16",.tx={.fifo_used=2048,.fifo_capacity=4095}};
    tape_contents_apply_transport(&v,&thin,&preview);
    assert(!preview.seconds && !preview.total_seconds && preview.progress==38);
    ui_set_transport_view(false,&preview);tick+=100;ui_render(&model);
    expect_text(0,56,"38 %",1);for(int y=48;y<56;++y)for(int x=0;x<128;++x)assert(!pixel(x,y));
    assert(!count_text("DETECTING"));
    expect_text(80,56,"BUF  50%",1);artifact("tape-unknown-length");
    tape_contents_event(&v,&thin,BTN_NEXT_REPEAT,2000,&tx);assert(v.seek_offset==43000);
    tape_contents_event(&v,&thin,BTN_PREV_LONG,3000,&tx);assert(v.seek_offset==42000);
    tape_contents_event(&v,&thin,BTN_STOP,3001,&tx);assert(v.seek_offset==37000 && v.page==TAPE_UI_TRANSPORT);
    tape_contents_event(&v,&thin,BTN_NEXT_LONG,4000,&tx);
    assert(tape_contents_event(&v,&thin,BTN_PLAY,1,&tx)==TAPE_ACTION_SEEK);
    puts("PASS analyzer UI: inline seek, gap click actions without MOTOR-wait interference, estimated-time candidate starts without warning, missing time never invented, target index preview");
    return 0;
}
