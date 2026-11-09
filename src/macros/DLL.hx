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
        if (Backend.isLinux()) {
            Backend.useMinGW();
            var nativeFile = FileSystem.fullPath(Context.resolvePath("hxcpp_jit/Native.hx"));
            var root = Path.directory(Path.directory(Path.directory(nativeFile)));
            var backendDir = Context.definedValue("hxcpp_JIT_lib");
            if (backendDir == null) backendDir = Sys.getEnv("HXCPP_JIT_LIB_DIR");
            if (backendDir == null) backendDir = Path.join([root, "build-linux"]);
            for (entry in ["libhxcpp_JIT.so", "libgcc_s.so.1", "linux/include/c++/string", "linux/include/stdio.h", "clang-resources/include", "licenses"])
                if (!FileSystem.exists(Path.join([backendDir, entry])))
                    Context.fatalError('Linux bundle is incomplete: $backendDir/$entry. Run scripts/build-linux.sh.', Context.currentPos());
            var out = Path.removeTrailingSlashes(output);
            if (Context.defined("openfl") && Path.withoutDirectory(out) == "obj")
                out = Path.join([Path.directory(out), "bin"]);
            FileSystem.createDirectory(out);
            for (entry in ["libhxcpp_JIT.so", "libgcc_s.so.1"])
                File.copy(Path.join([backendDir, entry]), Path.join([out, entry]));
            for (entry in ["linux", "clang-resources", "licenses"])
                copyDir(Path.join([backendDir, entry]), Path.join([out, entry]));
            return Context.getBuildFields();
        }
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

            var backendDir = Context.definedValue("hxcpp_JIT_lib");
            if (backendDir == null) backendDir = Sys.getEnv("HXCPP_JIT_LIB_DIR");
            if (backendDir == null)
                backendDir = _source_path + (Backend.useMinGW() ? "build-mingw" : "build");
            backendDir = Path.normalize(backendDir).replace("/", "\\");
            if (!backendDir.endsWith("\\")) backendDir += "\\";
            if (Backend.useMinGW()) {
                if (!FileSystem.exists(backendDir + "mingw\\include\\windows.h") &&
                    !FileSystem.exists(backendDir + "mingw\\x86_64-w64-mingw32\\include\\windows.h"))
                    Context.fatalError('MinGW Windows headers are missing from $backendDir', Context.currentPos());
                for (entry in ["hxcpp_JIT.dll", "hxcpp_JIT.lib", "mingw-backend.txt", "mingw\\include\\c++\\v1\\string", "clang-resources\\include"]) {
                    if (!FileSystem.exists(backendDir + entry))
                        Context.fatalError('MinGW backend is incomplete: $backendDir$entry. Run scripts/build-mingw.ps1.', Context.currentPos());
                }
            }
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
            if (Path.isAbsolute(out))
                cwd = out;
            else if (cwd.endsWith("\\"))
                cwd += out;
            else
                cwd += "\\" + out;

            if (!cwd.endsWith("\\"))
                cwd += "\\";

            cwd = cwd.replace("/", "\\");
            if (!FileSystem.exists(cwd))
                FileSystem.createDirectory(cwd);

            var dlls = Backend.useMinGW() ? ["hxcpp_JIT.dll", "libc++.dll", "libunwind.dll"] : ["hxcpp_JIT.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_codecvt_ids.dll", "msvcp140_atomic_wait.dll", "vccorlib140.dll", "concrt140.dll"];
            for (i in dlls) {
                var path = (backendDir + i).replace("/", "\\");
                if (FileSystem.exists(path))
                    Sys.command('cmd.exe /c copy /Y "$path" "$cwd"');
            }

            var resSrc = backendDir + "clang-resources\\include";
            if (Backend.useMinGW())
                copyDir(backendDir + "mingw", cwd + "mingw");

            if (!FileSystem.exists(resSrc) || !FileSystem.isDirectory(resSrc))
                resSrc = findClang(_source_path);

            if (resSrc != null && FileSystem.exists(resSrc) && FileSystem.isDirectory(resSrc)) {
                var resDst = cwd + "clang-resources\\include";
                copyDir(resSrc, resDst);
            }
        }
        return Context.getBuildFields();
    }

    static function copyDir(src:String, dst:String) {
        if (!FileSystem.exists(dst))
            FileSystem.createDirectory(dst);
        for (entry in FileSystem.readDirectory(src)) {
            var s = Path.join([src, entry]);
            var d = Path.join([dst, entry]);
            if (FileSystem.isDirectory(s))
                copyDir(s, d);
            else
                File.copy(s, d);
        }
    }

    static function findClang(sourcePath:String):String {
        var clangDir = sourcePath + "llvm-build\\lib\\clang";
        if (!FileSystem.exists(clangDir) || !FileSystem.isDirectory(clangDir))
            return null;
        for (entry in FileSystem.readDirectory(clangDir)) {
            var versionDir = clangDir + "\\" + entry;
            if (!FileSystem.isDirectory(versionDir))
                continue;
            var include = versionDir + "\\include";
            if (FileSystem.exists(include) && FileSystem.isDirectory(include))
                return include;
        }
        return null;
    }
}
#end
