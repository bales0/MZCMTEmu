#include "audio/wav_writer.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static FILE *s_fp = NULL;
static char s_path[256];
static uint32_t s_data_bytes = 0;
static uint32_t s_sample_rate = 0;
static uint16_t s_bits_per_sample = 0;
static uint16_t s_num_channels = 0;

static void wav_writer_reset_state(void)
{
    s_fp = NULL;
    s_path[0] = '\0';
    s_data_bytes = 0;
    s_sample_rate = 0;
    s_bits_per_sample = 0;
    s_num_channels = 0;
}

static bool write_u16_le(FILE *fp, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    return (fwrite(b, 1, 2, fp) == 2);
}

static bool write_u32_le(FILE *fp, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    b[2] = (uint8_t)((v >> 16) & 0xFFU);
    b[3] = (uint8_t)((v >> 24) & 0xFFU);
    return (fwrite(b, 1, 4, fp) == 4);
}

static bool write_header_placeholder(FILE *fp,
                                     uint32_t sample_rate,
                                     uint16_t bits_per_sample,
                                     uint16_t num_channels)
{
    uint32_t byte_rate = sample_rate * (uint32_t)num_channels * ((uint32_t)bits_per_sample / 8U);
    uint16_t block_align = (uint16_t)(num_channels * (bits_per_sample / 8U));

    if (fwrite("RIFF", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 36U)) return false; // placeholder
    if (fwrite("WAVE", 1, 4, fp) != 4) return false;

    if (fwrite("fmt ", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 16U)) return false;      // PCM fmt chunk size
    if (!write_u16_le(fp, 1U)) return false;       // PCM
    if (!write_u16_le(fp, num_channels)) return false;
    if (!write_u32_le(fp, sample_rate)) return false;
    if (!write_u32_le(fp, byte_rate)) return false;
    if (!write_u16_le(fp, block_align)) return false;
    if (!write_u16_le(fp, bits_per_sample)) return false;

    if (fwrite("data", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 0U)) return false;       // placeholder data size

    return true;
}

bool wav_writer_open(const char *path, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    wav_writer_abort_delete();

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return false;
    }

    if (!write_header_placeholder(fp, sample_rate, bits_per_sample, num_channels)) {
        fclose(fp);
        return false;
    }

    s_fp = fp;
    snprintf(s_path, sizeof(s_path), "%s", path);
    s_data_bytes = 0;
    s_sample_rate = sample_rate;
    s_bits_per_sample = bits_per_sample;
    s_num_channels = num_channels;
    return true;
}

size_t wav_writer_write(const uint8_t *data, size_t len)
{
    if (!s_fp || !data || len == 0) {
        return 0;
    }

    size_t n = fwrite(data, 1, len, s_fp);
    s_data_bytes += (uint32_t)n;
    return n;
}

bool wav_writer_finalize(void)
{
    if (!s_fp) {
        return false;
    }

    uint32_t riff_size = 36U + s_data_bytes;

    // patch RIFF size
    if (fseek(s_fp, 4L, SEEK_SET) != 0) {
        wav_writer_abort_delete();
        return false;
    }
    if (!write_u32_le(s_fp, riff_size)) {
        wav_writer_abort_delete();
        return false;
    }

    // patch data size
    if (fseek(s_fp, 40L, SEEK_SET) != 0) {
        wav_writer_abort_delete();
        return false;
    }
    if (!write_u32_le(s_fp, s_data_bytes)) {
        wav_writer_abort_delete();
        return false;
    }

    if (fflush(s_fp) != 0) {
        wav_writer_abort_delete();
        return false;
    }

    fclose(s_fp);

    // DŮLEŽITÉ:
    // soubor byl validně dokončen, takže už NESMÍ zůstat
    // v interním state jako "candidate for abort_delete"
    wav_writer_reset_state();

    return true;
}

void wav_writer_close(void)
{
    if (s_fp) {
        fclose(s_fp);
    }

    wav_writer_reset_state();
}

void wav_writer_abort_delete(void)
{
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }

    if (s_path[0] != '\0') {
        remove(s_path);
    }

    wav_writer_reset_state();
}

bool wav_writer_is_open(void)
{
    return (s_fp != NULL);
}

const char *wav_writer_get_path(void)
{
    return s_path;
}

uint32_t wav_writer_get_data_bytes(void)
{
    return s_data_bytes;
}

uint32_t wav_writer_get_sample_rate(void)
{
    return s_sample_rate;
}