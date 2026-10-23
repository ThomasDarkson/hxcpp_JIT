# Third-party notices

hxcpp_JIT itself is licensed under the Apache License 2.0 (see `LICENSE` and `NOTICE`). The precompiled `hxcpp_JIT.dll` and `hxcpp_JIT.lib` contain or were produced with the following third-party software.

## LLVM Project (LLVM and Clang)

The precompiled `hxcpp_JIT.dll` contains parts of the LLVM Project, version 23.1.4, including Clang and its resource headers.

- License: Apache License v2.0 with LLVM Exceptions
- Project: https://llvm.org
- Source: https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.4
- License text: https://llvm.org/LICENSE.txt (also in `LICENSE.TXT` at the root of the LLVM source)

LLVM's license file also lists a few smaller third-party components that are part of LLVM. Their terms are in that same file.

The LLVM source used to build the DLL is unmodified.

## UPX

The precompiled `hxcpp_JIT.dll` is compressed with UPX.

- License: GNU GPL v2 with the UPX special exception, which allows compressing programs of any license
- Project: https://upx.github.io

UPX is not distributed with hxcpp_JIT, and compressing the DLL does not change its license.

## hxcpp_JIT_setup

Every program that uses hxcpp_JIT embeds `hxcpp_JIT_setup.exe`.

- Project: https://github.com/ThomasDarkson/hxcpp_JIT_setup
- License: Apache

## Microsoft Visual C++ Redistributable files

hxcpp_JIT redistributes the following Visual C++ v14 runtime DLLs, built as part of Visual Studio 2026:

- `vcruntime140.dll`
- `vcruntime140_1.dll`
- `vcruntime140_threads.dll`
- `msvcp140.dll`
- `msvcp140_1.dll`
- `msvcp140_2.dll`
- `msvcp140_codecvt_ids.dll`
- `msvcp140_atomic_wait.dll`
- `vccorlib140.dll`
- `concrt140.dll`

These are Microsoft's own binaries, unmodified, redistributed under the Distributable Code terms of the Microsoft Visual Studio 2026 license. They are not covered by hxcpp_JIT's Apache License 2.0,  Microsoft's license terms apply to them.

- License terms: https://visualstudio.microsoft.com/license-terms/vs2026-ga-visualcpp-v14-redist-runtime/
- Redistributable download: https://aka.ms/vc14/vc_redist.x64.exe

## Other Microsoft components

hxcpp_JIT does not contain or redistribute any other Microsoft software. `hxcpp_JIT_setup` downloads the Visual Studio Build Tools (including the MSVC build tools) and the Windows SDK directly from Microsoft, and only after the user approves the administrator permission prompt. Those components are licensed to the user by Microsoft under Microsoft's own license terms, and the setup window links to the license terms for the Build Tools and for the Windows SDK.