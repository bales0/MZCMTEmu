#include "storage/fs.h"
#include "audio/wav_reader.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static cmt_file_t *s_fp = NULL;
static wav_info_t s_info;
static uint32_t s_data_remaining = 0;
static bool s_is_open = false;
static bool s_read_error = false;

static bool read_u16_le(cmt_file_t *fp, uint16_t *out)
{
    uint8_t b[2];
    if (cmt_fread(b, 1, 2, fp) != 2) {
        return false;
    }

    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool read_u32_le(cmt_file_t *fp, uint32_t *out)
{
    uint8_t b[4];
    if (cmt_fread(b, 1, 4, fp) != 4) {
        return false;
    }

    *out = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    return true;
}

static bool skip_bytes(cmt_file_t *fp, uint32_t n)
{
    return (cmt_fseek(fp, (long)n, SEEK_CUR) == 0);
}

bool wav_reader_parse(cmt_file_t *fp, wav_info_t *out)
{
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];
    bool found_fmt = false;
    bool found_data = false;
    wav_info_t tmp;

    memset(&tmp, 0, sizeof(tmp));

    if (fp == NULL || out == NULL) {
        return false;
    }
    if(cmt_fseek(fp,0,SEEK_END))return false;
    long file_size=cmt_ftell(fp);
    if(file_size<12 || cmt_fseek(fp,0,SEEK_SET))return false;

    if (cmt_fread(riff_id, 1, 4, fp) != 4) {
        return false;
    }

    if (!read_u32_le(fp, &riff_size)) {
        return false;
    }

    if (cmt_fread(wave_id, 1, 4, fp) != 4) {
        return false;
    }

    uint64_t riff_end=(uint64_t)riff_size+8U;
    if(riff_size<4U || riff_end>(uint64_t)file_size)return false;

    if (memcmp(riff_id, "RIFF", 4) != 0 || memcmp(wave_id, "WAVE", 4) != 0) {
        return false;
    }

    // Procházení chunků, dokud nenajdeme fmt a data
    while (true) {
        char chunk_id[4];
        uint32_t chunk_size;
        long chunk_pos=cmt_ftell(fp);
        if(chunk_pos<0 || (uint64_t)chunk_pos+8U>riff_end)break;

        if (cmt_fread(chunk_id, 1, 4, fp) != 4) {
            break;
        }

        if (!read_u32_le(fp, &chunk_size)) {
            break;
        }
        uint64_t chunk_end=(uint64_t)chunk_pos+8U+chunk_size;
        if(chunk_end>riff_end || chunk_size>INT32_MAX)return false;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            // Minimálně 16 byte pro PCM fmt chunk
            if (chunk_size < 16) {
                return false;
            }

            if (!read_u16_le(fp, &tmp.audio_format) ||
                !read_u16_le(fp, &tmp.num_channels) ||
                !read_u32_le(fp, &tmp.sample_rate) ||
                !read_u32_le(fp, &tmp.byte_rate) ||
                !read_u16_le(fp, &tmp.block_align) ||
                !read_u16_le(fp, &tmp.bits_per_sample)) {
                return false;
            }

            // Pokud je fmt chunk delší než 16 byte, přeskoč zbytek
            if (chunk_size > 16) {
                if (!skip_bytes(fp, chunk_size - 16)) {
                    return false;
                }
            }

            // WAV chunky jsou word-aligned
            if ((chunk_size & 1U) && chunk_end<riff_end) {
                if (!skip_bytes(fp, 1)) {
                    return false;
                }
            }

            found_fmt = true;
        }
        else if (memcmp(chunk_id, "data", 4) == 0) {
            long data_pos = cmt_ftell(fp);
            if (data_pos < 0) {
                return false;
            }

            tmp.data_offset = (uint32_t)data_pos;
            tmp.data_size = chunk_size;
            found_data = true;

            // Přeskoč data, kdyby byl ještě potřeba další chunk
            if (!skip_bytes(fp, chunk_size)) {
                return false;
            }

            if ((chunk_size & 1U) && chunk_end<riff_end) {
                if (!skip_bytes(fp, 1)) {
                    return false;
                }
            }
        }
        else {
            // Neznámý chunk přeskoč
            if (!skip_bytes(fp, chunk_size)) {
                return false;
            }

            if ((chunk_size & 1U) && chunk_end<riff_end) {
                if (!skip_bytes(fp, 1)) {
                    return false;
                }
            }
        }

        if (found_fmt && found_data) {
            break;
        }
    }

    if (!found_fmt || !found_data) {
        return false;
    }

    *out = tmp;
    return true;
}
bool wav_reader_supported(const wav_info_t *info) {
    return info && info->audio_format==1U && info->num_channels==1U &&
        info->bits_per_sample==8U && info->block_align==1U &&
        info->sample_rate && info->sample_rate<=1000000U &&
        info->byte_rate==info->sample_rate;
}

void wav_reader_close(void)
{
    if (s_fp) {
        cmt_fclose(s_fp);
        s_fp = NULL;
    }

    memset(&s_info, 0, sizeof(s_info));
    s_data_remaining = 0;
    s_is_open = false;
    s_read_error = false;
}

bool wav_reader_open(const char *path, wav_info_t *info)
{
    wav_info_t tmp;

    if (!path || !info) {
        return false;
    }

    wav_reader_close();

    cmt_file_t *fp = cmt_fopen(path, "rb");
    if (!fp) {
        return false;
    }

    if (!wav_reader_parse(fp, &tmp)) {
        cmt_fclose(fp);
        return false;
    }

    // Přesuň file pointer na začátek audio dat
    if (cmt_fseek(fp, (long)tmp.data_offset, SEEK_SET) != 0) {
        cmt_fclose(fp);
        return false;
    }

    s_fp = fp;
    s_info = tmp;
    s_data_remaining = tmp.data_size;
    s_is_open = true;
    s_read_error = false;
    *info = tmp;
    return true;
}

bool wav_reader_probe(const char *path, wav_info_t *info)
{
    wav_info_t tmp;

    if (!path || !info) {
        return false;
    }

    cmt_file_t *fp = cmt_fopen(path, "rb");
    if (!fp) {
        return false;
    }

    bool ok = wav_reader_parse(fp, &tmp);
    cmt_fclose(fp);

    if (!ok) {
        return false;
    }

    *info = tmp;
    return true;
}

size_t wav_reader_read(uint8_t *dst, size_t max_len)
{
    if (!s_is_open || !s_fp || !dst || max_len == 0) {
        return 0;
    }

    if (s_data_remaining == 0) {
        return 0;
    }

    size_t to_read = max_len;
    if ((uint32_t)to_read > s_data_remaining) {
        to_read = (size_t)s_data_remaining;
    }

    size_t n = cmt_fread(dst, 1, to_read, s_fp);
    if (n < to_read) {
        s_read_error = true;
    }

    s_data_remaining -= (uint32_t)n;
    return n;
}

bool wav_reader_is_eof(void)
{
    return (s_is_open && !s_read_error && s_data_remaining == 0);
}

// helper bez header změny: player.c si ho deklaruje forward declaration
bool wav_reader_has_error(void)
{
    return s_read_error;
}
