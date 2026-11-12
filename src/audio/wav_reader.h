#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t audio_format;      // 1 = PCM
    uint16_t num_channels;      // chceme 1
    uint32_t sample_rate;       // např. 44100
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;   // chceme 8
    uint32_t data_offset;       // offset začátku audio dat v souboru
    uint32_t data_size;         // velikost audio dat v bytech
} wav_info_t;

/**
 * Otevře WAV soubor, projde RIFF/WAVE chunky, najde fmt + data a
 * připraví čtení audio dat od začátku data chunku.
 * Po úspěchu:
 * - vrátí true
 * - vyplní info
 * - interní file pointer zůstane otevřený a je nastavený na začátek audio dat
 */
bool wav_reader_open(const char *path, wav_info_t *info);

/**
 * Jen přečte hlavičku WAV a vrátí metadata bez ponechání otevřeného reader state.
 */
bool wav_reader_probe(const char *path, wav_info_t *info);

bool wav_reader_has_error(void);

/**
 * Načte audio data z data chunku.
 * Vrací počet skutečně načtených byteů.
 */
size_t wav_reader_read(uint8_t *dst, size_t max_len);

/**
 * Vrátí true, pokud už nejsou k dispozici další audio data.
 */
bool wav_reader_is_eof(void);

/**
 * Zavře aktuálně otevřený WAV soubor.
 */
void wav_reader_close(void);

#ifdef __cplusplus
}
#endif