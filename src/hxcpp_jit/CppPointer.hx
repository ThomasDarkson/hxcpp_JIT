package hxcpp_jit;

/**
	A native pointer, as a numeric address.

	Returned by `Cpp.get` and `Cpp.call` for pointer-typed values and by `Cpp.address`.
	It can be passed back to C++ through `Cpp.set` and `Cpp.call`.

	```haxe
	var p = cpp.get("&counter");
	trace(p.isNull()); // false
	```
**/
class CppPointer {
	/**
		The address this pointer holds.

		Stored as a `Float`, so it is exact for addresses below 2^53.
	**/
	public final address:Float;

	/**
		Creates a new pointer.

		@param address The numeric address.
	**/
	public function new(address:Float)
		this.address = address;

	/**
		Whether this pointer is a null pointer.
	**/
	public function isNull():Bool
		return address == 0;

	/**
		Returns a readable form of this pointer, like `JitPtr(0x0000000000401000)`.
	**/
	public function toString():String
		return "JitPtr(0x" + StringTools.hex(Std.int(address / 4294967296.0), 8) + StringTools.hex(Std.int(address % 4294967296.0), 8) + ")";
}
