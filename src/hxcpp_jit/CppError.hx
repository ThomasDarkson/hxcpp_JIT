package hxcpp_jit;

/**
	Thrown when something goes wrong in the JIT.

	For example, when C++ code fails to compile, a function or symbol can't be found,
	or a value has a type that can't be converted between Haxe and C++.

	For compile errors, `message` holds clang's diagnostics.
**/
class CppError extends haxe.Exception {}
