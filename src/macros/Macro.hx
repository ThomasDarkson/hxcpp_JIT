package macros;

#if macro
import haxe.io.Path;
import haxe.macro.Context;
import haxe.macro.Expr;
import sys.FileSystem;

class Macro {
	public static function addBuildXml():Array<Field> {
		var srcFile = FileSystem.fullPath(Context.resolvePath("hxcpp_jit/Native.hx"));
		var root = Path.directory(Path.directory(Path.directory(srcFile)));
		var nativeDir = Path.join([root, "native"]);
		var libDir = Context.definedValue("hxcpp_JIT_lib");
		if (libDir == null) libDir = Sys.getEnv("HXCPP_JIT_LIB_DIR");
		if (libDir == null) libDir = Path.join([root, "build"]);
		if (Sys.systemName() != "Windows")
			Context.fatalError("hxcpp_JIT only supports Windows.", Context.currentPos());
		var xml = '<files id="haxe"><compilerflag value="-I$nativeDir"/></files>\n' + '<target id="haxe">\n';
		var lib = Path.join([libDir, "hxcpp_JIT.lib"]);
		if (!FileSystem.exists(lib)) {
			var rel = Path.join([libDir, "Release", "hxcpp_JIT.lib"]);
			if (FileSystem.exists(rel)) lib = rel;
		}
		xml += '  <lib name="$lib"/>\n';
		xml += '</target>\n';
		Context.getLocalClass().get().meta.add(":buildXml", [macro $v{xml}], Context.currentPos());
		return null;
	}
}
#end
