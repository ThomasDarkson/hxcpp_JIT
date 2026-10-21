package macros;

import sys.io.File;
#if macro
import haxe.macro.Compiler;
import haxe.macro.Context;
import sys.FileSystem;
import haxe.io.Path;
import haxe.macro.Expr;

using StringTools;
using haxe.macro.PositionTools;

class DLL {
    public static function moveDLL() {
        if (Context.defined("display") || Context.defined("display-details") || Context.defined("completion") || Context.defined("no-output") || !Context.defined("cpp"))
            return Context.getBuildFields();
        var output = Compiler.getOutput();
        if (output == null || output == "" || output == "_")
            return Context.getBuildFields();
        if (Sys.systemName() == "Windows") {
            var _pos =  Context.currentPos();
            var _pos_info = _pos.getInfos();
            var _class = Context.getLocalClass();

            var _source_path = Path.directory(_pos_info.file);
            if( !Path.isAbsolute(_source_path) ) {
                _source_path = Path.join([Sys.getCwd(), _source_path]);
            }
            _source_path = Path.normalize(Path.join([_source_path, '../../']));

            if (!_source_path.endsWith("/"))
                _source_path += "/";

            _source_path = _source_path.replace("/", "\\");

            var path = (_source_path + ("build/hxcpp_JIT.dll")).replace("/", "\\");
            var cwd = Sys.getCwd().replace("/", "\\");
            var out = Compiler.getOutput().replace("/", "\\");
            #if openfl
            if (out.endsWith("obj/")) {
                out = out.substring(0, out.length - 4);
                out += "bin/";
            }
            else if (out.endsWith("obj")) {
                out = out.substring(0, out.length - 3);
                out += "bin";
            }
            #end
            if (cwd.endsWith("\\"))
                cwd += out;
            else
                cwd += "\\" + out;

            if (!cwd.endsWith("\\"))
                cwd += "\\";

            cwd = cwd.replace("/", "\\");
            if (!FileSystem.exists(cwd))
                FileSystem.createDirectory(cwd);
            Sys.command('cmd.exe /c copy /Y "$path" "$cwd"');
        }
        return Context.getBuildFields();
    }
}
#end
