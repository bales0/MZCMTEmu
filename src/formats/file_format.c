/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#include "file_format.h"
#include <string.h>
#include <strings.h>
bool file_format_is_sharp_tape(file_format_t f) { return f==FILE_FORMAT_MZF || f==FILE_FORMAT_MZT || f==FILE_FORMAT_M12 || f==FILE_FORMAT_MZQ;
    }
bool file_format_is_record_container(file_format_t f) { return f==FILE_FORMAT_MZT || f==FILE_FORMAT_MZQ;
    }
file_format_t file_format_detect_from_name(const char *path) {
    if (!path) return FILE_FORMAT_UNKNOWN;
    const char *ext=strrchr(path,'.');
    if (!ext) return FILE_FORMAT_UNKNOWN;
    static const char *const names[]={".mzf",".mzt",".m12",".lep",".l16",".wav",".tap",".mzq",".qdf",".qd"};
    static const file_format_t formats[]={FILE_FORMAT_MZF,FILE_FORMAT_MZT,FILE_FORMAT_M12,FILE_FORMAT_LEP,FILE_FORMAT_L16,FILE_FORMAT_WAV,FILE_FORMAT_TAP,FILE_FORMAT_MZQ,FILE_FORMAT_MZQ,FILE_FORMAT_MZQ};
    for (unsigned i=0;i<sizeof(formats)/sizeof(formats[0]);++i) if (!strcasecmp(ext,names[i])) return formats[i];
    return FILE_FORMAT_UNKNOWN;
}
