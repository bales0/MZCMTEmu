#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "audio/tap_stream.h"

/* Independent golden vectors from SD2CMT2-Reborn 600a788 tap_speed.cpp.
 * Verify actual level sequence, including the asymmetric sync and idle. */
static const uint16_t golden[4][5]={{9798,2321,2979,7749,3874},
    {7621,1805,2317,6027,3013},{6532,1547,1986,5166,2583},{5487,1300,1668,4340,2170}};
static uint64_t elapsed;
static size_t intervals;
static void expect(uint8_t level, uint32_t ticks, bool count) {
    uint8_t actual_level=99; uint32_t actual_ticks=0; bool actual_count=!count;
    assert(tap_stream_next_pulse(&actual_level,&actual_ticks,&actual_count));
    assert(actual_level==level && actual_ticks==ticks && actual_count==count);
    elapsed+=ticks; ++intervals;
}
static void write_block(FILE *f, const uint8_t *bytes, uint16_t length) {
    assert(fputc(length&255,f)!=EOF && fputc(length>>8,f)!=EOF);
    assert(fwrite(bytes,1,length,f)==length);
}
static void expect_block(const uint8_t *data, size_t length, int speed, bool last) {
    const uint16_t *t=golden[speed];
    for (unsigned i=0;i<(data[0] ? 1610U : 4032U);++i) {
        expect(0,t[0],false); expect(1,t[0],true);
    }
    expect(0,t[1],false); expect(1,t[2],true);
    for (size_t i=0;i<length;++i) for (int bit=7;bit>=0;--bit) {
        uint32_t ticks=(data[i]&(1U<<bit)) ? t[3] : t[4];
        expect(0,ticks,false); expect(1,ticks,true);
    }
    expect(0,t[0],false);
    if (!last) expect(0,data[0] ? 16000000U : 5600000U,false);
}
static void test_golden(void) {
    uint8_t header[19]={0},data[260],tail[]={0,0xA5};
    for (size_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)i;
    data[0]=255; header[1]=3; header[2]=0x80;
    FILE *f=fopen("golden.TaP","wb"); assert(f);
    write_block(f,header,sizeof(header)); write_block(f,data,sizeof(data));
    write_block(f,tail,sizeof(tail)); assert(!fclose(f));
    for (int speed=0;speed<4;++speed) {
        tap_stream_info_t info; elapsed=0; intervals=0;
        assert(tap_stream_open("golden.TaP",(tap_speed_t)speed,&info));
        assert(info.total_bytes==287 && info.block_count==3);
        assert(info.baud==(speed==0 ? 1400 : speed==1 ? 1800 : speed==2 ? 2100 : 2500));
        expect_block(header,sizeof(header),speed,false);
        expect_block(data,sizeof(data),speed,false);
        expect_block(tail,sizeof(tail),speed,true);
        assert(elapsed==info.total_ticks);
        assert(intervals==23855);
        uint8_t level; uint32_t ticks; bool count;
        assert(tap_stream_is_eof() && !tap_stream_has_error());
        assert(!tap_stream_next_pulse(&level,&ticks,&count));
        assert(!tap_stream_next_pulse(&level,&ticks,&count));
        tap_stream_close();
    }
    assert(remove("golden.TaP")==0);
    puts("PASS TAP all four speeds: golden pilot/sync/MSB data/terminator/gaps and exact totals");
}
static void invalid(const uint8_t *data, size_t size) {
    FILE *f=fopen("invalid.tap","wb"); assert(f);
    if (size) assert(fwrite(data,1,size,f)==size);
    assert(!fclose(f)); tap_stream_info_t info;
    memset(&info,0xCC,sizeof(info));
    assert(!tap_stream_open("invalid.tap",TAP_SPEED_1400,&info));
    assert(tap_stream_has_error() && !tap_stream_is_eof());
    assert(info.total_ticks==0 && info.block_count==0 && info.total_bytes==0);
    tap_stream_close(); assert(!tap_stream_has_error()); assert(!remove("invalid.tap"));
}
static void test_bad(void) {
    static const uint8_t cases[][8]={{0},{2,0,0},{0,0,0,0},{1,0,0,0},
        {2,0,1,0},{255,255,255,0},{2,0,0,0,2,0,255}};
    const size_t sizes[]={0,1,2,3,4,4,4,7};
    invalid(cases[0],sizes[0]); invalid(cases[0],sizes[1]);
    for (size_t i=1;i<7;++i) invalid(cases[i],sizes[i+1]);
    assert(!tap_stream_open(NULL,TAP_SPEED_1400,NULL)); tap_stream_close();
    assert(!tap_stream_open("missing.tap",TAP_SPEED_1400,NULL)); tap_stream_close();
    assert(!tap_stream_open("missing.tap",(tap_speed_t)-1,NULL)); tap_stream_close();
    assert(!tap_stream_open("missing.tap",TAP_SPEED_COUNT,NULL)); tap_stream_close();
    assert(!tap_stream_speed_baud((tap_speed_t)99));
    assert(tap_stream_is_tap_path("mixed.TaP") && !tap_stream_is_tap_path("x.tapx"));
    assert(!tap_stream_is_tap_path(NULL));
    puts("PASS TAP empty/truncated/invalid lengths and flags, bad later block, arguments and cleanup");
}
static void test_bounds(void) {
    static uint8_t data[65535]; memset(data,0xFF,sizeof(data));
    FILE *f=fopen("max.tap","wb"); assert(f); write_block(f,data,sizeof(data)); assert(!fclose(f));
    tap_stream_info_t info;
    assert(tap_stream_open("max.tap",TAP_SPEED_2500,&info));
    elapsed=0; expect_block(data,sizeof(data),3,true);
    assert(elapsed==info.total_ticks && tap_stream_is_eof());
    tap_stream_close(); assert(!remove("max.tap"));
    /* Tiny file with enough pilot/gap time to exceed a 32-bit microsecond clock. */
    uint8_t small[]={0,0}; f=fopen("long.tap","wb"); assert(f);
    for (int i=0;i<1000;++i) write_block(f,small,2);
    assert(!fclose(f)); assert(tap_stream_open("long.tap",TAP_SPEED_1400,&info));
    assert(info.block_count==1000 && info.total_ticks/16>UINT32_MAX);
    tap_stream_close(); assert(!remove("long.tap"));
    puts("PASS TAP maximum 65535-byte block and duration beyond 32-bit microseconds");
}
static void fuzz(void) {
    uint32_t rng=0xA5679B21; uint8_t bytes[64];
    for (unsigned trial=0;trial<2000;++trial) {
        size_t n=trial%65;
        for (size_t i=0;i<n;++i) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; bytes[i]=(uint8_t)rng; }
        if (n>=4 && trial%2) { bytes[0]=(uint8_t)(n-2); bytes[1]=0; bytes[2]=(trial&2) ? 0 : 255; }
        FILE *f=fopen("fuzz.tap","wb"); assert(f);
        assert(fwrite(bytes,1,n,f)==n && !fclose(f));
        if (tap_stream_open("fuzz.tap",(tap_speed_t)(trial%4),NULL)) {
            uint8_t level; uint32_t ticks; bool count; unsigned budget=100000;
            while (tap_stream_next_pulse(&level,&ticks,&count)) { assert(ticks && level<=1 && budget--); }
            assert(tap_stream_is_eof() && !tap_stream_has_error());
        } else assert(tap_stream_has_error());
        tap_stream_close();
    }
    assert(!remove("fuzz.tap")); puts("PASS TAP 2000 deterministic bounded parser fuzz cases (sanitizers depend on host)");
}
static void fixture(const char *path) {
    for (int speed=0;speed<TAP_SPEED_COUNT;++speed) {
        tap_stream_info_t info;
        printf("TAP fixture: %s @ %u Bd\n",path,(unsigned)tap_stream_speed_baud((tap_speed_t)speed));
        fflush(stdout);
        assert(tap_stream_open(path,(tap_speed_t)speed,&info));
        uint64_t total=0, edges=0; uint8_t level; uint32_t ticks; bool count;
        while (tap_stream_next_pulse(&level,&ticks,&count)) {
            assert(ticks && level<=1); total+=ticks; ++edges;
        }
        assert(tap_stream_is_eof() && !tap_stream_has_error() && total==info.total_ticks);
        printf("PASS fixture: %lu bytes, %lu blocks, %" PRIu64 " intervals, %" PRIu64 " ticks\n",
               (unsigned long)info.total_bytes,(unsigned long)info.block_count,
               edges,total);
        tap_stream_close();
    }
}
static unsigned cancel_calls;
static bool cancel_scan(void *context) {assert(context==&cancel_calls);return ++cancel_calls==2;}
static void test_cancel(void) {
 uint8_t data[600]={0};FILE *f=fopen("cancel.tap","wb");assert(f);write_block(f,data,sizeof(data));assert(!fclose(f));
 tap_stream_set_scan_control(cancel_scan,&cancel_calls);cancel_calls=0;
 assert(!tap_stream_open("cancel.tap",TAP_SPEED_1400,NULL) && tap_stream_analysis_cancelled());
 tap_stream_set_scan_control(NULL,NULL);assert(tap_stream_open("cancel.tap",TAP_SPEED_1400,NULL) && !tap_stream_analysis_cancelled());
 tap_stream_set_scan_control(cancel_scan,&cancel_calls);cancel_calls=0;
 uint8_t level;uint32_t ticks;bool count;while(tap_stream_next_pulse(&level,&ticks,&count)){}
 assert(tap_stream_is_eof() && !cancel_calls);tap_stream_close();tap_stream_set_scan_control(NULL,NULL);assert(!remove("cancel.tap"));
 puts("PASS TAP analysis cancellation, clean retry and no UI callback during playback");
}
int main(int argc, char **argv) { test_cancel();
    test_golden(); test_bad(); test_bounds(); fuzz();
    for (int i=1;i<argc;++i) fixture(argv[i]);
    return 0;
}
