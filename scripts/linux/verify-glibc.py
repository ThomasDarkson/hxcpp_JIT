#!/usr/bin/env python3
from pathlib import Path
import os,re,subprocess,sys
os.environ['LC_ALL']='C'
root=Path(sys.argv[1])
def readelf(*args):
 r=subprocess.run(['readelf',*map(str,args)],text=True,capture_output=True)
 if r.returncode or 'Error:' in r.stderr:raise SystemExit(r.stderr)
 return r.stdout
count=0
for p in root.rglob('*'):
 if not p.is_file():continue
 with p.open('rb') as f:
  if f.read(4)!=b'\x7fELF':continue
 count+=1
 h=readelf('-h',p)
 if 'ELF64' not in h or 'Advanced Micro Devices X86-64' not in h:raise SystemExit(f'{p}: expected x86_64 ELF')
 versions=set()
 for line in readelf('--dyn-syms','--wide',p).splitlines():
  if ' UND ' not in line:continue
  versions.update((int(a),int(b)) for a,b in re.findall(r'@GLIBC_(\d+)\.(\d+)',line))
  if '@GLIBC_PRIVATE' in line or '@GLIBC_ABI_' in line:raise SystemExit(f'{p}: private/new glibc ABI required')
 if any(v>(2,28) for v in versions):raise SystemExit(f'{p}: requires glibc {max(versions)}, exceeds 2.28')
 needed=set(re.findall(r'\(NEEDED\).*?\[(.*?)\]',readelf('-d',p)))
 allowed={'libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','librt.so.1','libutil.so.1','ld-linux-x86-64.so.2'}
 missing=needed-allowed-{f.name for f in root.iterdir() if f.is_file()}
 if missing:raise SystemExit(f'{p}: unbundled dependencies {sorted(missing)}')
 print(f'PASS {p}: maximum glibc {max(versions,default=(0,0))}; dependencies {sorted(needed)}')
if not count:raise SystemExit('No ELF binaries found')
for pattern in ('libc.so*','libc-*.so','ld-linux*','ld-*.so'):
 if list(root.rglob(pattern)):raise SystemExit('Do not bundle glibc or its loader')
