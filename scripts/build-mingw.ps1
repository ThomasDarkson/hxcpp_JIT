[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [string]$LLVMSource = (Join-Path $PSScriptRoot '../llvm-project'),
    [string]$LLVMBuild = (Join-Path $PSScriptRoot '../llvm-build-mingw'),
    [string]$Output = (Join-Path $PSScriptRoot '../build-mingw'),
    [int]$Jobs = 2,
    [switch]$SkipLLVM
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$root = Split-Path $PSScriptRoot -Parent
$Toolchain = (Resolve-Path -LiteralPath $Toolchain).Path
$LLVMSource = [IO.Path]::GetFullPath($LLVMSource)
$LLVMBuild = [IO.Path]::GetFullPath($LLVMBuild)
$Output = [IO.Path]::GetFullPath($Output)
if ($Jobs -lt 1) { throw 'Jobs must be positive' }
$separator = [IO.Path]::DirectorySeparatorChar
if ($Output.TrimEnd('\', '/') -eq $Toolchain.TrimEnd('\', '/') -or
    $Output.StartsWith($Toolchain.TrimEnd('\', '/') + $separator, [StringComparison]::OrdinalIgnoreCase) -or
    $Toolchain.StartsWith($Output.TrimEnd('\', '/') + $separator, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output and Toolchain must be separate, non-nested directories'
}
Get-Command cmake, ninja, lib.exe -ErrorAction Stop | Out-Null
$cc = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang.exe'
$cxx = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
foreach ($path in @($cc, $cxx, (Join-Path $Toolchain 'bin/libc++.dll'), (Join-Path $Toolchain 'bin/libunwind.dll'))) {
    if (!(Test-Path -LiteralPath $path)) { throw "Required LLVM-MinGW file missing: $path" }
}
$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $Toolchain 'bin') + ';' + $oldPath
    if (!$SkipLLVM) {
        if (!(Test-Path (Join-Path $LLVMSource 'llvm/CMakeLists.txt'))) { throw 'LLVMSource must be an LLVM source checkout' }
        Invoke-Checked cmake @('-S', (Join-Path $LLVMSource 'llvm'), '-B', $LLVMBuild, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cxx",
            '-DLLVM_ENABLE_PROJECTS=clang', '-DLLVM_TARGETS_TO_BUILD=X86', '-DLLVM_ENABLE_ASSERTIONS=OFF',
            '-DLLVM_ENABLE_RTTI=ON', '-DLLVM_ENABLE_EH=ON', '-DLLVM_INCLUDE_TESTS=OFF',
            '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_INCLUDE_DOCS=OFF',
            '-DCLANG_INCLUDE_TESTS=OFF', '-DCLANG_INCLUDE_DOCS=OFF', '-DLLVM_ENABLE_ZLIB=OFF',
            '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_PARALLEL_LINK_JOBS=1')
        Invoke-Checked cmake @('--build', $LLVMBuild, '--parallel', "$Jobs", '--target',
            'clangInterpreter', 'clangFrontend', 'clang-resource-headers', 'LLVMOrcJIT',
            'LLVMX86CodeGen', 'LLVMX86AsmParser', 'LLVMX86Desc', 'LLVMX86Info',
            'LLVMBitReader', 'LLVMBitWriter', 'LLVMTargetParser')
    }
    Invoke-Checked cmake @('-S', $root, '-B', $Output, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cxx", '-DHXCPP_JIT_MINGW=ON',
        "-DLLVM_DIR=$LLVMBuild/lib/cmake/llvm", "-DClang_DIR=$LLVMBuild/lib/cmake/clang")
    Invoke-Checked cmake @('--build', $Output, '--parallel', "$Jobs")
    Invoke-Checked lib.exe @('/nologo', '/machine:x64', "/def:$root/native/hxcpp_JIT.def", "/out:$Output/hxcpp_JIT.lib")

    $bundle = Join-Path $Output 'mingw'
    New-Item -ItemType Directory -Force -Path $bundle | Out-Null
    foreach ($name in @('include', 'lib', 'x86_64-w64-mingw32')) {
        $source = Join-Path $Toolchain $name
        if (!(Test-Path $source)) { throw "Toolchain directory missing: $source" }
        $destination = Join-Path $bundle $name
        if (Test-Path $destination) { Remove-Item -Recurse -Force -LiteralPath $destination }
        Copy-Item -Recurse -LiteralPath $source -Destination $destination
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $bundle 'bin') | Out-Null
    foreach ($dll in @('libc++.dll', 'libunwind.dll')) {
        Copy-Item -Force -LiteralPath (Join-Path $Toolchain "bin/$dll") -Destination $Output
        Copy-Item -Force -LiteralPath (Join-Path $Toolchain "bin/$dll") -Destination (Join-Path $bundle 'bin')
    }
    $share = Join-Path $Toolchain 'share'
    if (Test-Path $share) {
        $destination = Join-Path $bundle 'share'
        if (Test-Path $destination) { Remove-Item -Recurse -Force -LiteralPath $destination }
        Copy-Item -Recurse -LiteralPath $share -Destination $destination
    }
    foreach ($entry in (Get-ChildItem -LiteralPath $Toolchain | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)' })) {
        Copy-Item -Recurse -Force -LiteralPath $entry.FullName -Destination $bundle
    }
    $resourceRoots = @(Get-ChildItem -Directory (Join-Path $LLVMBuild 'lib/clang') | Where-Object { Test-Path (Join-Path $_.FullName 'include/stddef.h') })
    if ($resourceRoots.Count -ne 1) { throw 'Expected exactly one LLVM build Clang resource directory' }
    $resources = Join-Path $Output 'clang-resources'
    if (Test-Path $resources) { Remove-Item -Recurse -Force -LiteralPath $resources }
    New-Item -ItemType Directory -Force -Path $resources | Out-Null
    Copy-Item -Recurse -LiteralPath (Join-Path $resourceRoots[0].FullName 'include') -Destination $resources
} finally { $env:PATH = $oldPath }
Write-Host "MinGW backend built at $Output"
