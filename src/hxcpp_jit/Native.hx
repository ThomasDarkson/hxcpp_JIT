package hxcpp_jit;

import cpp.ConstCharStar;
import cpp.Int64;
import cpp.Star;

@:include("hxcpp_JIT.h") @:native("JitEngine") extern class RawEngine {}
@:include("hxcpp_JIT.h") @:native("JitValue") extern class RawValue {}
@:include("hxcpp_JIT.h") @:native("JitFn") extern class RawFn {}

@:include("hxcpp_JIT.h")
@:build(macros.Macro.addBuildXml())
extern class Native {
	@:native("jit_create_args") static function create(args:ConstCharStar):Star<RawEngine>;

	@:native("jit_llvm_version") static function llvmVersion():ConstCharStar;

	@:native("jit_destroy") static function destroy(e:Star<RawEngine>):Void;

	@:native("jit_last_error") static function lastError(e:Star<RawEngine>):ConstCharStar;

	@:native("jit_eval") static function eval(e:Star<RawEngine>, code:ConstCharStar):Int;

	@:native("jit_enable_std_string") static function enableStdString(e:Star<RawEngine>):Int;

	@:native("jit_get") static function get(e:Star<RawEngine>, expr:ConstCharStar, out:Star<RawValue>):Int;

	@:native("jit_set") static function set(e:Star<RawEngine>, lvalue:ConstCharStar, v:Star<RawValue>):Int;

	@:native("jit_symbol") static function symbol(e:Star<RawEngine>, name:ConstCharStar):Star<cpp.Void>;

	@:native("jit_bind_str") static function bind(e:Star<RawEngine>, name:ConstCharStar, ret:ConstCharStar, args:ConstCharStar):Star<RawFn>;

	@:native("jit_call") static function call(f:Star<RawFn>, args:Star<RawValue>, count:Int, ret:Star<RawValue>):Int;

	@:native("jit_fn_last_error") static function fnLastError(f:Star<RawFn>):ConstCharStar;

	@:native("jit_fn_free") static function fnFree(f:Star<RawFn>):Void;

	@:native("jit_value_new") static function valueNew(count:Int):Star<RawValue>;

	@:native("jit_value_free") static function valueFree(v:Star<RawValue>):Void;

	@:native("jit_value_at") static function valueAt(v:Star<RawValue>, i:Int):Star<RawValue>;

	@:native("jit_value_set_int") static function setInt(v:Star<RawValue>, x:Int64):Void;

	@:native("jit_value_set_double") static function setDouble(v:Star<RawValue>, x:Float):Void;

	@:native("jit_value_set_string") static function setString(v:Star<RawValue>, s:ConstCharStar):Void;

	@:native("jit_value_set_addr") static function setAddr(v:Star<RawValue>, a:Float):Void;

	@:native("jit_value_kind") static function kind(v:Star<RawValue>):Int;

	@:native("jit_value_get_int") static function getInt(v:Star<RawValue>):Int64;

	@:native("jit_value_get_double") static function getDouble(v:Star<RawValue>):Float;

	@:native("jit_value_get_string") static function getString(v:Star<RawValue>):ConstCharStar;

	@:native("jit_value_get_addr") static function getAddr(v:Star<RawValue>):Float;
}
