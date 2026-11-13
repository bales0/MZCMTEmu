#include "storage/fs.h"
#include "audio/wav_writer.h"
#include "storage/record_file.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static cmt_file_t *s_fp = NULL;
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

static bool write_u16_le(cmt_file_t *fp, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    return (cmt_fwrite(b, 1, 2, fp) == 2);
}

static bool write_u32_le(cmt_file_t *fp, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    b[2] = (uint8_t)((v >> 16) & 0xFFU);
    b[3] = (uint8_t)((v >> 24) & 0xFFU);
    return (cmt_fwrite(b, 1, 4, fp) == 4);
}

static bool write_header_placeholder(cmt_file_t *fp,
                                     uint32_t sample_rate,
                                     uint16_t bits_per_sample,
                                     uint16_t num_channels)
{
    uint32_t byte_rate = sample_rate * (uint32_t)num_channels * ((uint32_t)bits_per_sample / 8U);
    uint16_t block_align = (uint16_t)(num_channels * (bits_per_sample / 8U));

    if (cmt_fwrite("RIFF", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 36U)) return false; // placeholder
    if (cmt_fwrite("WAVE", 1, 4, fp) != 4) return false;

    if (cmt_fwrite("fmt ", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 16U)) return false;      // PCM fmt chunk size
    if (!write_u16_le(fp, 1U)) return false;       // PCM
    if (!write_u16_le(fp, num_channels)) return false;
    if (!write_u32_le(fp, sample_rate)) return false;
    if (!write_u32_le(fp, byte_rate)) return false;
    if (!write_u16_le(fp, block_align)) return false;
    if (!write_u16_le(fp, bits_per_sample)) return false;

    if (cmt_fwrite("data", 1, 4, fp) != 4) return false;
    if (!write_u32_le(fp, 0U)) return false;       // placeholder data size

    return true;
}

bool wav_writer_open(const char *path, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels)
{
    if (path == NULL || path[0] == '\0' || strlen(path)>=sizeof(s_path) ||
        !sample_rate || sample_rate>1000000 || bits_per_sample!=8 || num_channels!=1) {
        return false;
    }

    wav_writer_abort_delete();

    cmt_file_t *fp = record_file_create(path,sizeof(s_path));
    if (!fp) {
        return false;
    }

    if (!write_header_placeholder(fp, sample_rate, bits_per_sample, num_channels)) {
        cmt_fclose(fp);
        cmt_remove(path);
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

    /* FAT32 file size and RIFF fields must include the 44-byte header and
     * possible word-alignment byte without wrapping either 32-bit bound. */
    if(len>UINT32_MAX-45U-s_data_bytes) return 0;
    size_t n = cmt_fwrite(data, 1, len, s_fp);
    s_data_bytes += (uint32_t)n;
    return n;
}

bool wav_writer_finalize(void)
{
    if (!s_fp) {
        return false;
    }

    uint32_t padding=s_data_bytes&1U;
    if(padding && cmt_fputc(0,s_fp)==EOF) { wav_writer_abort_delete();return false; }
    uint32_t riff_size = 36U + s_data_bytes + padding;

    // patch RIFF size
    if (cmt_fseek(s_fp, 4L, SEEK_SET) != 0) {
        wav_writer_abort_delete();
        return false;
    }
    if (!write_u32_le(s_fp, riff_size)) {
        wav_writer_abort_delete();
        return false;
    }

    // patch data size
    if (cmt_fseek(s_fp, 40L, SEEK_SET) != 0) {
        wav_writer_abort_delete();
        return false;
    }
    if (!write_u32_le(s_fp, s_data_bytes)) {
        wav_writer_abort_delete();
        return false;
    }

    if (cmt_fflush(s_fp) != 0) {
        wav_writer_abort_delete();
        return false;
    }

    bool closed=cmt_fclose(s_fp)==0;
    s_fp=NULL;
    if(!closed) { cmt_remove(s_path);wav_writer_reset_state();return false; }

    // DŮLEŽITÉ:
    // soubor byl validně dokončen, takže už NESMÍ zůstat
    // v interním state jako "candidate for abort_delete"
    wav_writer_reset_state();

    return true;
}

void wav_writer_close(void)
{
    if (s_fp) {
        cmt_fclose(s_fp);
    }

    wav_writer_reset_state();
}

void wav_writer_abort_delete(void)
{
    if (s_fp) {
        cmt_fclose(s_fp);
        s_fp = NULL;
    }

    if (s_path[0] != '\0') {
        cmt_remove(s_path);
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