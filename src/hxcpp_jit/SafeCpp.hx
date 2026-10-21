package hxcpp_jit;

/**
    `hxcpp_JIT.Cpp` but exceptions are caught with `lastError`.

    `onError` can be set to be called every time an error is caught.
    It receives the error, the session, the name of the method that failed and the arguments it was called with.

    Example:

    ```haxe
    var cpp = new SafeCpp();
    cpp.onError = (error, cpp, method, args) -> {
        trace(method); // eval
        trace(args); // [int i = zero;,null]
        trace(error);
        // In file included from <<< inputs >>>:1:
        // input_line_3:1:9: error: use of undeclared identifier 'zero'
        //  1 | int i = zero;
        //    |         ^~~~
    }
    cpp.eval("int i = zero;");
    ```

    Using SafeCpp is usually slower than using Cpp, because SafeCpp 
    wraps everything in a try/catch block.
**/
class SafeCpp {
    public var cpp(default, null):Cpp;
    public var lastError(default, null):CppError;
    /**
        If not null, called every time an exception is caught.

        The arguments are the error, the underlying session (`null` if it could not be created),
        and the name of the method that threw: `new`, `eval`, `enableStdString`, `get`, `set`, `bind`,
        `call`, `address` or `dispose`. Errors from a function returned by `bind` use
        `SafeCppFunction.call` and `SafeCppFunction.dispose`.

        The last argument holds the arguments the method was called with, in order, as written in its signature.
        For example `eval("int i = zero;")` gives `["int i = zero;", null]`, `set("x", 1, true)` gives `["x", 1, true]`,
        and `call("f", [1, 2])` gives `["f", [1, 2]]`. Methods without arguments, like `dispose`, give an empty array.
    **/
    public var onError:CppError->Cpp->String->Array<Dynamic>->Void;

    public function new(?args:Array<String>, stdString:Bool = true) {
        cpp = attempt("new", [args, stdString], () -> new Cpp(args, stdString));
    }

    public function eval(source:String, ?isFile:Bool):Void {
        attempt("eval", [source, isFile], () -> {
            requireCpp().eval(source, isFile);
            return null;
        });
    }

    public function enableStdString():Void {
        attempt("enableStdString", [], () -> {
            requireCpp().enableStdString();
            return null;
        });
    }

    public function get(expr:String):Dynamic {
        return attempt("get", [expr], () -> requireCpp().get(expr));
    }


    /**
        If `autoDeclare` is `true`, `set` creates the variable when it doesn't exist yet.

        The variable is declared with a C++ type taken from the first value, then set:

        | Haxe value | C++ type |
        |---|---|
        | `Bool` | `bool` |
        | `Int` | `int` |
        | `Float` | `double` |
        | `String` | `std::string` (needs `stdStringEnabled`) |
        | `CppPointer` or `null` | `void*` |

        ```haxe
        var cpp = new SafeCpp();
        cpp.set("x", 64, true); // Same as cpp.eval("int x{};") followed by cpp.set("x", 64)
        trace(cpp.get("x")); // 64
        ```

        Only plain names are created. Names like `point.x` or `arr[2]` still need the variable to be declared with `eval`.
        Later calls convert to the variable's type like a C++ assignment, so setting `x` to `3.7` stores `3`.
        Haxe can't tell `64` from `64.0` at runtime, so both create an `int`. Declare the variable with `eval` for any other type.
    **/
    public function set(lvalue:String, value:Dynamic, autoDeclare:Bool = false):Void {
        attempt("set", [lvalue, value, autoDeclare], () -> {
            var session = requireCpp();
            try {
                session.set(lvalue, value);
            } catch (e:CppError) {
                if (!autoDeclare || !isUndeclared(e, lvalue))
                    throw e;
                session.eval(declarationFor(session, lvalue, value));
                session.set(lvalue, value);
            }
            return null;
        });
    }

    static final plainName = ~/^[A-Za-z_][A-Za-z0-9_]*$/;

    static function isUndeclared(e:CppError, name:String):Bool {
        return plainName.match(name) && e.message.indexOf("undeclared identifier") >= 0;
    }

    static function declarationFor(session:Cpp, name:String, value:Dynamic):String {
        var type:String;
        var ttype = Type.typeof(value);
        if (value == null || Std.isOfType(value, CppPointer))
            type = "void*";
        else if (ttype == TBool)
            type = "bool";
        else if (ttype == TInt)
            type = "int";
        else if (ttype == TFloat)
            type = "double";
        else if (Std.isOfType(value, String)) {
            if (!session.stdStringEnabled)
                throw new CppError("Can't create a string variable without std::string support (see enableStdString)");
            type = "std::string";
        } else
            throw new CppError("Can't create a variable from a value of type " + Std.string(Type.typeof(value)));
        return type + " " + name + "{};";
    }

    /**
        Returns a safe bound function whose errors also update this.lastError.
        The return type is SafeCppFunction, rather than the raw CppFunction.
    **/
    public function bind(name:String, ?ret:String, ?argTypes:Array<String>):SafeCppFunction {
        return attempt("bind", [name, ret, argTypes], () -> new SafeCppFunction(this, requireCpp().bind(name, ret, argTypes)));
    }

    public function call(name:String, ?args:Array<Dynamic>):Dynamic {
        return attempt("call", [name, args], () -> requireCpp().call(name, args));
    }

    public function address(name:String):CppPointer {
        return attempt("address", [name], () -> requireCpp().address(name));
    }

    public function dispose():Void {
        attempt("dispose", [], () -> {
            if (cpp != null) {
                cpp.dispose();
                cpp = null;
            }
            return null;
        });
    }

    @:allow(hxcpp_jit) function requireCpp():Cpp {
        if (cpp == null)
            throw new CppError("C++ JIT session is unavailable (creation failed or it was disposed)");
        return cpp;
    }

    @:allow(hxcpp_jit) function attempt<T>(method:String, arguments:Array<Dynamic>, operation:Void->T):Null<T> {
        try {
            return operation();
        } catch (e:CppError) {
            lastError = e;
        } catch (e:haxe.Exception) {
            lastError = new CppError(e.message, e);
        } catch (e:Dynamic) {
            lastError = new CppError(Std.string(e));
        }
        if (onError != null) onError(lastError, cpp, method, arguments != null ? arguments : []);
        return null;
    }
}

/**
    A bound function returned by SafeCpp.bind.
    Available as hxcpp_jit.SafeCpp.SafeCppFunction.
    Calls and disposal update the owning SafeCpp.lastError.
**/
class SafeCppFunction {
    var owner:SafeCpp;
    var fn:CppFunction;

    public var lastError(get, never):CppError;

    @:allow(hxcpp_jit.SafeCpp) function new(owner:SafeCpp, fn:CppFunction) {
        this.owner = owner;
        this.fn = fn;
    }

    function get_lastError():CppError {
        return owner.lastError;
    }

    public function call(?args:Array<Dynamic>):Dynamic {
        return owner.attempt("SafeCppFunction.call", [args], () -> {
            owner.requireCpp();
            if (fn == null)
                throw new CppError("C++ function has been disposed");
            return fn.call(args);
        });
    }

    public function dispose():Void {
        owner.attempt("SafeCppFunction.dispose", [], () -> {
            if (fn != null) {
                fn.dispose();
                fn = null;
            }
            return null;
        });
    }
}