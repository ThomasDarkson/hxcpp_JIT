[![](https://thomasdarkson.com/assets/stickers/sticker.png)](https://thomasdarkson.com)

[<img src="https://thomasdarkson.com/hxcpp_JIT/hxcpp_JIT_logo.png" style="width:60%; height:auto;">](https://thomasdarkson.com)

# hxcpp_JIT

hxcpp_JIT lets you compile and run native C++ code at runtime from Haxe. It wraps Clang's interpreter (Clang-Repl) in a small C library and exposes it with a simple Haxe class.

Define a function in a string or a file, then call it, read variables, or change them, all while your program is running.

> [!NOTE]
> Supports hxcpp on Windows x64 and Linux x86_64 (glibc 2.28+).

> [!WARNING]
> This repository contains files larger than 100 MB, so to clone the repository safely, [Git LFS](https://git-lfs.com/) installation is needed. Make sure it is installed before you clone it.

MinGW is the default backend on Windows. The Haxe application can still be built with MSVC, while the JIT uses bundled LLVM-MinGW headers and runtime libraries.

The MSVC backend is still available with `-D hxcpp_jit_msvc`. Linux uses its own bundled headers and runtime libraries automatically.

## Installation
```
haxelib install hxcpp_JIT
```

Enter this command in the command prompt to get the library, then add it to your Haxe project.

------------

### Haxe Projects
Add this to `build.hxml` to add hxcpp_JIT to your Haxe build.
```hxml
-lib hxcpp_JIT
```

### OpenFL Projects
Add this to `project.xml`.
```xml
<haxelib name="hxcpp_JIT"/>
```

------------

### Requirements
- Windows 10 x64 (21H2 or higher) or Windows 11, or Linux x86_64 with glibc 2.28 or newer
- Haxe and hxcpp (For building the Haxe application only)
- MSVC and the Windows SDK on Windows, or the normal C++ build tools on Linux (For building the Haxe application only)
- The runtime files for your platform (included with the corresponding hxcpp_JIT backend, see [below](#shipping-the-executable))

To rebuild the native library, you also need [CMake](https://cmake.org/download/), [Ninja](https://github.com/ninja-build/ninja/releases) and LLVM/Clang source. The Linux bundle uses LLVM 23.1.5. The Windows MinGW build also needs an x64 UCRT [LLVM-MinGW toolchain](https://github.com/mstorsjo/llvm-mingw/releases).

MinGW users do not need build tools installed on their PC. Linux users need system glibc 2.28 or newer. Your Haxe application and its other native libraries must also support the systems you want to run on.

If you choose the MSVC JIT backend, users still need the MSVC Build Tools and Windows SDK. The setup described below can install them. The Visual C++ v14 runtime DLLs are shipped with that backend (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

### Automatic setup
This applies to the optional MSVC backend only. MinGW and Linux do not run this setup.

hxcpp_JIT includes [hxcpp_JIT_setup](https://github.com/ThomasDarkson/hxcpp_JIT_setup), a small program that checks for the MSVC build tools and Windows SDK (Visual Studio 2026), and installs them if missing.

The setup exe is **embedded in your compiled program**, so you don't need to ship the setup. When your program starts:

1. The embedded setup is extracted to `%TEMP%\hxcpp_jit\setup_<hash>.exe` (once per build, later launches reuse it).
2. It runs silently to check the machine. If everything is installed, your program continues as normal.
3. If something is missing, the setup asks for administrator permission and opens a window that installs it automatically. The window closes itself once everything is installed, and your program continues.

The setup needs administrator permission and an internet connection to install components, and everything is downloaded from Microsoft.

See the [hxcpp_JIT_setup README](https://github.com/ThomasDarkson/hxcpp_JIT_setup) for its arguments, exit codes and how to build it.

To update the embedded setup, replace `build/hxcpp_jit_setup.exe` in this project and recompile your Haxe project. It is read at Haxe compile time, so already compiled programs keep the old version.

The native library and its headers/runtime files are supplied in `build-mingw/` for Windows MinGW, `build/` for Windows MSVC and `build-linux/` for Linux. Use the bundle for your selected backend. The Haxe build copies the runtime files to the application output.

## Building the DLL
This part of the README explains how to rebuild the native library that hxcpp_JIT needs. Extract the complete LLVM/Clang source into `llvm-project/`.

### MinGW
Run these commands from an x64 Native Tools PowerShell with CMake, Ninja and the MSVC `lib.exe` tool available. Extract the x64 UCRT LLVM-MinGW toolchain into a separate folder first (e.g., C:\llvm-mingw).

```powershell
# First build, or after updating LLVM or LLVM-MinGW
./scripts/build-mingw.ps1 -Toolchain C:/llvm-mingw

# Native source changes, using the same LLVM build and toolchain
./scripts/build-mingw.ps1 -Toolchain C:/llvm-mingw -SkipLLVM
```

This builds LLVM and hxcpp_JIT with LLVM-MinGW, and packages the result into `build-mingw/`.

### MSVC
Run these commands from an x64 Native Tools PowerShell with MSVC, the Windows SDK, CMake and Ninja installed.

```powershell
# First build, or after updating LLVM
./scripts/build-msvc.ps1

# Native source changes, using the same LLVM build
./scripts/build-msvc.ps1 -SkipLLVM
```

This builds the optional MSVC backend into `build/`. Add `-D hxcpp_jit_msvc` to the Haxe build, or `<define name="hxcpp_jit_msvc"/>` to an OpenFL project.

### Linux
Run this command to build with a glibc 2.28 baseline using Docker or Podman.

```bash
./scripts/build-linux.sh --llvm-version 23.1.5 --jobs 2
```

The Linux script downloads LLVM source and builds the bundle into `build-linux/`. Change `--llvm-version` when updating LLVM. Docker or Podman is needed for this build workflow.

## Shipping the executable
Ship the files copied to the output for the backend you use.

### Windows MinGW
- `hxcpp_JIT.dll`
- `libc++.dll`
- `libunwind.dll`
- `mingw/`
- `clang-resources/`
- The applicable license and third-party notices

### Windows MSVC
- `hxcpp_JIT.dll`
- `clang-resources/`
- The Visual C++ runtime DLLs supplied with `build/`
- The applicable license and third-party notices

The MSVC Build Tools and Windows SDK must be installed on the user's PC. The setup is embedded in the Haxe executable, so it does not need to be shipped separately.

### Linux
- `libhxcpp_JIT.so`
- `libgcc_s.so.1`
- `linux/`
- `clang-resources/`
- `licenses/`

## Usage
To use hxcpp_JIT, you will need either C++ code or a file containing it. Using a file is recommended for anything bigger than a few lines.

### Using without a file
```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp(); // Create a new session
		cpp.eval("
			double half(double x) {
				return x / 2;
			}
		"); // Compile the code
		var result:Float = cpp.call("half", [5.0]); // Call it and get the returned value
		cpp.dispose(); // Free the session
	}
}
```

### Using with a file
`eval` accepts a path to a file. Pass `true` as the second argument to always treat the first argument as a file.

```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp();
		cpp.eval("script.cpp", true); // Contains the same code as the script above
		var result:Float = cpp.call("half", [5.0]);
		cpp.dispose();
	}
}
```

If the second argument is left out, `eval` treats its first argument as a file only when it points to an existing file. Otherwise it is treated as code. Pass `false` to never read a file.

```haxe
cpp.eval("int x = 3;");        // code
cpp.eval("script.cpp");        // file, since it exists
cpp.eval("script.cpp", true);  // file, and an exception is thrown if it doesn't exist
cpp.eval("script.cpp", false); // code
```

### Sessions keep their state
Everything defined by an `eval` stays available to the next ones.

```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp();
		cpp.eval("int counter = 0;");
		cpp.eval("void bump() { counter += 2; }");
		cpp.call("bump");
		cpp.call("bump");
		trace(cpp.get("counter")); // 4
	}
}
```

Each `Cpp` instance is its own session. Functions and variables of one instance can't be seen by another.

## Calling functions
`call` finds a function by its name, works out its signature, and remembers it for the next calls. The function has to be declared with `eval` first.

```haxe
cpp.eval("int add(int a, int b) { return a + b; }");
trace(cpp.call("add", [1, 2])); // 3
```

`call` returns the value directly. It doesn't catch errors, so use `try`/`catch`.

```haxe
try {
	cpp.call("missing");
} catch (e:hxcpp_jit.CppError) {
	trace(e.message);
}
```

Argument counts and value kinds are checked before execution; mismatches throw `CppError`. Pass exactly as many arguments as the function takes. Default arguments of the C++ function can't be used. Arguments are converted to the parameter types like in C++. A `const char*` parameter only points to the string while the call is running, so use `std::string` if the function keeps it.

For functions you call a lot, or functions that are overloaded, use `bind`. For overloaded functions, the return type and the argument types have to be given, since the signature can't be worked out.

```haxe
cpp.eval("
	double area(double w, double h) { return w * h; }
	int area(int side) { return side * side; }
");

var rect = cpp.bind("area", "double", ["double", "double"]);
trace(rect.call([2.0, 3.0])); // 6
rect.dispose(); // Optional early release; session disposal also releases bound functions
```

Lambdas and functors work too, as long as they don't have an overloaded `operator()`.

## Variables
`get` evaluates any C++ expression, and `set` assigns to a C++ lvalue.

```haxe
cpp.eval("
	struct Point { double x, y; };
	Point point = {1.0, 2.0};
	int arr[4] = {1, 2, 3, 4};
");

trace(cpp.get("point.x + point.y")); // 3
cpp.set("point.x", 1.5);
cpp.set("arr[2]", 7);
trace(cpp.get("arr[2]")); // 7
```

`set` can't create variables. A variable has to be declared with `eval` first, with any value (`0`, `nullptr`, or none at all). Setting a variable that doesn't exist throws a `CppError`.

```haxe
cpp.set("score", 10); // Throws, `score` doesn't exist
cpp.eval("int score = 0;");
cpp.set("score", 10); // Works
```

The value is converted to the variable's type like a C++ assignment would, so setting an `int` to `3.7` stores `3`. To make a variable read-only, declare it `const`.

Strings should go into `std::string` variables. A `const char*` variable would point to memory that is freed as soon as `set` returns.

## Value mapping
Values are converted between Haxe and C++ like this:

| Haxe | C++ |
|---|---|
| `Int` | integral types, enums |
| `Bool` | `bool` |
| `Float` | `float`, `double` |
| `String` | `const char*`, `char*`, `std::string` |
| `CppPointer` | pointers |
| `null` | `void`, null pointers |

`bool` results come back as `Bool`. A `Bool` can also be passed to integral parameters (`true` is `1`). Integers that don't fit in an `Int` come back as `Float`. Pointers are stored as numbers, so they are exact for addresses below 2^53.

Structs, classes and containers can't be passed directly. Wrap them in a function or expression that returns one of the types above, like `point.x` in the example above. Trying to return an unsupported type throws a `CppError`.

### Pointers
Pointers come back as `CppPointer` instances, which can be passed back to C++.

```haxe
cpp.eval("int value = 5; int* ptr = &value;");
var p = cpp.get("ptr");
trace(p.isNull()); // false
cpp.set("ptr", null); // Sets it to a null pointer
trace(cpp.get("ptr").isNull()); // true
```

`address` returns the address of a symbol by its IR name. Only `extern "C"` variables and functions have a plain name; other C++ symbols are mangled by the compiler (on Windows, `int x;` is `?x@@3HA`). To get the address of any variable, use `get("&x")` instead.

```haxe
cpp.eval("extern \"C\" int shared = 1;");
var addr = cpp.address("shared");
var same = cpp.get("&shared"); // Works for any variable
```

#### CppPointer
`CppPointer` is a native pointer stored as a numeric address. It is returned by `get`, `call` and `address` for pointer values, and it can be passed back to C++ through `set` and `call`.

| Member | Description |
|---|---|
| `new CppPointer(address:Float)` | Creates a pointer from a numeric address. |
| `address:Float` | The address the pointer holds (read-only). It is exact for addresses below 2^53. |
| `isNull():Bool` | `true` if the address is `0`. |
| `toString():String` | A readable form, like `JitPtr(0x0000000000401000)`. |

```haxe
import hxcpp_jit.Cpp;
import hxcpp_jit.CppPointer;

class Main {
	static function main() {
		var cpp = new Cpp();
		cpp.eval("int value = 5; int* ptr = &value; int read(int* p) { return *p; }");

		var p:CppPointer = cpp.get("ptr");
		trace(p);            // JitPtr(0x...)
		trace(p.address);    // The address as a Float
		trace(cpp.call("read", [p])); // 5, pointers can be passed back to C++

		var none = new CppPointer(0);
		trace(none.isNull()); // true
		cpp.set("ptr", none); // Same as cpp.set("ptr", null)
	}
}
```

A `CppPointer` is only a number. It does not keep the memory it points to alive, and the library does not check that the address is valid, so using a pointer after the variable it points to is gone is undefined behavior, as in C++.

## std::string
`std::string` arguments and results are enabled by default. This needs the C++ standard library headers to be on the machine.

```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp();
		cpp.eval("
			#include <string>
			std::string greet(std::string name) {
				return \"Hello, \" + name;
			}
		");
		trace(cpp.call("greet", ["Haxe"])); // Hello, Haxe
	}
}
```

If the headers aren't available, creating the session throws a `CppError`. To skip `std::string` support, pass `false` as the second constructor argument. It can be turned on later with `enableStdString`, which does nothing if it is already on. `stdStringEnabled` tells you the current state.

```haxe
var cpp = new Cpp(null, false); // No std::string yet
trace(cpp.stdStringEnabled); // false
cpp.enableStdString(); // Throws a CppError if the headers are missing
trace(cpp.stdStringEnabled); // true
```

Without `std::string` support, use `const char*` in your C++ code.

[hxcpp_JIT_setup](https://github.com/ThomasDarkson/hxcpp_JIT_setup) already installs needed headers.

## Supported C++
hxcpp_JIT runs real Clang, so most of C++ works, including:

- Templates, namespaces, enums, `constexpr`, lambdas, functors and recursion.
- Classes, inheritance, virtual functions, multiple inheritance, RTTI (`dynamic_cast`, `typeid`).
- The standard library (`<vector>`, `<map>`, `<memory>`, `<functional>`, `<cmath>`, `<cstdio>` and so on).
- Exceptions thrown and caught inside C++ code, including standard exception types, rethrowing, nested exceptions and `std::exception_ptr`.
- `thread_local` variables and static initializers.

An exception that escapes C++ code back into Haxe is not translated into a `CppError`, so catch it in C++ ([see Errors](#errors)).

## Compiler flags
The first argument of the constructor takes extra flags for Clang. The default standard is `-std=c++20`.

```haxe
var cpp = new Cpp(["-std=c++17", "-O2", "-I/my/include"]);
```

## Shared libraries
`loadLibrary` loads a shared library (`.dll` on Windows, `.so` on Linux) so the C++ code in the session can call its exports. Load it first, then declare the functions you need with `eval`. The declaration is enough, the function itself comes from the library.

```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp();
		cpp.loadLibrary("mathlib.dll"); // libmathlib.so on Linux
		cpp.eval("extern \"C\" int triple(int x);");
		trace(cpp.call("triple", [4])); // 12
		cpp.dispose();
	}
}
```

Relative paths are resolved against the current working directory. Loading the same file again does nothing, and the library stays loaded until the session is disposed. If the file or one of its dependencies can't be loaded, `loadLibrary` throws a `CppError`.

Use `extern "C"` for functions you declare, the same way as for `address`, so their names match the ones the library exports. On MinGW, `__imp_` names such as the ones used for `__declspec(dllimport)` work too.

Libraries are part of the [compilation cache](#compilation-cache) check, so load them before `evalCachedFile`, and in the same order on every launch.

## Compilation cache
Compiling a big C++ file with Clang can take a long time, and `eval` does it again every time your program starts. `evalCachedFile` compiles a file once, saves the result to a folder, and loads it from there on the next launches.

```haxe
import hxcpp_jit.Cpp;

class Main {
	static function main() {
		var cpp = new Cpp(["-std=c++17", "-O2"]);
		trace(cpp.evalCachedFile("core.cpp", "api.hpp", "cache/core")); // Compiled and cached
		trace(cpp.call("add", [2, 3])); // 5
		cpp.dispose();
	}
}
```

The first launch compiles `core.cpp` and fills `cache/core`. Every launch after that loads it instead of compiling it again, and `cacheStatus` tells you which one happened.

### Source and declarations
`evalCachedFile` takes two files and a folder.

| Argument | Description |
|---|---|
| `source` | The C++ file to cache. It holds the function bodies, private globals and initialization. |
| `declarations` | A header with the types and declarations that you want to use from Haxe. |
| `cacheDirectory` | The folder the cache is saved in. It is created if it doesn't exist. |

`core.cpp`:
```cpp
#include "api.hpp"
int add(int a, int b) { return a + b; }
```

`api.hpp`:
```cpp
#pragma once
int add(int a, int b);
```

The declarations file is what later `call`, `bind`, `get` and `set` see, so put everything they need in it, types, macros and function declarations. Do not put statements or non-inline definitions in it, they belong in the source.

Use a cache for a big, self-contained file, and call its functions afterwards. Each file needs its own `cacheDirectory`, never share one between two files.

### cacheStatus
`evalCachedFile` returns a readable message about what the cache did, and `cacheStatus` holds the same message for the last call.

```haxe
trace(cpp.cacheStatus); // Loaded compiled cache (IR + machine code)
```

| Message | Meaning |
|---|---|
| `Compiled and cached` | Nothing usable was saved, so the file was compiled and saved. |
| `Loaded compiled cache (IR + machine code)` | The saved code and its machine code were both reused. |
| `Loaded cached IR; rebuilt machine code` | The machine code was missing or damaged, so only it was compiled again. |
| `Loaded cached IR; machine code deferred until first use` | The machine code is compiled when a function needs it. |
| `Compiled; cache write skipped: ...` | The code compiled but couldn't be saved. |
| `Cache disabled: ...` | The folder can't be written to, so the file was compiled without a cache. |
| `Cache load failed; removed cached code` | The saved code couldn't be run and was deleted. See [Errors](#cache-errors). |

A cache that can't be saved never stops your program, the file is just compiled like `eval` would.

### What is saved
A cache folder contains three files, and all of them can be replaced:

| File | Description |
|---|---|
| `unit.bc` | The compiled code as LLVM bitcode. |
| `unit.obj` | The machine code, with a check to make sure it belongs to this cache. |
| `unit.json` | The inputs the cache was made from. |

Only code is saved. Pointers, variables and other memory are never saved, so **constructors and static initializers run again** every time, and each launch starts with fresh state.

### When the cache is thrown away
The cache is checked every time, using a SHA-256 hash of every file the compiler read, not the date or size of the file. Editing a header without changing its size or date is still noticed. The cache is thrown away and the file is compiled again when any of these change:

- The source, the declarations, or any header they include. Deleted files count too.
- The compiler flags, including the ones hxcpp_JIT adds by itself.
- Anything you `eval`'d on the same session before `evalCachedFile`.
- The path or contents of a library loaded with `loadLibrary` before `evalCachedFile`, or the order they were loaded in.
- The CPU features, the LLVM version, or `hxcpp_JIT.dll` itself.
- The working directory and the build tool environment variables.
- The location of the Windows SDK or C++ standard library headers, even if the old one is still installed.

A cache that is damaged, cut off or replaced while another program was writing is detected and rebuilt, and files are written in a way that can't leave a half-written cache. A file that changes while it is being compiled isn't saved at all.

To clear a cache yourself, delete its folder.

### Cache errors
If the cache can't be run, `evalCachedFile` throws a `CppError` and `cacheStatus` becomes `Cache load failed; removed cached code`. The bad cache is already deleted, but this session can't be used anymore, so create a new `Cpp` and call `evalCachedFile` again. Errors in your C++ code are thrown as usual.

```haxe
var cpp = new Cpp();
try {
	cpp.evalCachedFile("core.cpp", "api.hpp", "cache/core");
} catch (e:hxcpp_jit.CppError) {
	cpp.dispose();
	cpp = new Cpp(); // Fresh session
	cpp.evalCachedFile("core.cpp", "api.hpp", "cache/core"); // Compiles again and fills the cache
}
```

### SafeCpp
`SafeCpp` doesn't wrap `evalCachedFile` yet. Call it on the session in `cpp`, and use `try`/`catch` with it.

```haxe
var safe = new SafeCpp();
safe.cpp.evalCachedFile("core.cpp", "api.hpp", "cache/core");
```

## LLVM version
`Cpp.LLVM_VERSION` is the LLVM version that the library was built with.

```haxe
trace(Cpp.LLVM_VERSION); // 23.1.5
```

## Errors
Compilation errors, argument-count/type mismatches, and use of disposed Haxe sessions/functions throw a `CppError`. When C++ code fails to compile, its message contains Clang's diagnostics. Exceptions escaping C++ execution and native faults are not translated into `CppError`.

```haxe
import hxcpp_jit.Cpp;
import hxcpp_jit.CppError;

class Main {
	static function main() {
		var cpp = new Cpp();
		try {
			cpp.eval("int broken = nope;");
		} catch (e:CppError) {
			trace(e.message); // Clang's error about the undeclared identifier 'nope'
		}
		cpp.dispose();
	}
}
```

The session stays usable after a failed `eval`. Using a session or function after it was disposed throws a `CppError` too, and disposing twice is safe.

## SafeCpp
`SafeCpp` is `Cpp` with every call wrapped in a `try`/`catch`. Instead of throwing, a failed call returns `null` (or does nothing for `Void` functions) and stores the error in `lastError`. Use it when you would rather check for errors than catch them, for example in scripting or in game code where a bad script shouldn't crash the program.

```haxe
import hxcpp_jit.SafeCpp;

class Main {
	static function main() {
		var cpp = new SafeCpp();
		cpp.eval("int i = zero;"); // Doesn't throw
		if (cpp.lastError != null)
			trace(cpp.lastError.message); // Clang's error about the undeclared identifier 'zero'
		cpp.dispose();
	}
}
```

It has the same methods as `Cpp`: `eval`, `enableStdString`, `get`, `set`, `bind`, `call`, `address` and `dispose`, and the same constructor arguments.

### onError
Set `onError` to be called every time an error is caught. It receives the error, the underlying `Cpp` session (`null` if the session could not be created), and the name of the method that failed: `new`, `eval`, `enableStdString`, `get`, `set`, `bind`, `call`, `address` or `dispose`. Errors from a function returned by `bind` are reported as `SafeCppFunction.call` and `SafeCppFunction.dispose`.

The last argument is an array with the arguments the method was called with, in the order of its signature. `eval("int i = zero;")` gives `["int i = zero;", null]`, `set("x", 1, true)` gives `["x", 1, true]`, and `call("f", [1, 2])` gives `["f", [1, 2]]`. Methods without arguments, like `dispose`, give an empty array.

```haxe
var cpp = new SafeCpp();
cpp.onError = (error, session, method, args) -> trace(method + "(" + args + "): " + error.message);
cpp.eval("int i = zero;"); // Prints "eval(int i = zero;,null): " followed by Clang's error
```

### Creating variables
`SafeCpp.set` takes an optional third argument, `autoDeclare`. When it is `true` and the variable doesn't exist yet, `set` creates it first. The C++ type is picked from the first value.

```haxe
var cpp = new SafeCpp();
cpp.set("x", 64, true); // Same as cpp.eval("int x{};") followed by cpp.set("x", 64)
trace(cpp.get("x")); // 64
```

| Haxe value | C++ type |
|---|---|
| `Bool` | `bool` |
| `Int` | `int` |
| `Float` | `double` |
| `String` | `std::string` (needs `stdStringEnabled`) |
| `CppPointer` or `null` | `void*` |

Only plain names are created. `point.x` or `arr[2]` still need the variable declared with `eval`. Haxe can't tell `64` from `64.0` at runtime, so both create an `int`. Declare the variable with `eval` for any other type. Later sets convert to the variable's type like a C++ assignment.

`autoDeclare` is `false` by default.

### Bound functions
`bind` returns a `SafeCppFunction`, whose `call` and `dispose` report errors to the owning `SafeCpp` in the same way.

```haxe
cpp.eval("int add(int a, int b) { return a + b; }");
var add = cpp.bind("add", "int", ["int", "int"]);
trace(add.call([1, 2])); // 3
add.call([1]);           // Wrong argument count, returns null
trace(add.lastError.message);
add.dispose();
```

### Things to know
- `lastError` is not cleared by a successful call, so check the return value, or set it from `onError`, rather than only testing `lastError != null`.
- A `null` result can also be a legitimate value (a `void` function or a null pointer), so use `lastError` or `onError` to tell the difference.
- `cpp` holds the underlying `Cpp` session. It is `null` if creation failed or after `dispose`.
- `SafeCpp` is a little slower than `Cpp` because of the `try`/`catch`.

## License
hxcpp_JIT is licensed under the [Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for attribution.

The precompiled `hxcpp_JIT.dll` contains parts of LLVM and Clang (Apache License v2.0 with LLVM Exceptions). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details. The Microsoft Visual C++ runtime DLLs shipped with hxcpp_JIT, and the Build Tools/Windows SDK installed by the setup, are licensed by Microsoft under their own terms.