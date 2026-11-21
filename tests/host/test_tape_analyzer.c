#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "formats/tape_analyzer.h"
#include "audio/wav_pulse_stream.h"
#include "audio/edge_writer.h"
#include "audio/sharp_stream.h"

static const tape_index_t *analyze(const char *path) {
    assert(tape_analyzer_begin(path));unsigned steps=0;
    while(tape_analyzer_step())assert(++steps<100000);
    const tape_index_t *idx=tape_analyzer_result();assert(idx->state==TAPE_INDEX_FINISHED);
    assert(idx->scan_complete && idx->duration_known && tape_analyzer_percent()==100);return idx;
}
static void source(const char *title) {
    uint8_t h[128]={1},data[]={0,128,1,255};memcpy(h+1,title,strlen(title));
    h[18]=4;h[21]=h[23]=0x12;
    FILE *f=fopen("source.mzf","wb");assert(f && fwrite(h,1,128,f)==128 && fwrite(data,1,4,f)==4 && !fclose(f));
}
static void record_source(loader_mode_t mode,unsigned unit) {
    sharp_stream_info_t original;assert(sharp_stream_open("source.mzf",1,mode,&original));assert(!original.fallback);
    uint8_t level;uint32_t ticks;bool count;
    while(!sharp_stream_is_eof()) {
        while(sharp_stream_next_pulse(&level,&ticks,&count))assert(edge_writer_write_interval_units(level,(ticks+unit/2)/unit));
        if(sharp_stream_is_eof())break;
        uint32_t pause=sharp_stream_boundary_delay_us(false);
        if(pause)assert(edge_writer_write_interval_units(0,(pause*16+unit/2)/unit));
        assert(sharp_stream_advance());
    }
    sharp_stream_close();
}
static void codecs(void) {
    const loader_mode_t modes[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2,LOADER_MODE_NORMAL_1_3,LOADER_MODE_NORMAL_1_4,
        LOADER_MODE_IC_1_1,LOADER_MODE_IC_1_2,LOADER_MODE_IC_1_3,LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_1,LOADER_MODE_TC_1_2,LOADER_MODE_TC_1_3};
    for(unsigned f=0;f<2;++f)for(unsigned m=0;m<sizeof(modes)/sizeof(modes[0]);++m) {
        const char *path=f ? "recorded.l16":"recorded.lep";
        source("REAL GAME\r");assert(edge_writer_open(path,f ? 62500:20000));record_source(modes[m],f ? 256:800);
        assert(edge_writer_write_interval_units(0,1000000/(f ? 16:50)) && edge_writer_finalize());
        tape_analyzer_invalidate();const tape_index_t *idx=analyze(path);
        printf("INDEX %s mode=%u programs=%u verified=%u\n",path,modes[m],idx->program_count_recognized,idx->program_count_verified);
        assert(idx->program_count_recognized==1);
        if(idx->program_count_verified!=1) {
            for(unsigned j=0;j<idx->block_count;++j) {
                const tape_block_t *b=&idx->blocks[j];
                printf("BLOCK kind=%u valid=%u offsets=%llu..%llu time=%llu..%llu\n",b->kind,b->validity,
                    (unsigned long long)b->range.offset_begin,(unsigned long long)b->range.offset_end,
                    (unsigned long long)b->range.time_begin_us,(unsigned long long)b->range.time_end_us);
            }
            assert(idx->program_count_verified==1);
        }
        const tape_program_t *p=&idx->programs[0];
        assert(p->metadata.confidence==PROGRAM_PAYLOAD_VALID);
        assert(p->chain_complete && p->seekable);
        assert(p->metadata.load==0x1200 && p->metadata.exec==0x1200 && p->metadata.declared_size==4);
        assert(!strcmp(p->metadata.title,"REAL GAME") && p->metadata.recorded_loader_mode==modes[m]);
        assert(p->chain.time_begin_us<p->chain.time_end_us && p->chain.time_end_us<=idx->waveform_us);
        assert(idx->checkpoints[p->start_checkpoint].source_offset==p->chain.offset_begin);
        assert(tape_analyzer_begin(path) && tape_analyzer_result()->cache_hit);
        assert(!remove(path));
    }
    assert(edge_writer_open("multi.l16",62500));
    for(unsigned i=0;i<5;++i) {
        char name[18];snprintf(name,sizeof(name),"GAME %u\r",i);source(name);
        record_source(LOADER_MODE_IC_1_4,256);assert(edge_writer_write_interval_units(0,62500));
    }
    assert(edge_writer_finalize());const tape_index_t *idx=analyze("multi.l16");
    assert(idx->program_count_recognized==5 && idx->program_count_verified==5);
    for(unsigned i=0;i<5;++i) {
        char expected[18];snprintf(expected,sizeof(expected),"GAME %u",i);
        assert(!strcmp(idx->programs[i].metadata.title,expected));
        if(i)assert(idx->programs[i].chain.offset_begin>idx->programs[i-1].chain.offset_end);
    }
}
static void u16(FILE *f,unsigned n){assert(fputc(n&255,f)>=0 && fputc((n>>8)&255,f)>=0);}
static void u32(FILE *f,uint32_t n){u16(f,n&65535);u16(f,n>>16);}
static void wav(unsigned rate) {
    FILE *f=fopen("signal.wav","wb");assert(f);unsigned n=rate/10;
    assert(fwrite("RIFF",1,4,f)==4);u32(f,48+n);assert(fwrite("WAVEJUNK",1,8,f)==8);u32(f,3);
    assert(fwrite("abc\0fmt ",1,8,f)==8);u32(f,16);u16(f,1);u16(f,1);u32(f,rate);u32(f,rate);u16(f,1);u16(f,8);
    assert(fwrite("data",1,4,f)==4);u32(f,n);
    for(unsigned i=0;i<n;++i)assert(fputc(i<777 ? 0x80:(i%31<8 ? 0xFF:i%31<24 ? 0x80:0),f)>=0);
    assert(!fclose(f));
}
/* Test fixture only: synthesize sampled audio from the generated five-game
 * tape. Production analysis never converts, extracts or writes a recording. */
static void multi_wav(void) {
    FILE *input=fopen("multi.l16","rb"),*out=fopen("multi.wav","wb");assert(input && out);
    assert(fwrite("RIFF",1,4,out)==4);u32(out,0);assert(fwrite("WAVEfmt ",1,8,out)==8);u32(out,16);
    u16(out,1);u16(out,1);u32(out,96000);u32(out,96000);u16(out,1);u16(out,8);
    assert(fwrite("data",1,4,out)==4);u32(out,0);
    uint64_t us=0,samples=0;uint8_t level=0;int byte;
    while((byte=fgetc(input))!=EOF) {
        int s=(int8_t)byte;if(s)level=s>0;
        us+=(s==0 ? 127:s<0 ? -s:s)*16U;
        uint64_t end=(us*96000+500000)/1000000;
        while(samples<end){assert(fputc(level ? 255:0,out)>=0);++samples;}
    }
    assert(!ferror(input) && !fclose(input));
    assert(!fseek(out,4,SEEK_SET));u32(out,36+(uint32_t)samples);
    assert(!fseek(out,40,SEEK_SET));u32(out,(uint32_t)samples);assert(!fclose(out));
    const tape_index_t *idx=analyze("multi.wav");
    assert(idx->program_count_recognized==5 && idx->program_count_verified==5);
    for(unsigned i=0;i<5;++i) {
        char title[18];snprintf(title,sizeof(title),"GAME %u",i);
        assert(!strcmp(idx->programs[i].metadata.title,title) && idx->programs[i].seekable);
    }
    assert(idx->waveform_ticks16==wav_pulse_time(samples,96000,16000000));
}
static uint8_t aux_level,cmt_level;
static unsigned cmt_short;
static void aux_interval(unsigned us) {
    assert(edge_writer_write_interval_units(aux_level,(us+8)/16));aux_level^=1;
}
static void cmt_symbol(bool symbol) {
    if(symbol)aux_interval(cmt_short*2);else {aux_interval(cmt_short);aux_interval(cmt_short);}
}
static void aux_indexes(void) {
    for(unsigned protocol=0;protocol<2;++protocol)for(unsigned bad=0;bad<2;++bad) {
        assert(edge_writer_open("aux.l16",62500));aux_level=0;
        if(!protocol) {
            uint8_t bytes[19]={0};memcpy(bytes+2,"SIN TEST  ",10);bytes[12]=4;
            for(unsigned i=0;i<18;++i)bytes[18]^=bytes[i];
            if(bad)bytes[18]^=1;
            for(unsigned i=0;i<1024;++i)aux_interval(612);
            aux_interval(612*31/100);aux_interval(612*35/100);
            for(unsigned i=0;i<19;++i)for(unsigned bit=0;bit<8;++bit) {
                unsigned us=612*(bytes[i]&(0x80U>>bit) ? 79:39)/100;aux_interval(us);aux_interval(us);
            }
        } else {
            uint8_t bytes[131]={0};bytes[1]=1;memcpy(bytes+2,"CPM TEST",8);bytes[19]=4;
            uint16_t checksum=0;for(unsigned i=1;i<=128;++i)checksum+=bytes[i];
            if(bad)checksum^=1;
            bytes[129]=(uint8_t)checksum;bytes[130]=(uint8_t)(checksum>>8);cmt_short=208;
            for(unsigned i=0;i<1536;++i)cmt_symbol(i%3!=0);
            cmt_symbol(true);cmt_level=0;
            for(unsigned i=0;i<131;++i)for(unsigned bit=0;bit<8;++bit) {
                uint8_t next=((bytes[i]>>bit)&1)^1;cmt_symbol(next!=cmt_level);cmt_level=next;
            }
        }
        aux_interval(1000);aux_interval(1000);assert(edge_writer_finalize());
        const tape_index_t *idx=analyze("aux.l16");
        assert(!idx->program_count_verified && !idx->index_complete);
        if(bad)assert(!idx->program_count_recognized);
        else {
            assert(idx->program_count_recognized==1 && !idx->programs[0].seekable);
            assert(!strcmp(idx->programs[0].metadata.title,protocol ? "CPM TEST":"SIN TEST"));
            assert(idx->programs[0].start_checkpoint==TAPE_INDEX_NO_ENTRY && idx->programs[0].aux_profile);
        }
        assert(!remove("aux.l16"));
    }
}
typedef struct {uint8_t level;uint64_t ticks;} run_t;
static run_t expected[4096],actual[4096];
static unsigned append(run_t *r,unsigned n,uint8_t level,uint32_t ticks) {
    if(n && r[n-1].level==level)r[n-1].ticks+=ticks;
    else {assert(n<4096);r[n++]=(run_t){level,ticks};}
    return n;
}
static unsigned read_cursor(tape_cursor_t *c,run_t *out) {
    unsigned n=0;uint8_t level;uint32_t ticks;
    while(tape_cursor_next(c,&level,&ticks))n=append(out,n,level,ticks);
    assert(!c->error);return n;
}
static tape_cursor_t cursor;
static void assert_suffix(const tape_index_t *idx,unsigned n,uint64_t target) {
    assert(tape_cursor_seek(&cursor,idx,target,NULL,NULL));
    unsigned count=read_cursor(&cursor,actual);tape_cursor_close(&cursor);
    uint64_t skip=target*16;
    if(skip>idx->waveform_ticks16)skip=idx->waveform_ticks16;
    unsigned first=0;while(first<n && skip>=expected[first].ticks){skip-=expected[first].ticks;++first;}
    assert(count==n-first);
    for(unsigned j=0;j<count;++j) {
        assert(actual[j].level==expected[first+j].level);
        assert(actual[j].ticks==expected[first+j].ticks-(j ? 0:skip));
    }
}
static void wave_seek(void) {
    const unsigned rates[]={44100,48000,96000};
    for(unsigned r=0;r<3;++r) {
        wav(rates[r]);tape_analyzer_invalidate();const tape_index_t *idx=analyze("signal.wav");
        assert(idx->program_count_recognized==0 && !idx->index_complete);
        wav_info_t info;assert(wav_reader_open("signal.wav",&info) && wav_pulse_stream_begin(info.sample_rate));
        unsigned n=0;uint8_t level;uint32_t ticks;
        while(wav_pulse_stream_next_ticks16(&level,&ticks))n=append(expected,n,level,ticks);
        assert(!wav_pulse_stream_error());wav_reader_close();
        assert(tape_cursor_open(&cursor,idx,NULL));unsigned got=read_cursor(&cursor,actual);tape_cursor_close(&cursor);
        assert(got==n);for(unsigned i=0;i<n;++i)assert(actual[i].level==expected[i].level && actual[i].ticks==expected[i].ticks);
        const unsigned targets[]={0,1,99,800,17600,17620,50000,99999,100000,UINT32_MAX};
        for(unsigned t=0;t<sizeof(targets)/sizeof(targets[0]);++t)assert_suffix(idx,n,targets[t]);
        const tape_index_t *light=tape_time_prepare("signal.wav",NULL,NULL);
        assert(light && light->time_only && !light->programs && !light->program_count_recognized && light->checkpoint_count==1);
        for(unsigned t=0;t<sizeof(targets)/sizeof(targets[0]);++t)assert_suffix(light,n,targets[t]);
        /* Reader probe/analyzer never consumes the live reader's descriptor. */
        assert(wav_reader_open("signal.wav",&info));uint8_t a[4];assert(wav_reader_read(a,4)==4);
        assert(tape_analyzer_begin("signal.wav") && tape_analyzer_result()->cache_hit);
        assert(wav_reader_read(a,4)==4 && a[0]==0x80);wav_reader_close();
    }
}
static void edges(void) {
    for(unsigned format=0;format<2;++format) {
        const char *path=format ? "values.l16":"values.lep";FILE *f=fopen(path,"wb");assert(f);
        unsigned unit=format ? 256:800;uint64_t total=0;uint8_t previous=0;unsigned n=0;
        for(unsigned i=0;i<4096;++i) {
            uint8_t b=(uint8_t)i;assert(fputc(b,f)>=0);int s=(int8_t)b;
            if(s)previous=s>0;
            unsigned duration=(s==0 ? 127:s<0 ? -s:s)*unit;
            total+=duration;n=append(expected,n,previous,duration);
        }
        assert(!fclose(f));const tape_index_t *idx=analyze(path);assert(idx->waveform_ticks16==total);
        assert(idx->program_count_recognized==0);
        assert(tape_cursor_open(&cursor,idx,NULL));unsigned got=read_cursor(&cursor,actual);tape_cursor_close(&cursor);
        assert(n==got);for(unsigned i=0;i<n;++i)assert(expected[i].level==actual[i].level && expected[i].ticks==actual[i].ticks);
        for(unsigned i=0;i<31;++i)assert_suffix(idx,n,idx->waveform_us*i/30);
        assert(tape_analyzer_begin(path) && tape_analyzer_result()->cache_hit);
        cmt_storage_invalidate();assert(!tape_index_source_valid(idx));
        assert(tape_analyzer_begin(path) && !tape_analyzer_result()->cache_hit);tape_analyzer_cancel();
        assert(tape_analyzer_result()->state==TAPE_INDEX_CANCELLED);
        idx=analyze(path);assert(!idx->cache_hit);
        tape_analyzer_invalidate();assert(tape_analyzer_begin(path));
        assert(!tape_analyzer_begin(path) && tape_analyzer_result()->state==TAPE_INDEX_SCANNING);
        cmt_storage_invalidate();assert(!tape_analyzer_step());
        assert(tape_analyzer_result()->state==TAPE_INDEX_ERROR && !tape_analyzer_result()->scan_complete);
        idx=analyze(path);
        f=fopen(path,"r+b");assert(f);assert(fputc(55,f)>=0 && !fclose(f));assert(!tape_index_source_valid(idx));
    }
}
static void bad_wave(void) {
    wav(44100);FILE *f=fopen("signal.wav","r+b");assert(f);
    assert(!fseek(f,4,SEEK_SET));u32(f,UINT32_MAX);assert(!fclose(f));
    wav_info_t info;assert(!wav_reader_probe("signal.wav",&info));
    tape_analyzer_invalidate();assert(!tape_analyzer_begin("signal.wav"));
    assert(tape_analyzer_result()->state==TAPE_INDEX_ERROR);
    wav(48000);f=fopen("signal.wav","r+b");assert(f);
    assert(!fseek(f,46,SEEK_SET));u16(f,16);assert(!fclose(f)); /* fmt bits/sample */
    assert(wav_reader_probe("signal.wav",&info) && !wav_reader_supported(&info));
    assert(!tape_analyzer_begin("signal.wav"));
    /* Legal RIFF order with data before fmt, plus odd-sized data padding. */
    f=fopen("ordered.wav","wb");assert(f);assert(fwrite("RIFF",1,4,f)==4);u32(f,42);
    assert(fwrite("WAVEdata",1,8,f)==8);u32(f,5);
    const uint8_t data[6]={0,192,128,64,0,0};assert(fwrite(data,1,6,f)==6);
    assert(fwrite("fmt ",1,4,f)==4);u32(f,16);u16(f,1);u16(f,1);u32(f,44100);u32(f,44100);u16(f,1);u16(f,8);
    assert(!fclose(f));assert(wav_reader_probe("ordered.wav",&info) && info.data_offset==20 && info.data_size==5);
    const tape_index_t *ordered=analyze("ordered.wav");
    assert(ordered->waveform_ticks16==wav_pulse_time(5,44100,16000000));
}
static void uncertain_and_limits(void) {
    source("CHECKSUM\r");assert(edge_writer_open("bad.l16",62500));record_source(LOADER_MODE_IC_1_4,256);
    assert(edge_writer_write_interval_units(0,62500) && edge_writer_finalize());
    const tape_index_t *idx=analyze("bad.l16");assert(idx->program_count_verified==1);
    uint64_t end=idx->programs[0].chain.offset_end;assert(end>128);
    FILE *input=fopen("bad.l16","rb"),*partial=fopen("truncated.l16","wb");assert(input && partial);
    for(uint64_t i=0;i<end-128;++i){int byte=fgetc(input);assert(byte!=EOF && fputc(byte,partial)>=0);}
    assert(!fclose(input) && !fclose(partial));idx=analyze("truncated.l16");
    assert(idx->program_count_recognized==1 && !idx->program_count_verified && !idx->programs[0].seekable);
    FILE *f=fopen("bad.l16","r+b");assert(f && !fseek(f,(long)(end-128),SEEK_SET));
    for(unsigned i=0;i<128;++i)assert(fputc(0,f)>=0);
    assert(!fclose(f));tape_analyzer_invalidate();idx=analyze("bad.l16");
    assert(idx->program_count_recognized==1 && !idx->program_count_verified && !idx->programs[0].seekable);
    /* A valid BASIC/header chain does not prove standalone module dependencies. */
    source("BASIC\r");f=fopen("source.mzf","r+b");assert(f && fputc(2,f)>=0 && !fclose(f));
    assert(edge_writer_open("basic.l16",62500));record_source(LOADER_MODE_NORMAL_1_1,256);assert(edge_writer_finalize());
    idx=analyze("basic.l16");assert(idx->program_count_recognized==1 && !idx->program_count_verified && !idx->programs[0].seekable);
    assert(edge_writer_open("dependent.l16",62500));record_source(LOADER_MODE_NORMAL_1_2,256);
    assert(edge_writer_write_interval_units(0,62500));source("DEPENDENT\r");record_source(LOADER_MODE_NORMAL_1_2,256);
    assert(edge_writer_finalize());idx=analyze("dependent.l16");
    assert(idx->program_count_recognized==2 && !idx->program_count_verified && !idx->programs[1].seekable);
    /* A partial/false leader is structural evidence, never a verified program. */
    f=fopen("false.l16","wb");assert(f);
    for(unsigned i=0;i<30000;++i)assert(fputc(i%2 ? 6:(uint8_t)-6,f)>=0);
    assert(!fclose(f));idx=analyze("false.l16");assert(!idx->program_count_recognized && !idx->index_complete);
    assert(edge_writer_open("capacity.l16",62500));
    for(unsigned i=0;i<TAPE_INDEX_PROGRAM_CAPACITY+1;++i) {
        char name[18];snprintf(name,sizeof(name),"CAP %u\r",i);source(name);record_source(LOADER_MODE_IC_1_4,256);
        assert(edge_writer_write_interval_units(0,62500));
    }
    assert(edge_writer_finalize());idx=analyze("capacity.l16");
    assert(idx->truncated && idx->program_count_recognized==TAPE_INDEX_PROGRAM_CAPACITY && !idx->index_complete);
    assert(!idx->program_count_verified && idx->duration_known);
}
static void long_tapes(void) {
    uint8_t bytes[1024]={0};
    for(unsigned mib=1;mib<=10;mib+=9) {
        FILE *f=fopen("long.l16","wb");assert(f);
        for(unsigned i=0;i<mib*1024;++i)assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));
        assert(!fclose(f));
        struct timespec before,after;timespec_get(&before,TIME_UTC);clock_t cpu=clock();
        tape_analyzer_invalidate();const tape_index_t *idx=analyze("long.l16");
        timespec_get(&after,TIME_UTC);
        double wall=(after.tv_sec-before.tv_sec)+(after.tv_nsec-before.tv_nsec)/1e9;
        double seconds=(double)(clock()-cpu)/CLOCKS_PER_SEC;
        assert(idx->waveform_us==(uint64_t)mib*1024*1024*127*16);
        assert(idx->checkpoint_count<=TAPE_INDEX_CHECKPOINT_CAPACITY && !idx->truncated);
        assert(idx->cursor_read_calls==mib*1024 && idx->cursor_read_bytes==(uint64_t)mib*1024*1024);
        uint64_t target=idx->waveform_us*97/100;
        assert(tape_cursor_seek(&cursor,idx,target,NULL,NULL));
        uint8_t level;uint32_t ticks;uint64_t sum=0;
        while(tape_cursor_next(&cursor,&level,&ticks)){assert(!level);sum+=ticks;}
        assert(!cursor.error && sum==(idx->waveform_us-target)*16);tape_cursor_close(&cursor);
        printf("BENCH L16 %uMiB wall=%.6f cpu=%.6f cursor_reads=%llu bytes=%llu checkpoints=%u\n",mib,wall,seconds,
            (unsigned long long)idx->cursor_read_calls,(unsigned long long)idx->cursor_read_bytes,idx->checkpoint_count);
        timespec_get(&before,TIME_UTC);cpu=clock();
        const tape_index_t *light=tape_time_prepare("long.l16",NULL,NULL);timespec_get(&after,TIME_UTC);
        assert(light && light->time_only && light->waveform_ticks16==idx->waveform_ticks16);
        assert(!light->programs && !light->blocks && !light->program_count_recognized && !light->index_complete);
        assert(light->checkpoint_count<=TAPE_INDEX_CHECKPOINT_CAPACITY && light->cursor_read_calls==mib*1024);
        printf("BENCH LENGTH_L16 %uMiB wall=%.6f cpu=%.6f cursor_reads=%llu bytes=%llu checkpoints=%u\n",mib,
            (after.tv_sec-before.tv_sec)+(after.tv_nsec-before.tv_nsec)/1e9,(double)(clock()-cpu)/CLOCKS_PER_SEC,
            (unsigned long long)light->cursor_read_calls,(unsigned long long)light->cursor_read_bytes,light->checkpoint_count);
        assert(tape_cursor_seek(&cursor,light,target,NULL,NULL));sum=0;
        while(tape_cursor_next(&cursor,&level,&ticks)){assert(!level);sum+=ticks;}
        assert(!cursor.error && sum==(light->waveform_us-target)*16);tape_cursor_close(&cursor);
    }
    for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=i%2 ? 1:255;
    for(unsigned mib=1;mib<=10;mib+=9) {
        FILE *f=fopen("dense.l16","wb");assert(f);
        for(unsigned i=0;i<mib*1024;++i)assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));
        assert(!fclose(f));struct timespec before,after;timespec_get(&before,TIME_UTC);clock_t cpu=clock();
        tape_analyzer_invalidate();const tape_index_t *idx=analyze("dense.l16");timespec_get(&after,TIME_UTC);
        assert(!idx->program_count_recognized && idx->waveform_us==(uint64_t)mib*1024*1024*16);
        printf("BENCH DENSE_L16 %uMiB wall=%.6f cpu=%.6f cursor_reads=%llu bytes=%llu checkpoints=%u\n",mib,
            (after.tv_sec-before.tv_sec)+(after.tv_nsec-before.tv_nsec)/1e9,(double)(clock()-cpu)/CLOCKS_PER_SEC,
            (unsigned long long)idx->cursor_read_calls,(unsigned long long)idx->cursor_read_bytes,idx->checkpoint_count);
    }
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    edges();wave_seek();bad_wave();codecs();multi_wav();aux_indexes();uncertain_and_limits();long_tapes();
    printf("PASS bounded read-only index, multi-program NORMAL/IC/TC, exact edge/WAV waveform and bidirectional seek, cache/cancel; pools=%zu bytes\n",tape_analyzer_pool_bytes());return 0;
}
