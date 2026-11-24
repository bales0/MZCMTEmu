/* Reuse the existing fake RMT peripheral, never substitute player/FIFO code. */
#define main existing_rmt_regression_main
#include "test_raw_rmt.c"
#undef main
#include "formats/tape_analyzer.h"

static const tape_index_t *scan_tape(const char *path) {
    assert(tape_analyzer_begin(path));while(tape_analyzer_step()){}
    const tape_index_t *idx=tape_analyzer_result();assert(idx->state==TAPE_INDEX_FINISHED);return idx;
}
static bool cancel_seek(void *ctx){(void)ctx;return false;}
static void finish_indexed_program(void) {
    unsigned budget=250000;
    while(player_get_state()==PLAYER_STATE_PLAYING) {
        assert(budget--);
        if(tx.running && RMT.clock)advance(remaining);else wall_ticks+=10000ULL*16;
        player_service();
    }
    assert(player_get_state()==PLAYER_STATE_EOF && !tx.running && !pin && !routed);
}
static void indexed_edges(void) {
    FILE *f=fopen("seek.l16","wb");assert(f);
    for(unsigned i=0;i<6000;++i)assert(fputc(i%2 ? 17:255-23,f)>=0);
    assert(!fclose(f));
    const tape_index_t *idx=scan_tape("seek.l16");
    symbol_digest=1469598103934665603ULL;uint64_t before=active_ticks;player_start("seek.l16");
    drain_player();assert(player_get_state()==PLAYER_STATE_EOF);
    uint64_t duration=active_ticks-before,hash=symbol_digest;
    assert(duration==(idx->waveform_us+800000)*16);
    /* Ordinary PLAY enters seek by pausing first, without a content scan. */
    player_start("seek.l16");assert(player_can_time_seek() && !player_time_index(NULL,NULL));
    for(unsigned i=0;i<40;++i){advance(10000*16);player_service();}
    player_pause_user();uint64_t held=raw_pulse_get_completed_us();
    const tape_index_t *light=player_time_index(NULL,NULL);
    assert(light && light->time_only && !light->programs && !light->program_count_recognized && !light->program_count_verified);
    assert(light->waveform_us==idx->waveform_us && player_tape_position_us()==held-300000);
    uint64_t full=player_tape_total_us();
    assert(full==light->waveform_us && player_get_total_seconds()==full/1000000);
    assert(player_prepare_tape(light,TAPE_INDEX_NO_ENTRY,1000000,true,NULL,NULL));
    assert(player_get_position_seconds()==1 && player_tape_total_us()==full);
    assert(player_prepare_tape(light,TAPE_INDEX_NO_ENTRY,1500000,true,NULL,NULL));
    assert(player_tape_position_us()==1500000 && player_tape_total_us()==full);
    assert(player_get_progress_percent()==1500000*100/full);
    assert(player_prepare_tape(light,TAPE_INDEX_NO_ENTRY,1000000,true,NULL,NULL));
    before=active_ticks;player_resume_user();drain_player();
    assert(player_get_state()==PLAYER_STATE_EOF && active_ticks-before==(light->waveform_us-1000000+800000)*16);
    player_stop();unsigned starts=transmits;
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,0,false,NULL,NULL));
    assert(player_get_state()==PLAYER_STATE_PAUSED && !tx.running && transmits==starts && !routed);
    assert(!player_tape_elapsed_us() && !player_tape_position_us() && player_tape_total_us()==full);
    symbol_digest=1469598103934665603ULL;before=active_ticks;
    player_resume_motor();assert(player_get_state()==PLAYER_STATE_PAUSED);
    player_resume_user();assert(player_get_state()==PLAYER_STATE_PLAYING);
    assert(!player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,1000000,true,NULL,NULL)); /* no live seek */
    drain_player();assert(player_get_state()==PLAYER_STATE_EOF);
    assert(active_ticks-before==duration && symbol_digest==hash);
    assert(player_tape_position_us()==idx->waveform_us && player_get_progress_percent()==100);
    assert(player_get_position_seconds()==full/1000000 && player_get_total_seconds()==full/1000000);

    /* Partial DMA half-symbol + pending source pulses must not survive reset. */
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,0,false,NULL,NULL));player_resume_user();
    for(unsigned i=0;i<40;++i){advance(10000*16);player_service();}
    advance(1234);player_pause_user();
    assert(player_get_state()==PLAYER_STATE_PAUSED && tx.running);
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,1000000,true,NULL,NULL));
    assert(!tx.running && !routed && !player_tape_elapsed_us() && player_tape_position_us()==1000000);
    raw_pulse_status_t status;raw_pulse_status(&status);assert(!status.running && status.fifo_used>=256);
    before=active_ticks;player_resume_user();drain_player();
    assert(player_get_state()==PLAYER_STATE_EOF && active_ticks-before==(idx->waveform_us-1000000+800000)*16);
    uint64_t frozen=player_tape_position_us();player_stop();advance(10000000);
    assert(player_tape_position_us()==frozen && !tx.running && !routed && !pin);
    starts=transmits;
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,UINT64_MAX,true,NULL,NULL));
    assert(player_get_state()==PLAYER_STATE_EOF && !tx.running && !raw_pulse_level() && transmits==starts);
    assert(player_tape_position_us()==idx->waveform_us && player_get_progress_percent()==100);

    assert(!player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,10,true,cancel_seek,NULL));
    assert(player_get_state()==PLAYER_STATE_STOPPED && !tx.running && !raw_pulse_level() && !routed);
    player_stop();cmt_storage_invalidate();
    assert(!player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,0,false,NULL,NULL));
    assert(!tx.running && !routed && !raw_pulse_level());
    assert(!remove("seek.l16"));
}
static void indexed_programs(void) {
    fixture("indexed.mzf");assert(edge_writer_open("indexed.l16",62500));
    for(unsigned record=0;record<2;++record) {
    if(record){FILE *f=fopen("indexed.mzf","r+b");assert(f && !fseek(f,1,SEEK_SET));assert(fwrite("SECOND DMA\r",1,11,f)==11 && !fclose(f));}
    sharp_stream_info_t info;assert(sharp_stream_open("indexed.mzf",1,record ? LOADER_MODE_IC_1_4:LOADER_MODE_TC_1_3,&info));
    uint8_t level;uint32_t ticks;bool count;
    while(!sharp_stream_is_eof()) {
        while(sharp_stream_next_pulse(&level,&ticks,&count))assert(edge_writer_write_interval_units(level,(ticks+128)/256));
        if(sharp_stream_is_eof())break;
        uint32_t pause=sharp_stream_boundary_delay_us(false);if(pause)assert(edge_writer_write_interval_units(0,(pause+8)/16));
        assert(sharp_stream_advance());
    }
    sharp_stream_close();assert(edge_writer_write_interval_units(0,187500));
    }
    assert(edge_writer_finalize());
    const tape_index_t *idx=scan_tape("indexed.l16");
    assert(idx->program_count_verified==2 && idx->programs[0].seekable && idx->programs[1].seekable);
    uint64_t expected=idx->programs[0].chain_end_ticks16-idx->programs[0].chain_begin_ticks16+800000*16;
    assert(player_prepare_tape(idx,0,0,false,NULL,NULL));
    program_metadata_t m;player_metadata(&m);assert(!(m.valid&PROGRAM_TITLE));
    assert(player_preflight_metadata(&m) && !strcmp(m.title,"DMA TEST"));
    uint64_t before=active_ticks;player_resume_user();finish_indexed_program();
    assert(player_get_state()==PLAYER_STATE_EOF && active_ticks-before==expected);
    player_metadata(&m);assert(m.confidence==PROGRAM_PAYLOAD_VALID && m.declared_size==4 && !strcmp(m.title,"DMA TEST"));
    assert(player_tape_position_us()==idx->programs[0].chain.time_end_us);
    assert(player_tape_total_us()==idx->waveform_us && player_get_progress_percent()<100);
    const tape_program_t *second=&idx->programs[1];assert(second->chain_begin_ticks16>idx->programs[0].chain_end_ticks16);
    assert(player_prepare_tape(idx,1,0,false,NULL,NULL));
    assert(player_tape_position_us()==second->chain_begin_ticks16/16 && player_get_position_seconds()==second->chain.time_begin_us/1000000);
    assert(player_get_total_seconds()==idx->waveform_us/1000000 && !player_tape_elapsed_us());
    assert(player_preflight_metadata(&m) && !strcmp(m.title,"SECOND DMA"));
    before=active_ticks;player_resume_user();finish_indexed_program();
    assert(player_get_state()==PLAYER_STATE_EOF && active_ticks-before==second->chain_end_ticks16-second->chain_begin_ticks16+800000*16);
    player_metadata(&m);assert(m.confidence==PROGRAM_PAYLOAD_VALID && !strcmp(m.title,"SECOND DMA"));
    assert(player_tape_position_us()==second->chain.time_end_us && player_tape_total_us()==idx->waveform_us);
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,idx->programs[0].chain.time_end_us+100000,true,NULL,NULL));
    assert(player_tape_unverified_start() && !player_preflight_metadata(&m));
    player_metadata(&m);assert(!(m.valid&(PROGRAM_TITLE|PROGRAM_SIZE|PROGRAM_LOAD|PROGRAM_EXEC|PROGRAM_RECORDED_MODE)));
    /* An uncertain candidate remains available as an explicit time start,
     * without weakening the verified-program API or inventing boundaries. */
    tape_index_t estimated=*idx;tape_program_t candidate=*second;
    candidate.seekable=false;candidate.validity=TAPE_VALIDITY_UNCERTAIN;
    estimated.programs=&candidate;estimated.program_count_recognized=1;
    assert(!player_prepare_tape(&estimated,0,0,false,NULL,NULL));
    assert(player_prepare_tape(&estimated,TAPE_INDEX_NO_ENTRY,candidate.chain.time_begin_us,true,NULL,NULL));
    assert(player_tape_position_us()==candidate.chain.time_begin_us && !player_preflight_metadata(&m));
    before=active_ticks;player_resume_user();drain_player();
    assert(active_ticks-before==(idx->waveform_us-candidate.chain.time_begin_us+800000)*16);
    player_metadata(&m);assert(m.confidence==PROGRAM_PAYLOAD_VALID && !strcmp(m.title,"SECOND DMA"));
    uint64_t gap_target=0;
    assert(tape_gap_find(idx,idx->programs[0].chain.time_end_us,true,&gap_target,NULL,NULL)==TAPE_GAP_FOUND);
    assert(gap_target<second->chain.time_begin_us && second->chain.time_begin_us-gap_target<=TAPE_GAP_LEAD_US+2000);
    assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,gap_target,true,NULL,NULL));
    assert(player_get_state()==PLAYER_STATE_PAUSED && !tx.running && player_tape_position_us()==gap_target);
    player_metadata(&m);assert(!(m.valid&PROGRAM_TITLE));
    before=active_ticks;player_resume_user();drain_player();
    assert(active_ticks-before==(idx->waveform_us-gap_target+800000)*16);
    player_metadata(&m);assert(m.confidence==PROGRAM_PAYLOAD_VALID && !strcmp(m.title,"SECOND DMA"));
    player_stop();assert(!remove("indexed.mzf") && !remove("indexed.l16"));
}
static void indexed_gap_navigation(void) {
    assert(edge_writer_open("navigation.l16",62500));
    for(unsigned i=0;i<3;++i) {
        assert(edge_writer_write_interval_units(0,31250)); /* 500ms */
        for(unsigned j=0;j<64;++j) {
            assert(edge_writer_write_interval_units(1,100));assert(edge_writer_write_interval_units(0,100));
        }
    }
    assert(edge_writer_finalize());const tape_index_t *idx=scan_tape("navigation.l16");
    assert(idx->gaps_ready && idx->gap_count==3);
    for(unsigned pass=0;pass<2;++pass) {
        if(pass)idx=tape_time_prepare("navigation.l16",NULL,NULL);
        assert(idx && idx->gaps_ready && idx->gap_count==3);
        uint64_t first=idx->gaps[0].end_us-TAPE_GAP_LEAD_US;
        uint64_t second=idx->gaps[1].end_us-TAPE_GAP_LEAD_US,target=0;
        assert(tape_gap_find(idx,0,true,&target,NULL,NULL)==TAPE_GAP_FOUND && target==first);
        assert(tape_gap_find(idx,first+100000,true,&target,NULL,NULL)==TAPE_GAP_FOUND && target==second);
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,target,true,NULL,NULL));player_resume_user();
        for(unsigned i=0;i<45;++i){advance(10000ULL*16);player_service();}
        player_pause_user();uint64_t advanced=player_tape_position_us();
        assert(advanced>second && advanced<second+200000);
        /* Actual resumed PLAY moves beyond the landing point; a plain nearest
         * query repeats it. The navigation filter must select the prior gap. */
        assert(tape_gap_find(idx,advanced,false,&target,NULL,NULL)==TAPE_GAP_FOUND && target==second);
        assert(tape_gap_find_excluding(idx,advanced,false,second,&target,NULL,NULL)==TAPE_GAP_FOUND && target==first);
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,target,true,NULL,NULL));
        assert(player_tape_position_us()==first && player_tape_total_us()==idx->waveform_us && !tx.running);
        player_resume_user();
        for(unsigned i=0;i<45;++i){advance(10000ULL*16);player_service();}
        player_pause_user();advanced=player_tape_position_us();assert(advanced>first && advanced<first+200000);
        assert(tape_gap_find_excluding(idx,advanced,false,first,&target,NULL,NULL)==TAPE_GAP_NONE);
        assert(tape_gap_find(idx,advanced,true,&target,NULL,NULL)==TAPE_GAP_FOUND && target==second);
        player_stop();
    }
    assert(!remove("navigation.l16"));
    puts("PASS real RMT gap navigation: 500ms gaps, analyzer and ordinary PLAY indexes, PREV after auto-resume reaches prior landing, no wrap and forward resumes normal order");
}
static edge_scan_action_t skip_length(uint32_t done,uint32_t total,void *context) {
    (void)done;(void)total;(void)context;return EDGE_SCAN_SKIP;
}
static unsigned navigation_polls;static bool navigation_saw_full;
static bool navigation_poll(void *context) {
    (void)context;++navigation_polls;
    if(tape_gap_scan_percent()==100)navigation_saw_full=true;
    return true;
}
static void unknown_length_navigation(void) {
    edge_stream_set_scan_control(skip_length,NULL);
    for(unsigned format=0;format<2;++format) {
        unsigned unit=format ? 16:50;const char *path=format ? "unknown.l16":"unknown.lep";
        assert(edge_writer_open(path,1000000/unit));
        for(unsigned g=0;g<3;++g) {
            assert(edge_writer_write_interval_units(0,500000/unit));
            for(unsigned i=0;i<16;++i){assert(edge_writer_write_interval_units(1,100));assert(edge_writer_write_interval_units(0,100));}
        }
        for(unsigned i=0;i<100000;++i)assert(edge_writer_write_interval_units(i%2,100));
        assert(edge_writer_finalize());player_start(path);assert(player_get_state()==PLAYER_STATE_PLAYING);
        assert(!player_get_total_seconds());player_pause_user();
        navigation_polls=0;navigation_saw_full=false;
        const tape_index_t *idx=player_time_index(navigation_poll,NULL);
        assert(idx && idx->scan_complete && !idx->duration_known && !idx->waveform_us && !idx->cursor_read_bytes);
        assert(navigation_polls==1 && !navigation_saw_full && !idx->programs && !idx->blocks);
        uint64_t target=0;
        assert(tape_gap_find(idx,0,true,&target,navigation_poll,NULL)==TAPE_GAP_FOUND && target==300000);
        assert(!idx->duration_known && !idx->gaps_ready && !navigation_saw_full);
        tape_cursor_t cursor={0};
        assert(tape_cursor_seek(&cursor,idx,50000000,navigation_poll,NULL));
        assert(cursor.ticks16==50000000ULL*16 && cursor.read_bytes<idx->source.size_bytes/2 && !idx->duration_known);
        uint64_t base=cursor.offset-(cursor.remaining_ticks16 ? 1:0);tape_cursor_close(&cursor);
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,50000000,true,navigation_poll,NULL));
        assert(!player_get_total_seconds() && !idx->duration_known && player_tape_position_us()==50000000);
        unsigned initial=player_get_progress_percent();assert(initial==base*100/idx->source.size_bytes);
        assert(!raw_pulse_completed_source_bytes());player_resume_user();
        for(unsigned i=0;i<200;++i){advance(10000ULL*16);player_service();}
        player_pause_user();assert(raw_pulse_completed_source_bytes()>0);
        unsigned percent=player_get_progress_percent();
        assert(percent==(base+raw_pulse_completed_source_bytes())*100/idx->source.size_bytes && percent>=initial);
        assert(!player_get_total_seconds());advance(1000000ULL*16);assert(player_get_progress_percent()==percent);
        player_stop();assert(player_get_progress_percent()==percent);
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,10000000,true,navigation_poll,NULL));
        assert(player_tape_position_us()==10000000 && !player_get_total_seconds() && player_get_progress_percent()<percent);
        assert(tape_gap_find(idx,10000000,false,&target,navigation_poll,NULL)==TAPE_GAP_FOUND);
        assert(target<2000000 && !idx->duration_known && !idx->gaps_ready && !navigation_saw_full);
        player_stop();
        /* Seeking beyond EOF discovers the real total instead of returning a
         * fake maximum duration or a source error. No DMA transaction starts. */
        unsigned starts=transmits;
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,UINT64_MAX,true,navigation_poll,NULL));
        assert(player_get_state()==PLAYER_STATE_EOF && idx->duration_known && !tx.running && transmits==starts);
        assert(player_get_progress_percent()==100 && player_tape_position_us()==idx->waveform_us);
        player_stop();assert(!remove(path));
        /* A normal streaming pass may learn the total at source EOF; this
         * must keep the original physical clock and never count TX padding. */
        assert(edge_writer_open(path,1000000/unit));
        for(unsigned i=0;i<1200;++i)assert(edge_writer_write_interval_units(i%2,100));
        assert(edge_writer_finalize());player_start(path);player_pause_user();
        idx=player_time_index(NULL,NULL);assert(idx && !idx->duration_known);
        assert(player_prepare_tape(idx,TAPE_INDEX_NO_ENTRY,0,true,NULL,NULL) && !player_get_total_seconds());
        uint64_t expected=1200ULL*100*unit,before=active_ticks;
        player_resume_user();drain_player();
        assert(player_get_state()==PLAYER_STATE_EOF && active_ticks-before==(expected+800000)*16);
        assert(player_tape_position_us()==expected && player_tape_total_us()==expected);
        idx=player_time_index(NULL,NULL);assert(idx && idx->duration_known && idx->waveform_us==expected);
        player_stop();assert(!remove(path));
    }
    edge_stream_set_scan_control(NULL,NULL);
    puts("PASS unknown-length LEP/L16: navigation identity only, no forced length scan, bounded forward/back gap search, seek prefix reads only, physical byte percentages/freeze and safe EOF discovery");
}
static void percent_byte_transport(void) {
    FILE *f=fopen("percent.l16","wb");assert(f);
    for(unsigned i=0;i<100000;++i)assert(fputc(i%2 ? 7:251,f)>=0);
    assert(!fclose(f));edge_stream_set_scan_control(skip_length,NULL);
    player_start("percent.l16");player_pause_user();
    const tape_index_t *idx=player_time_index(NULL,NULL);assert(idx && !idx->duration_known);
    const uint64_t offsets[]={50000,20000,80000,50000};
    for(unsigned i=0;i<4;++i) {
        navigation_polls=0;
        assert(player_prepare_tape_offset(idx,offsets[i],navigation_poll,NULL));
        assert(navigation_polls<=8 && player_tape_source_offset()==offsets[i]);
        assert(!player_tape_absolute_time() && !player_get_total_seconds() && !player_tape_position_us());
        assert(player_get_progress_percent()==offsets[i]/1000);
        player_resume_user();for(unsigned j=0;j<45;++j){advance(10000ULL*16);player_service();}
        player_pause_user();assert(player_tape_source_offset()>offsets[i]);
        uint8_t held=player_get_progress_percent();player_stop();assert(player_get_progress_percent()==held);
    }
    unsigned starts=transmits;
    assert(player_prepare_tape_offset(idx,UINT64_MAX,NULL,NULL));
    assert(player_get_state()==PLAYER_STATE_EOF && transmits==starts && !tx.running && player_get_progress_percent()==100);
    player_stop();assert(player_get_progress_percent()==100);
    edge_stream_set_scan_control(NULL,NULL);assert(!remove("percent.l16"));
    puts("PASS percent byte transport: repeated offsets read no time prefix, no invented absolute clock/total, physical progress, pause/STOP freeze and direct EOF without TX");
}
static void l16_byte_transport(const char *path) {
    const tape_index_t *idx=tape_time_navigation(path,NULL,NULL);assert(idx && (idx->source.size_bytes==2577411 || idx->source.size_bytes==2589411) && !idx->duration_known);
    const unsigned percentages[]={10,70,35,90};
    for(unsigned i=0;i<4;++i) {
        uint64_t offset=idx->source.size_bytes*percentages[i]/100;
        navigation_polls=0;assert(player_prepare_tape_offset(idx,offset,navigation_poll,NULL));
        assert(navigation_polls<=8 && player_tape_source_offset()==offset && !player_tape_absolute_time());
        assert(player_get_progress_percent()==offset*100/idx->source.size_bytes && !player_get_total_seconds());
        program_metadata_t m;player_metadata(&m);assert(!(m.valid&PROGRAM_TITLE) && m.confidence==PROGRAM_UNKNOWN);
    }
    if(idx->source.size_bytes==2589411) {
        uint64_t current=0,previous=0;
        assert(tape_gap_find_offset(idx,900000,false,UINT64_MAX,&current,NULL,NULL)==TAPE_GAP_FOUND);
        assert(tape_gap_find_offset(idx,current,false,UINT64_MAX,&previous,NULL,NULL)==TAPE_GAP_FOUND && previous<current);
        assert(current>=790883 && current<792884 && previous>=134627 && previous<136628);
        const uint64_t starts[]={current,previous};
        for(unsigned i=0;i<2;++i) {
            assert(player_prepare_tape_offset(idx,starts[i],NULL,NULL) && player_tape_source_offset()==starts[i]);
            player_resume_user();for(unsigned j=0;j<45;++j){advance(10000ULL*16);player_service();}
            player_pause_user();assert(player_tape_source_offset()>starts[i] && !player_tape_absolute_time());
        }
        puts("PASS Export RMT: current and previous starts are distinct, restored byte offsets emit source and preserve pause/percent transport");
    }
    uint64_t offset=idx->source.size_bytes/2;
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,(long)offset,SEEK_SET));
    uint64_t suffix=0;int b;
    while((b=fgetc(f))!=EOF){int value=(int8_t)b;suffix+=(value==0 ? 127:value<0 ? -value:value)*16U;}
    assert(!ferror(f) && !fclose(f));
    assert(player_prepare_tape_offset(idx,offset,NULL,NULL));uint64_t before=active_ticks;
    player_resume_user();
    uint64_t budget=(idx->source.size_bytes-offset)*2+10000;
    while(player_get_state()==PLAYER_STATE_PLAYING) {
        assert(budget--);
        if(tx.running && RMT.clock)advance(remaining);else wall_ticks+=10000ULL*16;
        player_service();
    }
    assert(player_get_state()==PLAYER_STATE_EOF && !tx.running && !pin && !routed);
    assert(active_ticks-before==(suffix+800000)*16 && !player_tape_absolute_time() && !player_get_total_seconds());
    assert(player_tape_source_offset()==idx->source.size_bytes && player_get_progress_percent()==100);
    player_stop();assert(player_get_progress_percent()==100);
    puts("PASS L16 reference actual RMT: repeated 10/70/35/90 percent seeks without prefix scan, reset metadata, exact original suffix duration at 50 percent, EOF/STOP100 and no fake total");
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);player_init();indexed_edges();indexed_programs();indexed_gap_navigation();unknown_length_navigation();percent_byte_transport();
    if(argc>1)l16_byte_transport(argv[1]);
    puts("PASS indexed real player/RMT DMA: waveform hash matches ordinary PLAY, reset partial DMA/FIFO, pause precedence, physical time and frozen progress, safe chain end, live metadata, cancel and media invalidation");return 0;
}
