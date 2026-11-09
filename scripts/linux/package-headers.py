#!/usr/bin/env python3
from pathlib import Path
import os,shutil,subprocess,sys
os.environ['LC_ALL']='C'
bundle,resources,source=map(Path,sys.argv[1:])
compiler=os.environ.get('CXX','g++')
sysroot=Path(os.environ.get('HXCPP_JIT_SYSROOT','/'))
r=subprocess.run([compiler,'-E','-x','c++','-','-v'],input='',text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
paths=[];collect=False
for line in r.stderr.splitlines():
 if '#include <...> search starts here:' in line:collect=True
 elif 'End of search list.' in line:collect=False
 elif collect and Path(line.strip()).is_dir():paths.append(Path(line.strip()))
cpp=next((p for p in paths if (p/'string').is_file()),None)
target=next((p for p in paths if (p/'bits/c++config.h').is_file()),None)
if cpp is None or target is None:raise SystemExit('Cannot identify GCC C++ headers')
def tree(a,b):
 if b.exists():shutil.rmtree(b)
 shutil.copytree(a,b,symlinks=False)
include=bundle/'linux/include'
if shutil.which('rpm') and sysroot==Path('/'):
 if include.exists():shutil.rmtree(include)
 include.mkdir(parents=True)
 listing=subprocess.check_output(['rpm','-ql','glibc-headers','glibc-devel','kernel-headers'],text=True)
 for filename in listing.splitlines():
  p=Path(filename)
  if p.is_file() and p.is_relative_to('/usr/include'):
   dst=include/p.relative_to('/usr/include');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
else:tree(sysroot/'usr/include',include)
tree(cpp,include/'c++');tree(target,include/'c++/target')
tree(resources/'include',bundle/'clang-resources/include')
licenses=bundle/'licenses';licenses.mkdir(exist_ok=True)
project=Path(__file__).resolve().parents[2]
for name in ('LICENSE','NOTICE','THIRD_PARTY_NOTICES.md'):shutil.copy2(project/name,licenses/('hxcpp_JIT-'+name))
for path in (project/'scripts/linux/licenses').iterdir():
 if path.is_file():shutil.copy2(path,licenses/path.name)
for name in ('llvm','clang','compiler-rt'):shutil.copy2(source/name/'LICENSE.TXT',licenses/(name.upper()+'-LICENSE.TXT'))
bases=[sysroot/'usr/share/licenses',sysroot/'usr/share/doc']
for kind in ('licenses','doc'):bases.extend(sysroot.glob(f'opt/rh/gcc-toolset-*/root/usr/share/{kind}'))
for base in bases:
 for directory in base.glob('*'):
  if directory.is_dir() and any(t in directory.name for t in ('glibc','libgcc','libstdc++','gcc-toolset','kernel-headers')):
   for path in directory.rglob('*'):
    if path.is_file() and any(t in path.name.upper() for t in ('COPYING','LICENSE','COPYRIGHT')):
     shutil.copy2(path,licenses/(directory.name+'-'+path.name))
