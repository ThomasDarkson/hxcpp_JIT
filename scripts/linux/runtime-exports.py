#!/usr/bin/env python3
from pathlib import Path
import os,re,subprocess,sys
os.environ['LC_ALL']='C'
nm,archive,output=sys.argv[1:]
r=subprocess.run([nm,'-g','-j','--defined-only',archive],text=True,capture_output=True)
if r.returncode:raise SystemExit(r.stderr)
symbols=sorted({s for s in r.stdout.splitlines() if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9.$]*',s)})
if '__cxa_throw' not in symbols:raise SystemExit('Expected a complete GCC C++ runtime archive')
Path(output).write_text('{\n global:\n jit_*;\n __emutls_get_address;\n'+''.join(f' "{s}";\n' for s in symbols)+' local: *;\n};\n')
