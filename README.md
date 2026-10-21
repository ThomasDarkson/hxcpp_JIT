[![](https://thomasdarkson.com/assets/stickers/sticker.png)](https://thomasdarkson.com)

[<img src="https://thomasdarkson.com/hxcpp_JIT/hxcpp_JIT_logo.png" style="width:30%; height:auto;">](https://thomasdarkson.com)

# hxcpp_JIT

hxcpp_JIT lets you compile and run native C++ code at runtime from Haxe. It wraps Clang's interpreter (Clang-Repl) in a small C library and exposes it with a simple Haxe class.

Define a function in a string or a file, then call it, read variables, or change them, all while your program is running.

> [!NOTE]
> Works only on hxcpp and Windows 10 (21H2 or higher) x64 or Windows 11.

Will be updated whenever a new version of LLVM comes out. Since LLVM updates bi-weekly, hxcpp_JIT will be updated bi-weekly as well.

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
- Windows 10 x64 (21H2 or higher) or Windows 11
- All Visual C++ Redistributable Runtimes (installed automatically by [the setup](#automatic-setup))
- Visual Studio 2019 (For building Haxe only)
    - Windows 10 SDK (10.0.19041.0) (For building Haxe only)
    - MSVC v142 - VS 2019 C++ x86/64 build tools (Latest) (For building Haxe only)
- Visual Studio 2022 (installed automatically by [the setup](#automatic-setup))
    - Windows 11 SDK (10.0.22621.0) (Works on Windows 10)
    - MSVC v143 - VS 2022 C++ x86/64 build tools (Latest)
    - C++ ATL for latest v143 build tools (x86 & x64)
- [CMake](https://cmake.org/download/) (For building the .DLL only)
- [Ninja](https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip) (For building the .DLL only)
- [LLVM Project 23.1.4](https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.4.zip) (For building the .DLL only)

Everything except the items labeled "for building Haxe only" or "for building the .DLL only" is required for both building and running. Any Haxe program that uses hxcpp_JIT will not run without these requirements.

You don't have to install the runtime requirements yourself. The items marked "installed automatically" are handled by the setup that is built into every program that uses hxcpp_JIT.

### Automatic setup
hxcpp_JIT includes [hxcpp_JIT_setup](https://github.com/ThomasDarkson/hxcpp_JIT_setup), a small program that checks for the Visual C++ Redistributables, MSVC build tools, Windows SDK and ATL, and installs any that are missing.

The setup exe is **embedded in your compiled program**, so there is no extra file to copy or ship. When your program starts:

1. The embedded setup is extracted to `%TEMP%\hxcpp_jit\setup_<hash>.exe` (once per build, later launches reuse it).
2. It runs silently to check the machine. If everything is installed, your program continues as normal.
3. If something is missing, a window opens that tells the user what is missing. The user presses **Install**, and your program continues once everything is installed.

The extracted copy is left in the temp folder so later launches can reuse it. The setup needs administrator permission and an internet connection to install components, and everything is downloaded from Microsoft.

See the [hxcpp_JIT_setup README](https://github.com/ThomasDarkson/hxcpp_JIT_setup) for its arguments, exit codes and how to build it.

To update the embedded setup, replace `build/hxcpp_jit_setup.exe` in this project and recompile your Haxe project. It is read at Haxe compile time, so already compiled programs keep the old version.

`hxcpp_JIT.dll` and `hxcpp_JIT.lib` are both precompiled and included with the project, so you can skip the .DLL building process entirely. When you compile the Haxe project, `hxcpp_JIT.dll` will be automatically copied to the build output directory. You still need everything required to run the application.

The precompiled `hxcpp_JIT.dll` is compressed with [upx](https://github.com/upx/upx).

## Building the DLL
This part of the README explains how to build the `hxcpp_JIT.dll` and `hxcpp_JIT.lib` that hxcpp_JIT actually needs.

### Step 1
Download [LLVM Project 23.1.4](https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.4.zip) and extract the source code into a folder called `llvm-project` in the root directory of the project. Make sure the put the **source files** in `llvm-project/`, not the **llvm-project-llvmorg-23.1.4** folder in the ZIP. 

### Step 2
You must configure LLVM before building it. Run **x64 Native Tools Command Prompt for VS 2022** **as administrator**. Run `cd path\to\hxcpp_JIT`, then switch to PowerShell by typing and entering `powershell`.

After that, run this command.
`cmake -S llvm-project/llvm -B llvm-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_PARALLEL_LINK_JOBS=2`

### Step 3
To actually build LLVM, run this command.

`cmake --build llvm-build --target clangInterpreter clangFrontend clang-resource-headers LLVMOrcJIT LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc LLVMX86Info`

This will take a while.

### Step 4
Now, to build `hxcpp_JIT.dll` and `hxcpp_JIT.lib`, run this command.
`$ws = (Get-Location).Path.Replace('\','/')`

After that, run this.
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DLLVM_DIR=$ws/llvm-build/lib/cmake/llvm" "-DClang_DIR=$ws/llvm-build/lib/cmake/clang"`

Finally, run this to build `hxcpp_JIT.dll` and `hxcpp_JIT.lib`.

`cmake --build build`

This will create both files in the `build/` folder in the root directory. You can now compile the Haxe project with `haxe build.hxml` or, for OpenFL projects, `lime test windows` as you normally would.

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

## LLVM version
`Cpp.LLVM_VERSION` is the LLVM version that the library was built with.

```haxe
trace(Cpp.LLVM_VERSION); // 23.1.4
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

The precompiled `hxcpp_JIT.dll` contains parts of LLVM and Clang (Apache License v2.0 with LLVM Exceptions) and is compressed with UPX. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details. The Microsoft components installed by the setup are licensed by Microsoft under their own terms.