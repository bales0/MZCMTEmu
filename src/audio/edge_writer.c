#include "storage/fs.h"
#include "audio/edge_writer.h"
#include "storage/record_file.h"

#include <stdio.h>

#define EDGE_SIGNAL_THRESHOLD 128U
#define EDGE_MAX_UNITS 127U

static cmt_file_t *s_fp = NULL;
static char s_path[256];
static bool s_open = false;
static bool s_error = false;
static bool s_level_known = false;
static uint8_t s_level = 0;
static uint8_t s_units_in_block = 0;
static bool s_placeholder_emitted = false;
static long s_placeholder_offset = 0;

static bool close_interval(void);

static bool emit_slot(uint8_t level, uint8_t units)
{
    int8_t slot;

    if (!s_open || s_fp == NULL || units == 0 || units > EDGE_MAX_UNITS) {
        return false;
    }

    slot = level ? (int8_t)units : (int8_t)(-(int16_t)units);
    return (cmt_fputc((uint8_t)slot, s_fp) != EOF);
}

static bool emit_extension(void)
{
    if (!s_open || s_fp == NULL) {
        return false;
    }

    return (cmt_fputc(0, s_fp) != EOF);
}

static bool emit_placeholder(void)
{
    long pos;

    if (!s_open || s_fp == NULL) {
        return false;
    }

    pos = cmt_ftell(s_fp);
    if (pos < 0) {
        return false;
    }

    if (!emit_slot(s_level, 1U)) {
        return false;
    }

    s_placeholder_offset = pos;
    s_placeholder_emitted = true;
    return true;
}

static bool patch_placeholder(uint8_t units)
{
    long append_pos;

    if (!s_open || s_fp == NULL ||
        !s_placeholder_emitted ||
        units == 0 || units > EDGE_MAX_UNITS) {
        return false;
    }

    append_pos = cmt_ftell(s_fp);
    if (append_pos < 0) {
        return false;
    }

    if (cmt_fseek(s_fp, s_placeholder_offset, SEEK_SET) != 0 ||
        !emit_slot(s_level, units) ||
        cmt_fseek(s_fp, append_pos, SEEK_SET) != 0) {
        return false;
    }

    return true;
}

static bool accept_interval_unit(void)
{
    s_units_in_block++;

    if (s_units_in_block < EDGE_MAX_UNITS) {
        return true;
    }

    if (!s_placeholder_emitted) {
        if (!emit_placeholder()) {
            return false;
        }
    } else if (!emit_extension()) {
        return false;
    }

    s_units_in_block = 0;
    return true;
}

static bool accept_interval_units(uint8_t level, uint32_t units)
{
    level = level ? 1U : 0U;

    if (units == 0) {
        return true;
    }

    if (!s_level_known) {
        s_level_known = true;
        s_level = level;
    } else if (level != s_level) {
        if (!close_interval()) {
            return false;
        }
        s_level_known = true;
        s_level = level;
    }

    while (units > 0) {
        uint32_t room = EDGE_MAX_UNITS - s_units_in_block;
        uint32_t chunk = (units < room) ? units : room;

        s_units_in_block = (uint8_t)(s_units_in_block + chunk);
        units -= chunk;

        if (s_units_in_block == EDGE_MAX_UNITS) {
            if (!s_placeholder_emitted) {
                if (!emit_placeholder()) {
                    return false;
                }
            } else if (!emit_extension()) {
                return false;
            }

            s_units_in_block = 0;
        }
    }

    return true;
}

static void reset_interval_state(void)
{
    s_level_known = false;
    s_level = 0;
    s_units_in_block = 0;
    s_placeholder_emitted = false;
    s_placeholder_offset = 0;
}

static bool close_interval(void)
{
    if (!s_level_known) {
        reset_interval_state();
        return true;
    }

    if (!s_placeholder_emitted) {
        if (s_units_in_block != 0 &&
            !emit_slot(s_level, s_units_in_block)) {
            return false;
        }
    } else {
        uint8_t remainder = (s_units_in_block == 0) ?
            EDGE_MAX_UNITS : s_units_in_block;

        if (!patch_placeholder(remainder)) {
            return false;
        }

        if (s_units_in_block != 0 && !emit_extension()) {
            return false;
        }
    }

    reset_interval_state();
    return true;
}

static bool accept_level_unit(uint8_t level)
{
    if (!s_level_known) {
        s_level_known = true;
        s_level = level ? 1U : 0U;
        return accept_interval_unit();
    }

    if ((level ? 1U : 0U) == s_level) {
        return accept_interval_unit();
    }

    if (!close_interval()) {
        return false;
    }

    s_level_known = true;
    s_level = level ? 1U : 0U;
    return accept_interval_unit();
}

bool edge_writer_open(const char *path, uint32_t sample_rate)
{
    (void)sample_rate;

    edge_writer_abort_delete();

    if (path == NULL) {
        return false;
    }

    s_fp = record_file_create(path,sizeof(s_path));
    if (s_fp == NULL) {
        return false;
    }

    snprintf(s_path, sizeof(s_path), "%s", path);
    s_open = true;
    s_error = false;
    s_level_known = false;
    s_level = 0;
    s_units_in_block = 0;
    s_placeholder_emitted = false;
    s_placeholder_offset = 0;
    return true;
}

size_t edge_writer_write_samples(const uint8_t *samples, size_t len)
{
    size_t processed = 0;

    if (!s_open || s_fp == NULL || samples == NULL || len == 0 || s_error) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t level = (samples[i] >= EDGE_SIGNAL_THRESHOLD) ? 1U : 0U;

        if (!accept_level_unit(level)) {
            s_error = true;
            break;
        }
        processed++;
    }

    return processed;
}

bool edge_writer_write_interval_units(uint8_t level, uint32_t units)
{
    if (!s_open || s_fp == NULL || s_error) {
        return false;
    }

    if (!accept_interval_units(level, units)) {
        s_error = true;
        return false;
    }

    return true;
}

bool edge_writer_finalize(void)
{
    if(!s_open || !s_fp) return false;
    bool ok=!s_error && close_interval() && !cmt_fflush(s_fp);
    if(cmt_fclose(s_fp))ok=false;
    s_fp=NULL;s_open=false;
    if(!ok)cmt_remove(s_path);
    s_path[0]=0;reset_interval_state();
    return ok;
}

void edge_writer_abort_delete(void)
{
    if (s_fp != NULL) {
        cmt_fclose(s_fp);
        s_fp = NULL;
    }

    if (s_open && s_path[0] != '\0') {
        cmt_remove(s_path);
    }

    s_open = false;
    s_error = false;
    reset_interval_state();
    s_path[0] = '\0';
}
