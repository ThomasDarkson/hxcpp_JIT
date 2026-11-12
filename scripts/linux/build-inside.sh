#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
[[ $(uname -m) == x86_64 && $(getconf GNU_LIBC_VERSION) == 'glibc 2.28' ]] || {
    echo 'This build requires x86_64 glibc 2.28. Use scripts/build-linux.sh, or run directly on AlmaLinux 8.' >&2; exit 1;
}
LLVM_VERSION=${LLVM_VERSION:-23.1.5}
JOBS=${JOBS:-2}
[[ $LLVM_VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && $JOBS =~ ^[1-9][0-9]*$ ]] || exit 2
work="$PWD/.linux-build"
mkdir -p "$work/downloads" build-linux
python=/opt/python/cp311-cp311/bin/python3
[[ -x $python ]] || python=$(command -v python3)
"$python" -c 'import sys; assert sys.version_info >= (3,9), "Python 3.9+ required"'
if [[ ! -x $work/tools/bin/python3 ]]; then "$python" -m venv "$work/tools"; fi
"$work/tools/bin/pip" install --no-cache-dir --disable-pip-version-check cmake==3.31.6 ninja==1.11.1.4
export PATH="$work/tools/bin:$PATH"
export CC=${CC:-gcc} CXX=${CXX:-g++}
archive="$work/downloads/llvm-project-$LLVM_VERSION.src.tar.xz"
source_dir="$work/llvm-project-$LLVM_VERSION.src"
if [[ ! -d $source_dir ]]; then
    if [[ ! -f $archive ]]; then
        curl --fail --location --retry 3 \
            "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/llvm-project-$LLVM_VERSION.src.tar.xz" -o "$archive.partial"
        mv "$archive.partial" "$archive"
    fi
    tar --no-same-owner -xf "$archive" -C "$work"
fi
llvm_build="$work/llvm-$LLVM_VERSION"
cmake -S "$source_dir/llvm" -B "$llvm_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-march=x86-64 -mtune=generic -fPIC' \
    -DCMAKE_CXX_FLAGS='-march=x86-64 -mtune=generic -fPIC' \
    -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_OBJC_REWRITER=OFF \
    -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_ASSERTIONS=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_CURL=OFF \
    -DLLVM_PARALLEL_LINK_JOBS=1 -DBUILD_SHARED_LIBS=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF
cmake --build "$llvm_build" --parallel "$JOBS" --target clangInterpreter clang-resource-headers \
    LLVMX86CodeGen LLVMX86AsmParser LLVMBitReader LLVMBitWriter LLVMTargetParser
resources="$llvm_build/lib/clang/${LLVM_VERSION%%.*}"
native_build="$work/native-$LLVM_VERSION"
cmake -S . -B "$native_build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS='-march=x86-64 -mtune=generic -fPIC' -DCMAKE_CXX_FLAGS='-march=x86-64 -mtune=generic -fPIC' \
    -DHXCPP_JIT_MINGW=OFF -DHXCPP_JIT_STATIC_CXX_RUNTIME=ON \
    -DHXCPP_JIT_COMPILER_RT_SOURCE="$source_dir/compiler-rt" -DCLANG_RESOURCE_DIR="$resources" \
    -DLLVM_DIR="$llvm_build/lib/cmake/llvm" -DClang_DIR="$llvm_build/lib/cmake/clang"
cmake --build "$native_build" --parallel "$JOBS"
strip --strip-unneeded -o "$native_build/libhxcpp_JIT.stripped.so" "$native_build/libhxcpp_JIT.so"
cp "$native_build/libhxcpp_JIT.stripped.so" build-linux/libhxcpp_JIT.so
cp -L "$("$CXX" -print-file-name=libgcc_s.so.1)" build-linux/libgcc_s.so.1
"$python" scripts/linux/package-headers.py build-linux "$resources" "$source_dir"
"$python" scripts/linux/verify-glibc.py build-linux
echo 'Build complete: build-linux/ packaged and glibc compatibility verified. glibc is not bundled.'
