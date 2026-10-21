package hxcpp_jit;

import sys.FileSystem;
import haxe.Resource;
import cpp.ConstCharStar;
import cpp.Star;
import hxcpp_jit.Native;
import sys.io.File;

/**
	[![](https://thomasdarkson.com/assets/stickers/sticker.png)](https://thomasdarkson.com)

	A C++ JIT session (Clang-Repl) with functions for compiling, running, and interacting with C++ code at runtime.

	To get started, create a Cpp instance:

	```haxe
	import hxcpp_jit.Cpp;
	class Main {
		static function main() {
			var cpp = new Cpp();
			cpp.eval('
				int add(int a, int b) {
					return a + b;
				}
			');

			trace(cpp.call("add", [1, 2])); // 3
			cpp.dispose();
		}
	}
	```

	You can also use files!

	```haxe
	import hxcpp_jit.Cpp;
	class Main {
		static function main() {
			var cpp = new Cpp();
			cpp.eval("script.cpp", true);
			trace(cpp.call("add", [1, 2])); // 3
			cpp.dispose();
		}
	}
	```

	**Value mapping**

	| Haxe | C++ |
	|---|---|
	| `Int` | integral types, enums |
	| `Bool` | `bool` |
	| `Float` | `float`, `double` |
	| `String` | `const char*`, `char*`, `std::string` (see `stdStringEnabled`) |
	| `CppPointer` | pointers |
	| `null` | `void`, null pointer |

	Anything else (structs, classes, containers) has to be wrapped in a C++ expression
	or function that returns one of the types above.

	@see `eval`
	@see `get`
	@see `set`
	@see `call`
	@see `bind`
**/
@:keep
@:keepInit
@:keepSub
@:build(macros.DLL.moveDLL())
class Cpp {
	static final _boot = @:privateAccess Setup.init();

	/**
		The LLVM version the library `hxcpp_JIT` is actually built against.

		`hxcpp_JIT 0.0.1` uses LLVM `23.1.4`.
	**/
	public static var LLVM_VERSION(default, never):String = Native.llvmVersion().toString();

	/**
		Whether `std::string` arguments and results are allowed in `call`, `get` and `set`.

		This is `true` right after creation unless the instance was created with
		`stdString` set to `false` and `enableStdString` has not been called since.

		@see `enableStdString`
	**/
	public var stdStringEnabled(default, null):Bool = false;

	var engine:Star<RawEngine>;
	var bound:Map<String, CppFunction> = new Map();
	var functions:Array<CppFunction> = [];

	/**
		Creates a new C++ JIT session.

		Every instance is its own session. Functions and variables from one instance are not visible to another.

		@param args Extra clang flags, e.g. `["-std=c++17", "-O2", "-I/my/include"]`. Default standard is `-std=c++20`.
		@param stdString Whether to enable `std::string` support right away. If enabling fails, an exception is thrown. Default is `true`.
		@throws CppError If the session could not be created, or `std::string` could not be enabled.
	**/
	public function new(?args:Array<String>, stdString:Bool = true) {
		engine = Native.create(ConstCharStar.fromString(args == null ? "" : args.join("\n")));
		if (engine == null)
			throw new CppError("Failed to create JIT: " + Native.lastError(null).toString());

		if (stdString) {
			try {
				enableStdString();
			} catch (e:CppError) {
				Native.destroy(engine);
				engine = null;
				throw new CppError("Failed to enable std::string (are the C++ standard library headers available? Pass stdString = false to skip it): " + e.message);
			}
		}
	}

	/**
		Compiles and runs C++.

		`source` can be either C++ code or the path of a file containing C++ code:

		```haxe
		cpp.eval("int x = 3;"); // code
		cpp.eval("script.cpp"); // file, detected automatically because it exists
		cpp.eval("script.cpp", true); // file, always
		```

		@param source C++ code, or a path to a file with C++ code.
		@param isFile If `true`, `source` is always treated as a file path and an exception is thrown if the file does not exist.
		If `false`, `source` is always treated as code.
		If `null` (default), `source` is treated as a file path only if it is a single line and points to an existing file,
		otherwise as code.
		@throws CppError If the file could not be found, or if the code failed to compile or run. The message contains clang's diagnostics.
	**/
	public function eval(source:String, ?isFile:Bool):Void {
		ensureAlive();
		if (isFile == null)
			isFile = isExistingFile(source);

		var code = source;
		if (isFile) {
			if (!isExistingFile(source))
				throw new CppError("File not found: " + source);
			code = File.getContent(source);
		}

		check(Native.eval(engine, ConstCharStar.fromString(code)));
	}

	/**
		Allows `std::string` arguments and results in `call`, `get` and `set`.

		Does nothing if it is already enabled, which is the default for new instances (see the `stdString` argument of `new()`).
		Needs the C++ standard library headers on this machine.
		Without it, use `const char*` in your C++ code.

		@see `stdStringEnabled`
		@throws CppError If the standard library headers could not be found.
	**/
	public function enableStdString():Void {
		ensureAlive();
		if (stdStringEnabled)
			return;

		check(Native.enableStdString(engine));
		stdStringEnabled = true;
	}

	/**
		Evaluates a C++ expression and returns its value.
		Any C++ expression works:

		```haxe
		cpp.eval("int counter = 41;");
		trace(cpp.get("counter")); // 42
		trace(cpp.get("counter + 1")); // 42
		```

		Everything the expression uses must be declared first with `eval`.
		There is no way to check if a name exists, so `get` on an undeclared name throws.

		@param expr The C++ expression.
		@return `Int`, `Float`, `Bool`, `String`, `CppPointer`, or `null` for `void`.
		@throws CppError If the expression is invalid or its type is not supported.
	**/
	public function get(expr:String):Dynamic {
		ensureAlive();
		var v = Native.valueNew(1);
		try {
			check(Native.get(engine, ConstCharStar.fromString(expr), v));
			var r = Cpp.read(v);
			Native.valueFree(v);
			return r;
		} catch (e:Dynamic) {
			Native.valueFree(v);
			throw e;
		}
	}

	/**
		Assigns a value to a C++ lvalue.

		The variable must be declared first, and then it can be declared with any value.
		Setting a variable that was not declared throws an exception.

		```haxe
		cpp.set("counter", 3);
		cpp.set("point.x", 1.5);
		cpp.set("arr[2]", 7);
		```

		@param lvalue The C++ variable, field or array element to assign to.
		@param value The value to assign. `Int`, `Bool`, `Float`, `String`, `CppPointer` or `null`.
		@throws CppError If `lvalue` is not assignable, or `value` has an unsupported type.
	**/
	public function set(lvalue:String, value:Dynamic):Void {
		ensureAlive();
		var v = Native.valueNew(1);
		try {
			Cpp.write(v, value);
			check(Native.set(engine, ConstCharStar.fromString(lvalue), v));
			Native.valueFree(v);
		} catch (e:Dynamic) {
			Native.valueFree(v);
			throw e;
		}
	}

	/**
		Binds a C++ function, lambda or functor by name for repeated fast calls.

		The signature is deduced unless `ret` and `argTypes` are given.
		They are needed for overloaded functions:

		```haxe
		var area = cpp.bind("area", "double", ["double", "double"]);
		trace(area.call([2.0, 3.0])); // 6
		```

		The returned function can be disposed with `CppFunction.dispose` when it is no longer needed.
		Disposing the session also disposes all its bound functions.

		@param name Name of the C++ function.
		@param ret C++ return type. Leave blank to deduce the signature.
		@param argTypes C++ argument types. Leave blank to deduce the signature.
		@return The bound function.
		@throws CppError If the function does not exist, or the signature is ambiguous.
		@see `call`
	**/
	public function bind(name:String, ?ret:String, ?argTypes:Array<String>):CppFunction {
		ensureAlive();
		var f = Native.bind(engine, ConstCharStar.fromString(name), ret == null ? null : ConstCharStar.fromString(ret),
			ConstCharStar.fromString(argTypes == null ? "" : argTypes.join("\n")));
		if (f == null)
			throw new CppError(lastError());
		var result = new CppFunction().init(f, this);
		functions.push(result);
		return result;
	}

	/**
		Calls a C++ function by name.

		The function must be declared with `eval` before calling it.

		```haxe
		cpp.eval("int add(int a, int b) { return a + b; }");

		trace(cpp.call("add", [1, 2])); // 3

		try {
			cpp.call("missing");
		} catch (e:CppError) {
			trace(e.message);
		}
		```

		The signature is deduced and the binding is cached after the first call,
		so later calls to the same function are fast. Overloaded functions can't be deduced,
		so use `bind` with explicit types for them.

		Arguments are converted to the parameter types of the function, like a C++ call would.
		The argument count and value kinds are checked before execution; mismatches throw CppError.
		Pass exactly as many arguments as the function takes.
		Default arguments of the C++ function can't be used, all of the arguments must be passed.

		`std::string` parameters get their own copy of the value.
		A `const char*` parameter only points to the string while the call is running,
		so a function that stores the pointer somewhere will end up with a dangling pointer. Copy it into a `std::string` instead.

		@param name Name of the C++ function.
		@param args Arguments to call the function with.
		@return The function's return value, or `null` for `void`. `bool` results are returned as `Bool`.
		@throws CppError If the function does not exist, or an argument or the return type is not supported.
		@see `bind`
	**/
	public function call(name:String, ?args:Array<Dynamic>):Dynamic {
		ensureAlive();
		var f = bound.get(name);
		if (f == null) {
			f = bind(name);
			bound.set(name, f);
		}
		return f.call(args);
	}

	/**
		Returns the address of a symbol.

		The name is the IR name. Only `extern "C"` variables and functions have a plain name; other C++ symbols
		are mangled by the compiler (on Windows, `int x;` is `?x@@3HA`). 
		
		To get the address of any variable, use `get("&x")`.

		@param name Name of the symbol.
		@return The symbol's address.
		@throws CppError If the symbol could not be found.
	**/
	public function address(name:String):CppPointer {
		ensureAlive();
		var p:Star<cpp.Void> = Native.symbol(engine, ConstCharStar.fromString(name));
		if (p == null)
			throw new CppError(lastError());
		return new CppPointer(untyped __cpp__("(double)(unsigned long long){0}", p));
	}

	/**
		Destroys this session and frees everything it owns, including every function bound with `call` or `bind`.

		The instance can't be used afterwards.
	**/
	public function dispose():Void {
		for (f in functions.copy())
			f.dispose();
		functions = [];
		bound = new Map();
		if (engine != null) {
			Native.destroy(engine);
			engine = null;
		}
	}

	@:allow(hxcpp_jit) function ensureAlive():Void {
		if (engine == null)
			throw new CppError("C++ JIT session has been disposed");
	}

	@:allow(hxcpp_jit) function releaseFunction(f:CppFunction):Void {
		functions.remove(f);
		var names = [for (name => value in bound) if (value == f) name];
		for (name in names)
			bound.remove(name);
	}

	function lastError():String
		return Native.lastError(engine).toString();

	function check(rc:Int):Void {
		if (rc != 0)
			throw new CppError(lastError());
	}

	static function isExistingFile(path:String):Bool {
		if (path == null || path.length == 0)
			return false;
		try {
			return FileSystem.exists(path) && !FileSystem.isDirectory(path);
		} catch (e:Dynamic) {
			return false;
		}
	}

	@:allow(hxcpp_jit) static function read(v:Star<RawValue>):Dynamic {
		return switch (Native.kind(v)) {
			case 1:
				var f:Float = untyped __cpp__("(double){0}", Native.getInt(v));
				(f >= -2147483648.0 && f <= 2147483647.0) ? Std.int(f) : f;
			case 2: Native.getDouble(v);
			case 5:
				var b:Float = untyped __cpp__("(double){0}", Native.getInt(v));
				b != 0;
			case 3: new CppPointer(Native.getAddr(v));
			case 4: Native.getString(v).toString();
			case -1: throw new CppError("Unsupported C++ type (use int/float/pointer/const char*/std::string)");
			default: null;
		}
	}

	@:allow(hxcpp_jit) static function write(v:Star<RawValue>, x:Dynamic):Void {
		if (x == null)
			Native.setAddr(v, 0);
		else if (Std.isOfType(x, Bool))
			Native.setInt(v, (x : Bool) ? 1 : 0);
		else if (Std.isOfType(x, Int))
			Native.setInt(v, (x : Int));
		else if (Std.isOfType(x, Float))
			Native.setDouble(v, (x : Float));
		else if (Std.isOfType(x, String))
			Native.setString(v, ConstCharStar.fromString((x : String)));
		else if (Std.isOfType(x, CppPointer))
			Native.setAddr(v, (x : CppPointer).address);
		else
			throw new CppError("Unsupported argument type: " + Type.getClassName(Type.getClass(x)));
	}
}