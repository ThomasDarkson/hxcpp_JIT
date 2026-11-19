package macros;

import sys.io.File;
#if macro
import haxe.macro.Compiler;
import haxe.macro.Context;
import sys.FileSystem;
import haxe.io.Path;
import sys.io.Process;
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
                copyIfDifferent(Path.join([backendDir, entry]), Path.join([out, entry]));
            for (entry in ["linux", "clang-resources", "licenses"])
                copyDir(Path.join([backendDir, entry]), Path.join([out, entry]));
            flush();
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
                for (entry in ["hxcpp_JIT.dll", "hxcpp_JIT.lib", "mingw\\include\\c++\\v1\\string", "clang-resources\\include"]) {
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
                    copyIfDifferent(path, cwd + i);
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
            flush();
        }
        return Context.getBuildFields();
    }

    static var pending:Array<{src:String, dst:String}> = [];

    static function copyDir(src:String, dst:String) {
        if (!FileSystem.exists(dst))
            FileSystem.createDirectory(dst);
        for (entry in FileSystem.readDirectory(src)) {
            var s = Path.join([src, entry]);
            var d = Path.join([dst, entry]);
            if (FileSystem.isDirectory(s))
                copyDir(s, d);
            else
                copyIfDifferent(s, d);
        }
    }

    static function copyIfDifferent(src:String, dst:String):Void {
        pending.push({src: src, dst: dst});
    }

    static function flush():Void {
        var jobs = pending;
        pending = [];
        var sameSize = [];
        for (j in jobs) {
            if (!FileSystem.exists(j.dst) || FileSystem.isDirectory(j.dst) || FileSystem.stat(j.src).size != FileSystem.stat(j.dst).size)
                File.copy(j.src, j.dst);
            else
                sameSize.push(j);
        }
        if (sameSize.length == 0)
            return;
        var paths = [];
        for (j in sameSize) {
            paths.push(j.src);
            paths.push(j.dst);
        }
        var hashes = nativeSha256Batch(paths);
        for (j in sameSize) {
            var a = hashes.get(j.src);
            var b = hashes.get(j.dst);
            if (a == null || b == null || a != b)
                File.copy(j.src, j.dst);
        }
    }

    static function nativeSha256Batch(paths:Array<String>):Map<String, String> {
        var result = new Map<String, String>();
        var tmp = Sys.getEnv("TEMP");
        if (tmp == null) tmp = Sys.getEnv("TMPDIR");
        if (tmp == null) tmp = "/tmp";
        var stamp = Std.string(Std.int(Date.now().getTime() % 1000000000)) + "_" + Std.string(Std.random(1000000));
        var list = Path.join([tmp, "hxcpp_jit_hash_" + stamp + ".txt"]);
        var script = Path.join([tmp, "hxcpp_jit_hash_" + stamp + ".ps1"]);
        var windows = Sys.systemName() == "Windows";
        try {
            File.saveContent(list, paths.join("\n"));
            var p:Process;
            if (windows) {
                File.saveContent(script, "$ErrorActionPreference = 'SilentlyContinue'\n"
                    + "$sha = [System.Security.Cryptography.SHA256]::Create()\n"
                    + "foreach ($p in [System.IO.File]::ReadAllLines('" + list.replace("'", "''") + "')) {\n"
                    + "  try { $f = [System.IO.File]::OpenRead($p); try { [System.BitConverter]::ToString($sha.ComputeHash($f)).Replace('-', '') } finally { $f.Dispose() } } catch { '' }\n"
                    + "}\n");
                p = new Process("powershell", ["-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script]);
            } else {
                p = new Process("sh", ["-c", "xargs -d '\\n' sha256sum < \"$1\" 2>/dev/null", "sh", list]);
            }
            var out = p.stdout.readAll().toString();
            p.stderr.readAll();
            p.exitCode();
            p.close();
            var lines = out.split("\n");
            if (windows) {
                if (lines.length >= paths.length)
                    for (i in 0...paths.length) {
                        var h = lines[i].trim();
                        if (h.length == 64)
                            result.set(paths[i], h.toLowerCase());
                    }
            } else {
                for (line in lines)
                    if (line.length > 66 && line.charAt(64) == " ")
                        result.set(line.substr(66), line.substr(0, 64).toLowerCase());
            }
        } catch (e:Dynamic) {}
        try { FileSystem.deleteFile(list); } catch (e:Dynamic) {}
        try { if (windows) FileSystem.deleteFile(script); } catch (e:Dynamic) {}
        return result;
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
