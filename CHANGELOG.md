# 0.1.0
## Changes
- Added LLVM-MinGW support and made it the default on Windows
- Added Linux x86_64 support with glibc 2.28 or newer
- Updated LLVM to 23.1.5

# 0.0.3
## Changes
- Added `Cpp.evalCachedFile`, which compiles a file once and loads it from a cache folder on later launches, and `Cpp.cacheStatus`, which says what the cache did

# 0.0.2
## Changes
- Switched to Visual Studio 2026 from 2022
- Updated hxcpp_JIT_setup to 0.0.2, so it only installs Visual Studio 2026 Build Tools
- Visual C++ v14 Redistributable runtime DLLs are now included with hxcpp_JIT

## Fixes
- `clang-resources` folder is now shipped

# 0.0.1
- This is the initial release. Uses LLVM 23.1.4.