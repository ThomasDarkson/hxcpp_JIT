# Third-party notices

hxcpp_JIT itself is licensed under the Apache License 2.0 (see `LICENSE` and `NOTICE`). Its native libraries, bundled headers and runtime libraries contain or were produced with the following third-party software. The components included depend on the selected backend: Windows LLVM-MinGW, Windows MSVC or Linux.

This file summarizes the components. Keep the full license texts, copyright notices and exceptions supplied with the corresponding bundle; this summary does not replace them. Third-party files remain under their own licenses.

## LLVM Project (LLVM and Clang)

The native library contains LLVM and Clang. The `clang-resources/` directory contains Clang resource headers.

- License: Apache License v2.0 with LLVM Exceptions
- Project: https://llvm.org
- Source: https://github.com/llvm/llvm-project
- License text: https://llvm.org/LICENSE.txt

## LLVM-MinGW

LLVM-MinGW is the default Windows backend. It allows the Haxe application to be built with MSVC while the JIT uses the bundled LLVM-MinGW headers, libraries and runtime DLLs.

- Project and releases: https://github.com/mstorsjo/llvm-mingw
- Toolchain build scripts: MIT License, as described in https://github.com/mstorsjo/llvm-mingw/blob/master/LICENSE.txt

### libc++, libc++abi, libunwind and compiler-rt

- License: Apache License v2.0 with LLVM Exceptions, subject to the additional notices in each component's license file
- Source: https://github.com/llvm/llvm-project
- libc++ license: https://github.com/llvm/llvm-project/blob/main/libcxx/LICENSE.TXT
- libc++abi license: https://github.com/llvm/llvm-project/blob/main/libcxxabi/LICENSE.TXT
- libunwind license: https://github.com/llvm/llvm-project/blob/main/libunwind/LICENSE.TXT
- compiler-rt license: https://github.com/llvm/llvm-project/blob/main/compiler-rt/LICENSE.TXT

### mingw-w64

The `mingw/` directory includes mingw-w64 Windows API and C runtime headers, import libraries and runtime code. Libraries copied from the toolchain can also include mingw-w64 support libraries such as winpthreads.

- Project: https://www.mingw-w64.org
- Source: https://github.com/mingw-w64/mingw-w64
- Main license: https://github.com/mingw-w64/mingw-w64/blob/master/COPYING
- Runtime notices: https://github.com/mingw-w64/mingw-w64/blob/master/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt

## Linux backend

The Linux x86_64 bundle contains `libhxcpp_JIT.so`, `libgcc_s.so.1`, `linux/`, `clang-resources/` and `licenses/`. Keep the complete `licenses/` directory with a redistributed Linux bundle.

### compiler-rt

The Linux native library includes compiler-rt support, including emulated thread-local storage support. It uses the LLVM license described above.

- Source: https://github.com/llvm/llvm-project/tree/main/compiler-rt
- Bundled license: `licenses/COMPILER-RT-LICENSE.TXT`

### GCC libstdc++ and libgcc

The Linux native library includes C++ runtime code from GCC libstdc++. Its C++ headers are under `linux/include/c++/`. Exception unwinding uses the separately supplied `libgcc_s.so.1`.

- License: GNU General Public License version 3, with the GCC Runtime Library Exception version 3.1 for files carrying that exception; retain the individual file notices
- Project: https://gcc.gnu.org
- Source: https://gcc.gnu.org/git.html
- libstdc++ licensing: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html
- Exception: https://www.gnu.org/licenses/gcc-exception-3.1.html

The supplied Linux bundle uses GCC Toolset 14 C++ runtime code and headers, and a libgcc unwinder from the glibc 2.28-compatible build environment. Matching license texts are included as `licenses/gcc-toolset-14-gcc-COPYING*` and `licenses/libgcc-COPYING*`, including `COPYING3` and `COPYING.RUNTIME`. Keep the full texts and the runtime exception; GCC runtime files are not licensed under hxcpp_JIT's Apache license. Retain or provide corresponding source for redistributed GCC runtime components as required by their applicable terms.

### GNU C Library headers

The `linux/include/` directory contains glibc development headers from the glibc 2.28-compatible build environment.

- License: GNU Lesser General Public License version 2.1 or later for files carrying that license, together with the additional licenses and exceptions specified in individual files
- Project and source: https://www.gnu.org/software/libc/
- Bundled notices: `licenses/glibc-COPYING`, `licenses/glibc-COPYING.LIB` and `licenses/glibc-LICENSES`

glibc shared libraries and the dynamic loader are not bundled. The application uses the glibc installed by the user's Linux distribution. The bundled development headers still retain their own copyright and license terms.

### Linux UAPI headers

The `linux/include/` directory also contains Linux kernel userspace API headers, including architecture-specific headers.

- Source: https://www.kernel.org
- License: the SPDX license and exception stated in each header; many UAPI headers use GPL-2.0 with the Linux syscall exception
- Bundled texts: `licenses/Linux-GPL-2.0` and `licenses/Linux-syscall-note`

Preserve the individual header notices and the syscall exception. Kernel header files can carry other licenses; the individual file terms remain applicable. No Linux kernel executable is bundled.

## hxcpp_JIT_setup

Applications selecting the optional Windows MSVC backend embed `hxcpp_JIT_setup.exe`. The default LLVM-MinGW backend and the Linux backend do not embed or run this setup.

- Project: https://github.com/ThomasDarkson/hxcpp_JIT_setup
- License: Apache License 2.0

## Microsoft Visual C++ Redistributable files

The Windows MSVC bundle includes Visual C++ v14 runtime DLLs. Depending on the toolset version, these include:

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

These are Microsoft's own binaries and are not covered by hxcpp_JIT's Apache License 2.0. Redistribution is governed by the Microsoft license and Distributable Code terms applicable to the Visual Studio toolset from which the files were obtained.

- Visual Studio 2026 runtime license terms: https://visualstudio.microsoft.com/license-terms/vs2026-ga-visualcpp-v14-redist-runtime/
- Redistributable download: https://aka.ms/vc14/vc_redist.x64.exe

A Haxe application built with MSVC can also need Microsoft's runtime when using the MinGW JIT backend. That application's runtime requirements are separate from the LLVM-MinGW runtime libraries.

## Other Microsoft components

The MinGW and Linux bundles do not include the MSVC Build Tools or the Windows SDK. The optional MSVC setup downloads these components directly from Microsoft if installation is needed. They are licensed to the user by Microsoft under Microsoft's own terms.

The LLVM-MinGW backend uses the Windows system libraries, including UCRT supplied by supported Windows installations. Windows API import libraries and headers supplied by mingw-w64 are covered by the mingw-w64 notices above. This does not grant redistribution rights to Microsoft's SDK or other Microsoft components.
