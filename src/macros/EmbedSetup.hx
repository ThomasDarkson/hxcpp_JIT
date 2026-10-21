package macros;

#if macro
import haxe.io.Path;
import haxe.macro.Context;
import haxe.macro.Expr;
import sys.FileSystem;
import sys.io.File;

using StringTools;
using haxe.macro.PositionTools;

class EmbedSetup {
	public static function build():Array<Field> {
		var pos = Context.currentPos();
		var dir = Path.directory(pos.getInfos().file);
		if (!Path.isAbsolute(dir))
			dir = Path.join([Sys.getCwd(), dir]);
		var exe = Path.normalize(Path.join([dir, "../build/hxcpp_JIT_setup.exe"]));
		var bytes = FileSystem.exists(exe) ? File.getBytes(exe) : null;
		if (bytes == null)
			Context.warning('hxcpp_JIT_setup.exe not found at $exe, nothing will be embedded', pos);
		else
			Context.registerModuleDependency("macros.EmbedSetup", exe);

		var n = bytes == null ? 0 : bytes.length;
		var sb = new StringBuf();
		sb.add("static const unsigned char kSetupData[] = {\n");
		for (i in 0...n) {
			sb.add(bytes.get(i));
			sb.add(i == n - 1 ? "" : ",");
			if (i % 32 == 31) sb.add("\n");
		}
		if (n == 0) sb.add("0");
		sb.add("\n};\nstatic const size_t kSetupSize = " + n + ";\n");
		var data = sb.toString();

		var cls = Context.getLocalClass().get();
		var found = cls.meta.extract(":cppFileCode");
		if (found.length == 0)
			Context.error("Setup needs @:cppFileCode", pos);
		var code = switch (found[0].params[0].expr) {
			case EConst(CString(s, _)): s;
			default: Context.error("@:cppFileCode must be a plain string", pos);
		}
		cls.meta.remove(":cppFileCode");
		cls.meta.add(":cppFileCode", [macro $v{code.replace("//__EMBEDDED_SETUP__", data)}], pos);
		return null;
	}
}
#end
