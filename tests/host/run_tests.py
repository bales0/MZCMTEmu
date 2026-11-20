"""Build small host tests with production sources, no ESP-IDF dependencies.
Usage: python tests/host/run_tests.py [--cc /path/to/gcc] [--sanitize]
Hardware and RTOS are mocked; pulse/parser/transport code is linked separately.
All generated files stay in isolated .tmp directories.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--suite',action='append',help='Run only named suite(s), repeatable')
p.add_argument('--qd-fixtures', type=Path, action='append', default=[], help='Read-only QuickDisk fixture directory, repeatable')
p.add_argument('--tap-fixtures', type=Path, help='Optional read-only directory of TAP files')
args = p.parse_args()
fixtures = []
if args.tap_fixtures:
    if not args.tap_fixtures.is_dir():
        p.error('--tap-fixtures must be a directory')
    fixtures = sorted(str(f.resolve()) for f in args.tap_fixtures.iterdir()
                      if f.is_file() and f.suffix.lower() == '.tap')
    if not fixtures:
        p.error('--tap-fixtures directory contains no TAP files')
cc = shutil.which(args.cc)
if not cc:
    p.error('C compiler not found; pass --cc with its full path')
flags = ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror']
if os.name!='nt':
    flags += ['-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
(ROOT / '.tmp').mkdir(exist_ok=True)
SUITES = {
    'test_ul_z80': ['tests/host/test_ul_z80.c','src/play/mzf_loader.c','src/play/mz700_fast3.c','src/formats/mz_loader_profiles.c'],
    'test_menu': ['tests/host/test_menu.c'],
    'test_preflight_codecs': ['tests/host/test_preflight_codecs.c','src/audio/edge_stream.c','src/audio/edge_writer.c','src/audio/sharp_stream.c','src/formats/quickdisk.c','src/formats/sidecar.c','src/formats/file_format.c','src/formats/mz_tape_profiles.c','src/play/mzf_loader.c','src/play/mz700_fast3.c'],
    'test_edge_stream': ['tests/host/test_edge_stream.c','src/audio/edge_stream.c'],
    'test_ui_policy': ['tests/host/test_ui_policy.c','src/ui/policy.c'],
    'test_program_metadata': ['src/audio/edge_writer.c','src/audio/edge_stream.c','src/audio/wav_writer.c','src/audio/wav_reader.c','src/audio/wav_pulse_stream.c','tests/host/test_program_metadata.c','src/formats/program_metadata.c','src/formats/tape_metadata.c','src/record/record_autoname.c','src/formats/mz_tape_decoder.c','src/formats/mz_loader_profiles.c','src/formats/mz_title.c','src/audio/tap_stream.c'],
    'test_capture_pcnt': ['tests/host/test_capture_mcpwm.c','src/audio/edge_capture.c','src/audio/edge_guard.c'],
    'test_oled_ui': ['tests/host/test_oled_ui.c','src/ui/ui.c','src/ui/view.c','src/ui/policy.c','src/drivers/oled.c','src/formats/program_metadata.c','src/formats/mz_title.c','src/formats/mz_loader_profiles.c','src/formats/mz_tape_profiles.c'],
    'test_buttons': ['tests/host/test_buttons.c','src/drivers/buttons.c'],
    'test_sdcard': ['tests/host/test_sdcard.c','src/drivers/sdcard.c'],
    'test_storage_metrics': ['tests/host/test_storage.c','src/storage/fs.c'],
    'test_storage': ['tests/host/test_storage.c','src/storage/fs.c'],
    'test_player_rmt': ['tests/host/test_raw_rmt.c','src/audio/raw_pulse_rmt.c','src/audio/player.c',
        'src/audio/mzf_stream.c','src/audio/sharp_stream.c','src/audio/tap_stream.c',
        'src/audio/wav_reader.c','src/audio/wav_pulse_stream.c','src/audio/wav_writer.c','src/audio/edge_stream.c','src/audio/edge_writer.c',
        'src/formats/file_format.c','src/formats/quickdisk.c','src/formats/mz_title.c','src/formats/sidecar.c',
        'src/formats/mz_tape_profiles.c','src/formats/mz_loader_profiles.c','src/play/mzf_loader.c','src/play/mz700_fast3.c'],
    'test_raw_rmt': ['tests/host/test_raw_rmt.c','src/audio/raw_pulse_rmt.c'],
    'test_loader_choice': ['tests/host/test_loader_choice.c'],
    'test_capture_mcpwm': ['tests/host/test_capture_mcpwm.c','src/audio/edge_capture.c','src/audio/edge_guard.c'],
    'test_writer_io': ['tests/host/test_writer_io.c','tests/host/io_fail.c','src/audio/wav_writer.c',
        'src/audio/edge_writer.c','src/audio/mzf_writer.c','src/formats/mz_tape_decoder.c',
        'src/formats/mz_loader_profiles.c','src/formats/sidecar.c','src/formats/file_format.c'],
    'test_file_list': ['tests/host/test_file_list.c','src/file_list.c'],
    'test_autoname': ['src/audio/edge_stream.c','src/audio/edge_writer.c','tests/host/test_autoname.c','src/record/record_autoname.c','src/record/record_rename.c',
        'src/formats/mz_title.c','src/formats/mz_tape_decoder.c','src/formats/mz_loader_profiles.c'],
    'test_mzf_writer': ['tests/host/test_mzf_writer.c','src/audio/mzf_writer.c','src/formats/mz_tape_decoder.c','src/formats/mz_loader_profiles.c'],
    'test_sidecar_faults': ['tests/host/test_sidecar_faults.c','src/formats/sidecar.c','src/formats/file_format.c'],
    'test_sidecar_editor': ['tests/host/test_sidecar_editor.c','src/formats/sidecar.c','src/formats/sidecar_edit.c','src/formats/file_format.c','src/ui/sidecar_editor.c','src/ui/policy.c','src/drivers/oled.c'],
    'test_quickdisk': ['tests/host/test_quickdisk.c','src/formats/quickdisk.c'],
    'test_route_switch': ['tests/host/test_route_switch.c','src/app/route_switch.c'],
    'test_settings': ['tests/host/test_settings.c','src/config/settings_codec.c','src/config/nvs_settings.c'],
    'test_player_sharp': ['tests/host/test_player_sharp.c','src/audio/player.c','src/audio/mzf_stream.c',
        'src/audio/sharp_stream.c','src/formats/quickdisk.c','src/formats/mz_title.c','src/audio/raw_pulse.c','src/audio/tap_stream.c','src/audio/wav_reader.c','src/audio/wav_pulse_stream.c','src/audio/edge_stream.c',
        'src/formats/file_format.c','src/formats/mz_tape_profiles.c','src/formats/mz_loader_profiles.c',
        'src/formats/sidecar.c','src/play/mzf_loader.c','src/play/mz700_fast3.c'],
    'test_sharp_stream': ['tests/host/test_sharp_stream.c','src/audio/sharp_stream.c','src/formats/quickdisk.c','src/formats/mz_title.c',
        'src/formats/file_format.c','src/formats/mz_tape_profiles.c','src/formats/mz_loader_profiles.c',
        'src/formats/sidecar.c','src/play/mzf_loader.c','src/play/mz700_fast3.c'],
    'test_recorder': ['tests/host/test_recorder.c', 'src/audio/recorder.c', 'src/audio/mzf_writer.c',
                      'src/formats/mz_tape_decoder.c','src/formats/mz_loader_profiles.c',
                      'src/audio/edge_writer.c', 'src/audio/edge_stream.c', 'src/audio/wav_writer.c',
                      'src/formats/mz_title.c','src/record/record_autoname.c','src/record/record_rename.c',
                      'src/formats/sidecar.c','src/formats/file_format.c'],
    'test_tap_stream': ['tests/host/test_tap_stream.c', 'src/audio/tap_stream.c'],
    'test_player_tap': ['tests/host/test_player_tap.c', 'src/audio/player.c',
                        'src/audio/tap_stream.c', 'src/audio/raw_pulse.c',
                        'src/audio/wav_reader.c','src/audio/wav_pulse_stream.c', 'src/audio/edge_stream.c', 'src/file_list.c',
                        'src/formats/sidecar.c','src/formats/file_format.c'],
}
env = dict(os.environ)
# MinGW runtime DLLs are needed by the compiler as well as test executables.
env['PATH'] = str(Path(cc).parent) + os.pathsep + env.get('PATH', '')
print(subprocess.check_output([cc, '--version'], text=True, env=env).splitlines()[0], flush=True)
for name, sources in SUITES.items():
    if args.suite and name not in args.suite:continue
    print("RUN "+name,flush=True)
    with tempfile.TemporaryDirectory(prefix=name + '-', dir=ROOT / '.tmp') as tmp:
        exe = Path(tmp) / (name + ('.exe' if os.name == 'nt' else ''))
        suite_flags = ['-DHOST_TRACK_HEAP'] if name=='test_file_list' else []
        if name=='test_oled_ui':
            suite_flags += ['-DUI_RENDER_DIR="'+(ROOT/'docs/validation/ui').as_posix()+'"']
        if name=='test_capture_pcnt':
            suite_flags += ['-DCONFIG_MZ_RX_PCNT_GUARD=1']
        if name=='test_recorder':
            suite_flags += ['-DCMT_STORAGE_ASYNC_TEST']
        if name=='test_edge_stream':
            suite_flags += ['-DCMT_EDGE_DIAGNOSTICS=1']
        if name=='test_sidecar_editor':
            suite_flags += ['-DSIDECAR_TX_TEST','-DCMT_STORAGE_ASYNC_TEST','-DSC_RENDER_DIR="'+(ROOT/'docs/validation/ui').as_posix()+'"']
        if name=='test_player_rmt':
            suite_flags += ['-DRMT_PLAYER_TEST']
        if name in ('test_storage','test_storage_metrics'):
            suite_flags += ['-DESP_PLATFORM','-DCMT_STORAGE_TEST']
        if name=='test_storage_metrics':
            suite_flags += ['-DCMT_STORAGE_DIAGNOSTICS=1']
        if name=='test_writer_io':
            suite_flags += ['-Dfwrite=test_fwrite','-Dfputc=test_fputc','-Dfseek=test_fseek',
                            '-Dfflush=test_fflush','-Dfclose=test_fclose']
        sources=list(sources)
        if any(s in sources for s in ['src/audio/tap_stream.c','src/audio/sharp_stream.c','src/record/record_autoname.c','src/audio/mzf_writer.c']):
            for dependency in ['src/formats/program_metadata.c','src/formats/mz_title.c','src/formats/mz_loader_profiles.c']:
                if dependency not in sources:sources.append(dependency)
        if 'src/audio/player.c' in sources:
            for dependency in ['src/formats/mz_tape_profiles.c','src/formats/tape_metadata.c','src/record/record_autoname.c','src/formats/mz_tape_decoder.c']:
                if dependency not in sources:sources.append(dependency)
        if "src/audio/edge_stream.c" in sources:
            for dependency in ["src/storage/fs.c","src/formats/preflight_tape_metadata.c","src/formats/preflight_mz_tape_decoder.c","src/record/preflight_record_autoname.c","src/formats/program_metadata.c","src/formats/mz_loader_profiles.c","src/formats/mz_title.c"]:
                if dependency not in sources:sources.append(dependency)
        cmd = [cc, *flags, *suite_flags, '-DRECORDINGS_DIR="."', '-I', str(ROOT / 'tests/host/stubs'),
               '-I', str(ROOT / 'src'), *[str(ROOT / s) for s in sources], '-o', str(exe)]
        subprocess.run(cmd, cwd=tmp, env=env, check=True)
        extra = [str(f.resolve()) for d in args.qd_fixtures for f in sorted(d.iterdir()) if f.suffix.lower() in ('.qd','.qdf','.mzq')] if name=='test_quickdisk' else fixtures if name == 'test_tap_stream' else []
        for launch_attempt in range(4):
            try:
                subprocess.run([str(exe), *extra], cwd=tmp, env=env, check=True)
                break
            except PermissionError as error:
                if getattr(error,'winerror',None)!=5 or launch_attempt==3:
                    raise
                # Windows AV may briefly scan a just-linked executable.
                time.sleep(0.25)
