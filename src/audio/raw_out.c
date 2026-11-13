#include "audio/raw_out.h"
#include "audio/raw_timer.h"
#include "audio/raw_pulse.h"

/* WAV playback is a dedicated fixed-sample GPTimer backend. */
void raw_out_init(int sample_rate)
{
    raw_timer_init(sample_rate);
}

bool raw_out_start(void)
{
    return raw_timer_start();
}

void raw_out_stop(void)
{
    raw_timer_stop();
}

void raw_out_pause(void)
{
    raw_timer_pause();
}

void raw_out_resume(void)
{
    raw_timer_resume();
}

size_t raw_out_write(const uint8_t *data, size_t len)
{
    return raw_timer_write(data, len);
}

void raw_out_finish(void)
{
    raw_timer_finish();
}

size_t raw_out_free(void)
{
    return raw_timer_free();
}

size_t raw_out_level(void)
{
    return raw_timer_level();
}

bool raw_out_is_running(void)
{
    return raw_timer_is_running();
}

bool raw_out_is_underrun(void)
{
    return raw_timer_is_underrun();
}

void raw_out_clear_underrun(void)
{
    raw_timer_clear_underrun();
}

void raw_out_set_read_invert(bool invert)
{
    raw_pulse_set_read_invert(invert);
}

bool raw_out_get_read_invert(void)
{
    return raw_pulse_get_read_invert();
}

void raw_out_set_sample_output_enabled(bool enabled)
{
    raw_timer_set_output_enabled(enabled);
}
