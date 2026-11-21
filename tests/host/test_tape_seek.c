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
    sharp_stream_close();assert(edge_writer_write_interval_units(0,62500));
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
    player_stop();assert(!remove("indexed.mzf") && !remove("indexed.l16"));
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);player_init();indexed_edges();indexed_programs();
    puts("PASS indexed real player/RMT DMA: waveform hash matches ordinary PLAY, reset partial DMA/FIFO, pause precedence, physical time and frozen progress, safe chain end, live metadata, cancel and media invalidation");return 0;
}
