package macros;
#if macro
import haxe.macro.Context;
class Backend {
    public static function isLinux():Bool {
        if (Context.defined("linux")) return true;
        if (Context.defined("windows") || Context.defined("mac") || Context.defined("android") || Context.defined("ios")) return false;
        return Sys.systemName() == "Linux";
    }
    public static function useMinGW():Bool {
        if (isLinux()) {
            if (Context.defined("hxcpp_jit_msvc") || Context.defined("hxcpp_jit_mingw"))
                Context.fatalError("Windows JIT backend flags cannot be used on Linux.", Context.currentPos());
            return false;
        }
        if (Context.defined("hxcpp_jit_msvc") && Context.defined("hxcpp_jit_mingw"))
            Context.fatalError("Choose only one Windows JIT backend.", Context.currentPos());
        return !Context.defined("hxcpp_jit_msvc");
    }
}
#end
