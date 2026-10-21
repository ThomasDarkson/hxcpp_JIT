package hxcpp_jit;

import cpp.Star;
import hxcpp_jit.Native;

/**
	A bound C++ callable.

	Create one with `Cpp.bind`. Unlike `Cpp.call`, which looks the function up by name on every call,
	a bound function is called directly, so it is the better choice for functions that are called often.

	```haxe
	var add = cpp.bind("add");
	for (i in 0...1000)
		add.call([i, 1]);
	add.dispose();
	```

	@see `Cpp.bind`
**/
class CppFunction {
	var fn:Star<RawFn>;
	var owner:Cpp;

	@:allow(hxcpp_jit) function new() {}

	@:allow(hxcpp_jit) @:unreflective function init(fn:Star<RawFn>, owner:Cpp):CppFunction {
		this.owner = owner;
		this.fn = fn;
		return this;
	}

	/**
		Calls the C++ function.

		Arguments are converted following the value mapping described in `Cpp`.

		@param args Arguments to call the function with. Can be left blank for functions without arguments.
		@return The function's return value, or `null` for `void`.
		@throws CppError If an argument or the return type is not supported.
	**/
	public function call(?args:Array<Dynamic>):Dynamic {
		if (fn == null)
			throw new CppError("C++ function has been disposed");
		owner.ensureAlive();
		var n = args == null ? 0 : args.length;
		var a = Native.valueNew(n);
		var r = Native.valueNew(1);
		try {
			for (i in 0...n)
				Cpp.write(Native.valueAt(a, i), args[i]);
			if (Native.call(fn, a, n, r) != 0)
				throw new CppError(Native.fnLastError(fn).toString());
			var out = Cpp.read(r);
			Native.valueFree(a);
			Native.valueFree(r);
			return out;
		} catch (e:Dynamic) {
			Native.valueFree(a);
			Native.valueFree(r);
			throw e;
		}
	}

	/**
		Frees this function. It can't be called afterwards.

		Does nothing if it was already disposed.
	**/
	public function dispose():Void {
		if (fn != null) {
			Native.fnFree(fn);
			fn = null;
			var session = owner;
			owner = null;
			session.releaseFunction(this);
		}
	}
}
