"""Host-only comparison with the approved baseline; no device/SD access.
Builds in an isolated .tmp directory and compares length-only baseline,
metadata scan, and RAM cache hit on identical generated 1/10 MiB files.
Times are host filesystem/CPU measurements, never ESP32 performance claims.
"""
import argparse,os,re,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='gcc')
p.add_argument('--baseline',default='3664dab50e114083c3b593bdcba2eab0fe7dc474')
a=p.parse_args()
cc=shutil.which(a.cc)
if not cc:p.error('C compiler not found')
env=dict(os.environ);env['PATH']=str(Path(cc).parent)+os.pathsep+env.get('PATH','')
old=subprocess.check_output(['git','show',a.baseline+':src/audio/edge_stream.c'],cwd=ROOT,text=True)
names=sorted(set(re.findall(r'\b(edge_stream_[a-z_]+)\s*\(',old)))
sources=['src/audio/edge_stream.c','src/storage/fs.c','src/formats/preflight_tape_metadata.c','src/formats/preflight_mz_tape_decoder.c','src/record/preflight_record_autoname.c','src/formats/program_metadata.c','src/formats/mz_loader_profiles.c','src/formats/mz_title.c']
(ROOT/'.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='edge-benchmark-',dir=ROOT/'.tmp') as folder:
    work=Path(folder);baseline=work/'baseline.c'
    baseline.write_text(''.join('#define '+n+' baseline_'+n+'\n' for n in names)+old)
    exe=work/('benchmark.exe' if os.name=='nt' else 'benchmark')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-DCMT_EDGE_DIAGNOSTICS=1']
    if os.name!='nt':flags+=['-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE']
    subprocess.run([cc,*flags,'-I'+str(ROOT/'tests/host/stubs'),'-I'+str(ROOT/'src'),str(ROOT/'tests/host/benchmark_edge_preflight.c'),str(baseline),*[str(ROOT/s) for s in sources],'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],cwd=work,check=True,env=env)
