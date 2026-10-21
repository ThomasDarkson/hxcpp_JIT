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

## Microsoft components

hxcpp_JIT does not contain or redistribute any Microsoft software. `hxcpp_JIT_setup` downloads the Visual C++ Redistributables, MSVC build tools, Windows SDK and ATL directly from Microsoft, and only after the user presses **Install**. Those components are licensed to the user by Microsoft under Microsoft's own license terms, which the setup shows before installation.