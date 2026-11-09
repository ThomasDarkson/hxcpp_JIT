[CmdletBinding()]
param(
    [string]$LLVMSource = (Join-Path $PSScriptRoot '../llvm-project'),
    [string]$LLVMBuild = (Join-Path $PSScriptRoot '../llvm-build'),
    [string]$Output = (Join-Path $PSScriptRoot '../build'),
    [string]$RedistDir = '',
    [ValidateRange(1, 256)][int]$Jobs = 2,
    [switch]$SkipLLVM
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Full-Path([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}
function Assert-Separate([string]$First, [string]$Second) {
    $a = $First.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $b = $Second.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($a.StartsWith($b, [StringComparison]::OrdinalIgnoreCase) -or
        $b.StartsWith($a, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Use separate, non-nested directories: $First and $Second"
    }
}
function Read-Cache([string]$Directory, [string]$Name) {
    $cache = Join-Path $Directory 'CMakeCache.txt'
    if (!(Test-Path -LiteralPath $cache -PathType Leaf)) { return '' }
    $pattern = '^' + [regex]::Escape($Name) + ':[^=]+=(.*)$'
    foreach ($line in (Get-Content -LiteralPath $cache)) {
        if ($line -match $pattern) { return $Matches[1] }
    }
    return ''
}
function Assert-MsvcCache([string]$Directory) {
    $compiler = Read-Cache $Directory 'CMAKE_CXX_COMPILER'
    if ($compiler -and $compiler -notmatch '(^|[\\/])cl(\.exe)?$') {
        throw "Existing cache in $Directory uses $compiler. Choose a clean MSVC build directory."
    }
    $generator = Read-Cache $Directory 'CMAKE_GENERATOR'
    if ($generator -and $generator -ne 'Ninja') {
        throw "Existing cache in $Directory uses $generator. Choose a clean Ninja build directory."
    }
    if ((Read-Cache $Directory 'HXCPP_JIT_MINGW') -eq 'ON') {
        throw "$Directory belongs to the MinGW backend. Select a separate MSVC output directory."
    }
}
function Source-Version([string]$Directory) {
    foreach ($candidate in @('cmake/Modules/LLVMVersion.cmake', 'llvm/CMakeLists.txt')) {
        $path = Join-Path $Directory $candidate
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $text = Get-Content -Raw -LiteralPath $path
        $parts = @()
        foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
            $match = [regex]::Match($text, ('set\(LLVM_VERSION_' + $part + '\s+(\d+)\s*\)'))
            if (!$match.Success) { $parts = @(); break }
            $parts += $match.Groups[1].Value
        }
        if ($parts.Count -eq 3) { return ($parts -join '.') }
    }
    return ''
}
function Built-Version([string]$Directory) {
    $header = Join-Path $Directory 'include/llvm/Config/llvm-config.h'
    if (!(Test-Path -LiteralPath $header -PathType Leaf)) { return '' }
    $text = Get-Content -Raw -LiteralPath $header
    $match = [regex]::Match($text, '#define\s+LLVM_VERSION_STRING\s+"(\d+\.\d+\.\d+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return ''
}
function Find-Crt([string]$Directory) {
    if (!$Directory -or !(Test-Path -LiteralPath $Directory -PathType Container)) { return '' }
    if (Test-Path -LiteralPath (Join-Path $Directory 'vcruntime140.dll')) { return $Directory }
    $x64 = Join-Path $Directory 'x64'
    if (Test-Path -LiteralPath $x64 -PathType Container) {
        $candidates = @(Get-ChildItem -LiteralPath $x64 -Directory |
            Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } | Sort-Object Name -Descending)
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath (Join-Path $candidate.FullName 'vcruntime140.dll')) {
                return $candidate.FullName
            }
        }
    }
    return ''
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The MSVC backend must be built on Windows in an x64 Native Tools PowerShell.'
}
if ($env:VSCMD_ARG_TGT_ARCH -notmatch '^(x64|amd64)$') {
    throw 'Open an x64 Native Tools PowerShell first (VsDevCmd.bat -arch=x64).'
}
foreach ($program in @('cmake', 'ninja', 'cl.exe', 'dumpbin.exe')) {
    Get-Command $program -ErrorAction Stop | Out-Null
}
$root = Full-Path (Split-Path $PSScriptRoot -Parent)
$LLVMSource = Full-Path $LLVMSource
$LLVMBuild = Full-Path $LLVMBuild
$Output = Full-Path $Output
if ($LLVMBuild -eq $root -or $Output -eq $root) { throw 'Build/output directories must not be the project root.' }
Assert-Separate $LLVMBuild $Output
Assert-Separate $LLVMBuild $LLVMSource
Assert-Separate $Output $LLVMSource
if (!(Test-Path -LiteralPath (Join-Path $LLVMSource 'llvm/CMakeLists.txt')) -or
    !(Test-Path -LiteralPath (Join-Path $LLVMSource 'clang/CMakeLists.txt'))) {
    throw 'LLVMSource must contain the extracted llvm-project source (llvm/ and clang/).'
}
Assert-MsvcCache $LLVMBuild
Assert-MsvcCache $Output
$crt = if ($RedistDir) { Find-Crt (Full-Path $RedistDir) } else { Find-Crt $env:VCToolsRedistDir }
if (!$crt) { throw 'MSVC x64 CRT files not found. Set -RedistDir to the installed VC redist root or its x64 Microsoft.VC*.CRT directory.' }
$sourceVersion = Source-Version $LLVMSource
$builtVersion = Built-Version $LLVMBuild
if ($SkipLLVM) {
    $configuration = Read-Cache $LLVMBuild 'CMAKE_BUILD_TYPE'
    if ($configuration -and $configuration -ne 'Release') {
        throw 'The cached LLVM build is not Release. Rebuild LLVM without -SkipLLVM.'
    }
    foreach ($option in @('BUILD_SHARED_LIBS', 'LLVM_BUILD_LLVM_DYLIB', 'CLANG_LINK_CLANG_DYLIB')) {
        if ((Read-Cache $LLVMBuild $option) -eq 'ON') {
            throw "$option is ON in the LLVM cache. Use a static MSVC LLVM build."
        }
    }
    $runtime = Read-Cache $LLVMBuild 'CMAKE_MSVC_RUNTIME_LIBRARY'
    $crtMode = Read-Cache $LLVMBuild 'LLVM_USE_CRT_RELEASE'
    if (($runtime -and $runtime -ne 'MultiThreadedDLL') -or ($crtMode -and $crtMode -ne 'MD')) {
        throw 'The LLVM build must use the Release DLL CRT (/MD). Rebuild LLVM without -SkipLLVM.'
    }
}
if ($SkipLLVM -and $sourceVersion -and $builtVersion -and $sourceVersion -ne $builtVersion) {
    throw "LLVM source is $sourceVersion but the existing build is $builtVersion. Rebuild LLVM without -SkipLLVM in a clean LLVM build directory."
}

if (!$SkipLLVM) {
    Invoke-Checked cmake @('-S', (Join-Path $LLVMSource 'llvm'), '-B', $LLVMBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl.exe', '-DCMAKE_CXX_COMPILER=cl.exe',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DLLVM_USE_CRT_RELEASE=MD',
        '-DLLVM_ENABLE_PROJECTS=clang', '-DLLVM_TARGETS_TO_BUILD=X86', '-DLLVM_ENABLE_ASSERTIONS=OFF',
        '-DLLVM_ENABLE_EH=ON', '-DLLVM_ENABLE_RTTI=ON', '-DBUILD_SHARED_LIBS=OFF', '-DLLVM_BUILD_LLVM_DYLIB=OFF',
        '-DLLVM_INCLUDE_TESTS=OFF', '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
        '-DCLANG_ENABLE_STATIC_ANALYZER=OFF', '-DCLANG_ENABLE_OBJC_REWRITER=OFF',
        '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
        '-DLLVM_ENABLE_LIBEDIT=OFF', '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_PARALLEL_LINK_JOBS=1')
    Invoke-Checked cmake @('--build', $LLVMBuild, '--parallel', "$Jobs", '--target',
        'clangInterpreter', 'clangFrontend', 'clang-resource-headers', 'LLVMOrcJIT',
        'LLVMX86CodeGen', 'LLVMX86AsmParser', 'LLVMX86Desc', 'LLVMX86Disassembler', 'LLVMX86Info',
        'LLVMBitReader', 'LLVMBitWriter', 'LLVMTargetParser')
}
foreach ($config in @('lib/cmake/llvm/LLVMConfig.cmake', 'lib/cmake/clang/ClangConfig.cmake')) {
    if (!(Test-Path -LiteralPath (Join-Path $LLVMBuild $config))) {
        throw "Missing LLVM development build: $config. Run without -SkipLLVM first."
    }
}
$builtVersion = Built-Version $LLVMBuild
if (!$builtVersion) { throw 'Cannot identify the built LLVM version.' }
if ($sourceVersion -and $sourceVersion -ne $builtVersion) { throw 'LLVM source and build versions differ.' }
$major = $builtVersion.Split('.')[0]
$resource = Join-Path $LLVMBuild "lib/clang/$major"
if (!(Test-Path -LiteralPath (Join-Path $resource 'include/stddef.h'))) {
    throw "Clang resource headers missing at $resource. Rebuild LLVM without -SkipLLVM."
}
Invoke-Checked cmake @('-S', $root, '-B', $Output, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=cl.exe', '-DCMAKE_CXX_COMPILER=cl.exe', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
    '-DHXCPP_JIT_MINGW=OFF', "-DCLANG_RESOURCE_DIR=$resource",
    "-DLLVM_DIR=$LLVMBuild/lib/cmake/llvm", "-DClang_DIR=$LLVMBuild/lib/cmake/clang")
Invoke-Checked cmake @('--build', $Output, '--parallel', "$Jobs")
foreach ($file in @('hxcpp_JIT.dll', 'hxcpp_JIT.lib')) {
    if (!(Test-Path -LiteralPath (Join-Path $Output $file))) { throw "Build did not produce $file." }
}
$resourceDestination = Join-Path $Output 'clang-resources'
if (Test-Path -LiteralPath $resourceDestination) { Remove-Item -Recurse -Force -LiteralPath $resourceDestination }
New-Item -ItemType Directory -Force -Path $resourceDestination | Out-Null
Copy-Item -Recurse -LiteralPath (Join-Path $resource 'include') -Destination $resourceDestination
Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File | Copy-Item -Force -Destination $Output
$dependencies = (& dumpbin.exe /nologo /dependents (Join-Path $Output 'hxcpp_JIT.dll') 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect DLL dependencies.' }
foreach ($match in [regex]::Matches($dependencies, '(?im)^\s*((?:msvcp|vcruntime|concrt|vccorlib)\d+[^\s]*\.dll)\s*$')) {
    $name = $match.Groups[1].Value
    if (!(Test-Path -LiteralPath (Join-Path $Output $name))) { throw "Required MSVC runtime $name was not packaged; check -RedistDir." }
}
"MSVC x64 backend; LLVM $builtVersion; select -D hxcpp_jit_msvc in Haxe." |
    Set-Content -LiteralPath (Join-Path $Output 'msvc-backend.txt')

Write-Host "MSVC backend built at $Output (LLVM $builtVersion)."
Write-Host 'Build the Haxe application with -D hxcpp_jit_msvc'
