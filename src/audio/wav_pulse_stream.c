#include "audio/wav_pulse_stream.h"
#include "audio/wav_reader.h"
#include <stddef.h>

#define BUFFER_SAMPLES 256U
static uint8_t buffer[BUFFER_SAMPLES],hysteresis,run_level;
static size_t position,available;
static uint32_t rate,run_length;
static uint64_t samples,run_start,queued_us;
static bool eof,error;
bool wav_pulse_stream_begin(uint32_t sample_rate) {
    rate=sample_rate;position=available=0;hysteresis=run_level=0;
    run_length=0;samples=run_start=queued_us=0;eof=error=false;
    /* Every single-sample interval must remain representable at the 1 MHz HAL. */
    if(!rate || rate>1000000) { error=true;return false; }
    return true;
}
static uint64_t time_us(uint64_t index) { return wav_pulse_time(index,rate,1000000); }
static int next_sample(void) {
    if(position==available) {
        available=wav_reader_read(buffer,sizeof(buffer));position=0;
        if(!available) { error=wav_reader_has_error() || !wav_reader_is_eof();return -1; }
    }
    return buffer[position++];
}
static bool emit(uint64_t end,uint8_t *level,uint32_t *duration,bool exact) {
    uint64_t start_us=time_us(run_start),end_us=time_us(end);
    *level=run_level;
    *duration=exact ? (uint32_t)(wav_pulse_time(end,rate,16000000)-wav_pulse_time(run_start,rate,16000000)):(uint32_t)(end_us-start_us);
    queued_us=end_us;
    return *duration!=0;
}
static bool next_interval(uint8_t *level,uint32_t *duration,bool exact) {
    if(!level || !duration || eof || error || !rate) return false;
    for(;;) {
        /* Bound foreground read work even for hours of silence. Adjacent
         * equal-level intervals do not introduce an artificial GPIO edge. */
        if(run_length==BUFFER_SAMPLES) {
            bool ok=emit(samples,level,duration,exact);run_length=0;run_start=samples;return ok;
        }
        int value=next_sample();
        if(value<0) {
            eof=!error;
            if(error || !run_length) return false;
            bool ok=emit(samples,level,duration,exact);run_length=0;return ok;
        }
        hysteresis=wav_pulse_hysteresis(hysteresis,(uint8_t)value);
        if(run_length && hysteresis!=run_level) {
            bool ok=emit(samples,level,duration,exact);
            run_level=hysteresis;run_start=samples++;run_length=1;return ok;
        }
        if(!run_length) { run_level=hysteresis;run_start=samples; }
        ++run_length;++samples;
    }
}
bool wav_pulse_stream_eof(void) { return eof; }
bool wav_pulse_stream_error(void) { return error; }
uint64_t wav_pulse_stream_samples(void) { return samples; }
uint64_t wav_pulse_stream_queued_us(void) { return queued_us; }

bool wav_pulse_stream_next(uint8_t *level,uint32_t *duration) {return next_interval(level,duration,false);}
bool wav_pulse_stream_next_ticks16(uint8_t *level,uint32_t *ticks) {return next_interval(level,ticks,true);}
