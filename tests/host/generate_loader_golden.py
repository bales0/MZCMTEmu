"""Reproduce loader golden vectors using the pinned read-only reference algorithms.
This compiles only generator logic on the host. AVR peripheral wait code is
excluded; no generated source or build file is written to the reference repo.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--cxx', default='g++')
args = p.parse_args()
ref = args.reference.resolve()
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ref, text=True).strip()
if commit != '600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb':
    p.error('Reference HEAD differs from the required commit')
subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', 'src/play/mzf_loader.cpp',
                'src/play/mz700_fast3.cpp', 'src/formats/mz_loader_profiles.cpp'], cwd=ref, check=True)
cc = shutil.which(args.cxx)
if not cc:
    p.error('C++ compiler not found')
env = dict(os.environ)
env['PATH'] = str(Path(cc).parent) + os.pathsep + env.get('PATH', '')
COMPAT = "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#define PROGMEM\n#define PGM_P const char *\n#define PSTR(s) s\n#define pgm_read_byte(p) (*(const uint8_t *)(p))\n#define memcpy_P memcpy\nstatic inline void flash_text_copy(char *dst,size_t size,const char *src) { snprintf(dst,size,\"%s\",src); }\nstatic inline bool sdcard_file_seek(uint32_t pos) { (void)pos; return false; }\nstatic inline int16_t sdcard_file_read(void *buf,uint16_t n) { (void)buf;(void)n;return -1; }\n"
HARNESS = "#include \"compat.h\"\n#include \"mzf_loader.h\"\nint main() {\n    puts(\"/* Golden headers and Z80 loader bytes generated from unmodified protocol algorithms in SD2CMT2-Reborn 600a788. GPLv3. */\");\n    puts(\"static const struct loader_golden { int active,variant; unsigned size; const char *header_hex,*loader_hex; } loader_golden[17]={\");\n    for(int mode=0;mode<17;++mode) {\n        uint8_t h[128]={1},data[128]={0}; memcpy(h+1,\"TEST\\r\",5);\n        h[0x12]=4; h[0x14]=0; h[0x15]=0x12; h[0x16]=0; h[0x17]=0x12;\n        bool active=mzf_loader_prepare(FILE_FORMAT_MZF,(loader_mode_t)mode,h,132,128);\n        unsigned size=active ? mzf_loader_build_loader(data,128) : 0;\n        if(active && !mzf_loader_patch_loader_header(h)) return 2;\n        printf(\"{%d,%d,%u,\\\"\",active ? 1:0,(int)mzf_loader_get_variant(),size);\n        for(int i=0;i<128;++i) printf(\"%02x\",h[i]);\n        printf(\"\\\",\\\"\"); for(unsigned i=0;i<size;++i) printf(\"%02x\",data[i]); puts(\"\\\"},\");\n    }\n    puts(\"};\");\n}\n"
(ROOT / '.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ref-loader-', dir=ROOT / '.tmp') as tmp:
    tmp = Path(tmp)
    (tmp / 'compat.h').write_text(COMPAT, encoding='utf8')
    (tmp / 'main.cpp').write_text(HARNESS, encoding='utf8')
    for name in ['mzf_loader', 'mz700_fast3', 'mz_loader_profiles']:
        folder = 'formats' if name == 'mz_loader_profiles' else 'play'
        source = (ref / 'src' / folder / (name + '.cpp')).read_text(encoding='utf8')
        if name == 'mzf_loader':
            source = source[:source.index('bool mzf_loader_begin(void)')]
            start = source.index('static inline uint8_t write_level')
            end = source.index('void mzf_loader_reset', start)
            source = source[:start] + source[end:]
        source = re.sub(r'^#include[^\n]+\n', '', source, flags=re.M)
        headers = '#include "compat.h"\n#include "' + name + '.h"\n'
        if name == 'mzf_loader':
            headers += '#include "mz_loader_profiles.h"\n#include "mz700_fast3.h"\n'
        (tmp / (name + '.cpp')).write_text(headers + source, encoding='utf8')
    exe = tmp / ('golden.exe' if os.name == 'nt' else 'golden')
    subprocess.run([cc, '-std=c++11', '-O2', '-I', str(ref / 'src/play'), '-I', str(ref / 'src/formats'),
                    *[str(tmp / (n + '.cpp')) for n in ['main', 'mzf_loader', 'mz700_fast3', 'mz_loader_profiles']],
                    '-o', str(exe)], env=env, check=True)
    result = subprocess.check_output([str(exe)], env=env, text=True)
    (ROOT / 'tests/host/fixtures/loader_golden.h').write_text(result, encoding='utf8')
    high_source = HARNESS.replace('h[0x14]=0; h[0x15]=0x12; h[0x16]=0; h[0x17]=0x12;',
                                  'h[0x14]=0x10; h[0x15]=0x11; h[0x16]=0x10; h[0x17]=0x11;')
    if high_source == HARNESS:
        raise RuntimeError('HIGH-address harness substitution failed')
    (tmp / 'main.cpp').write_text(high_source, encoding='utf8')
    subprocess.run([cc, '-std=c++11', '-O2', '-I', str(ref / 'src/play'), '-I', str(ref / 'src/formats'),
                    *[str(tmp / (n + '.cpp')) for n in ['main', 'mzf_loader', 'mz700_fast3', 'mz_loader_profiles']],
                    '-o', str(exe)], env=env, check=True)
    result = subprocess.check_output([str(exe)], env=env, text=True)
    result = result.replace('static const struct loader_golden { int active,variant; unsigned size; const char *header_hex,*loader_hex; } loader_golden[17]={',
                            'static const struct loader_golden loader_high_golden[17]={')
    (ROOT / 'tests/host/fixtures/loader_high_golden.h').write_text(result, encoding='utf8')
print('Reference loader golden vectors generated')
