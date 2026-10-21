#include "hxcpp_JIT.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Interpreter/Interpreter.h"
#include "clang/Interpreter/IncrementalExecutor.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#error "hxcpp_JIT only supports Windows."
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::string moduleDir() {
  std::string p;
  HMODULE h = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(&moduleDir), &h))
    return "";
  char buf[4096];
  DWORD n = GetModuleFileNameA(h, buf, sizeof(buf));
  p.assign(buf, n);
  auto pos = p.find_last_of("/\\");
  return pos == std::string::npos ? "" : p.substr(0, pos);
}

static std::string findResourceDir() {
  namespace fs = std::filesystem;
  if (const char* env = std::getenv("HXCPP_JIT_RESOURCE_DIR")) return env;
  std::string dir = moduleDir();
  if (!dir.empty()) {
    fs::path cand = fs::path(dir) / "clang-resources";
    std::error_code ec;
    if (fs::exists(cand / "include", ec)) return cand.string();
  }
#ifdef HXCPP_JIT_CLANG_RESOURCE_DIR
  return HXCPP_JIT_CLANG_RESOURCE_DIR;
#else
  return "";
#endif
}

struct JitValue {
  int         kind = JIT_VOID;
  long long   i    = 0;
  double      d    = 0;
  void*       p    = nullptr;
  char*       s    = nullptr;
  JitValue() = default;
  JitValue(const JitValue&) = delete;
  JitValue& operator=(const JitValue&) = delete;
  ~JitValue() { std::free(s); }
};

typedef void (*ThunkFn)(JitValue* args, JitValue* ret, int count);

struct SessionLife {
  JitEngine* engine = nullptr;
};

struct JitFn {
  ThunkFn thunk = nullptr;
  std::shared_ptr<SessionLife> life;
};

struct JitEngine {
  std::shared_ptr<SessionLife> life = std::make_shared<SessionLife>();
  std::unique_ptr<clang::Interpreter> interp;
  std::string                         diagText;
  std::unique_ptr<llvm::raw_string_ostream> diagStream;
  std::string                         lastError;
  unsigned                            thunkCounter = 0;
  std::unordered_map<std::string, ThunkFn> getCache;
  std::unordered_map<std::string, ThunkFn> setCache;

  ~JitEngine() {
    life->engine = nullptr;
    interp.reset();
  }
};

static thread_local std::string g_createError;

static const char* kPrelude = R"PRELUDE(
extern "C" void jit_value_set_string(struct JitValue*, const char*);

struct JitValue {
  int         kind = 0;
  long long   i    = 0;
  double      d    = 0;
  void*       p    = nullptr;
  char*       s    = nullptr;
};

namespace __jit {

using size_t = __SIZE_TYPE__;

template <class T, T... V> struct integer_sequence {};
template <size_t... I> using index_sequence = integer_sequence<size_t, I...>;
template <size_t N> using make_index_sequence = __make_integer_seq<integer_sequence, size_t, N>;
template <class... A> using index_sequence_for = make_index_sequence<sizeof...(A)>;

template <class T> struct always_false { static constexpr bool value = false; };

template <class U> struct string_conv { static constexpr bool enabled = false; };
template <bool B, class T, class F> struct cond { using type = T; };
template <class T, class F> struct cond<false, T, F> { using type = F; };
template <class T> using bare = __remove_cv(__remove_reference_t(T));
template <class T> struct numeric {
  static constexpr bool value = __is_integral(T) || __is_floating_point(T) || __is_enum(T);
};
template <class T> struct numeric_temp {
  static constexpr bool value = __is_reference(T) && numeric<bare<T>>::value &&
    (__is_const(__remove_reference_t(T)) || __is_rvalue_reference(T));
};
template <class T> using get_ret =
  typename cond<string_conv<bare<T>>::enabled || numeric_temp<T>::value, bare<T>, T>::type;

inline void fail(JitValue& r, const char* message) {
  jit_value_set_string(&r, message);
  r.kind = -2;
}

template <class T> bool accepts(const JitValue& v) {
  using U = bare<T>;
  if constexpr (string_conv<U>::enabled) {
    return v.kind == 4;
  } else if constexpr (__is_reference(T)) {
    if constexpr (numeric_temp<T>::value)
      return v.kind == 1 || v.kind == 2 || (v.kind == 3 && v.p);
    return v.kind == 3 && v.p;
  } else if constexpr (numeric<U>::value) {
    return v.kind == 1 || v.kind == 2;
  } else if constexpr (__is_pointer(U)) {
    if constexpr (__is_same(U, const char*) || __is_same(U, char*))
      return v.kind == 3 || v.kind == 4;
    return v.kind == 3;
  } else {
    return v.kind == 3 && v.p;
  }
}

template <class... A, size_t... I>
bool validate(JitValue* a, JitValue* r, int n, index_sequence<I...>) {
  if (n != sizeof...(A)) { fail(*r, "Incorrect number of C++ arguments"); return false; }
  if (n && !a) { fail(*r, "Missing C++ argument array"); return false; }
  if (!(accepts<A>(a[I]) && ...)) {
    fail(*r, "Incompatible C++ argument (mutable references require a non-null CppPointer)");
    return false;
  }
  return true;
}

template <class T>
get_ret<T> get(JitValue& v) {
  using U = __remove_cv(__remove_reference_t(T));
  if constexpr (string_conv<U>::enabled) {
    return string_conv<U>::get(v);
  } else if constexpr (numeric_temp<T>::value) {
    if (v.kind == 3) return *reinterpret_cast<U*>(v.p);
    return v.kind == 2 ? (U)v.d : (U)v.i;
  } else if constexpr (__is_reference(T)) {
    return static_cast<T>(*reinterpret_cast<U*>(v.p));
  } else if constexpr (__is_same(U, bool)) {
    return v.kind == 2 ? v.d != 0 : v.i != 0;
  } else if constexpr (__is_pointer(U)) {
    if constexpr (__is_same(U, const char*) || __is_same(U, char*)) {
      if (v.kind == 4) return (U)v.s;
    }
    return (U)v.p;
  } else if constexpr (__is_floating_point(U)) {
    return v.kind == 2 ? (T)v.d : (T)v.i;
  } else if constexpr (__is_integral(U) || __is_enum(U)) {
    return v.kind == 2 ? (T)v.d : (T)v.i;
  } else {
    return *reinterpret_cast<U*>(v.p);
  }
}

template <class T>
void put(JitValue& r, T&& x) {
  using U = __remove_cv(__remove_reference_t(T));
  if constexpr (string_conv<U>::enabled) {
    string_conv<U>::put(r, x);
  } else if constexpr (__is_same(U, const char*) || __is_same(U, char*)) {
    if (x) jit_value_set_string(&r, x); else { r.kind = 3; r.p = nullptr; }
  } else if constexpr (__is_same(U, bool)) {
    r.kind = 5; r.i = x ? 1 : 0;
  } else if constexpr (__is_floating_point(U)) {
    r.kind = 2; r.d = (double)x;
  } else if constexpr (__is_integral(U) || __is_enum(U)) {
    r.kind = 1; r.i = (long long)x;
  } else if constexpr (__is_pointer(U)) {
    r.kind = 3; r.p = (void*)x;
  } else {
    r.kind = -1; 
  }
}

template <class R, class... A, size_t... I>
void call_fp_impl(R (*f)(A...), JitValue* a, JitValue* r, int n, index_sequence<I...>) {
  if (!validate<A...>(a, r, n, index_sequence<I...>{})) return;
  if constexpr (__is_void(R)) { f(get<A>(a[I])...); r->kind = 0; }
  else                        { put(*r, f(get<A>(a[I])...)); }
}
template <class R, class... A>
void call_fp(R (*f)(A...), JitValue* a, JitValue* r, int n) {
  call_fp_impl(f, a, r, n, index_sequence_for<A...>{});
}

template <class C, class R, class... A, size_t... I>
void call_obj_impl(C& c, R (C::*)(A...) const, JitValue* a, JitValue* r, int n, index_sequence<I...>) {
  if (!validate<A...>(a, r, n, index_sequence<I...>{})) return;
  if constexpr (__is_void(R)) { c(get<A>(a[I])...); r->kind = 0; }
  else                        { put(*r, c(get<A>(a[I])...)); }
}
template <class C, class R, class... A>
void call_obj(C& c, R (C::*m)(A...) const, JitValue* a, JitValue* r, int n) {
  call_obj_impl(c, m, a, r, n, index_sequence_for<A...>{});
}
template <class C, class R, class... A, size_t... I>
void call_obj_impl(C& c, R (C::*)(A...), JitValue* a, JitValue* r, int n, index_sequence<I...>) {
  if (!validate<A...>(a, r, n, index_sequence<I...>{})) return;
  if constexpr (__is_void(R)) { c(get<A>(a[I])...); r->kind = 0; }
  else                        { put(*r, c(get<A>(a[I])...)); }
}
template <class C, class R, class... A>
void call_obj(C& c, R (C::*m)(A...), JitValue* a, JitValue* r, int n) {
  call_obj_impl(c, m, a, r, n, index_sequence_for<A...>{});
}

template <class F>
void invoke(F&& f, JitValue* a, JitValue* r, int n) {
  using D = __decay(F);
  if constexpr (__is_pointer(D) && __is_function(__remove_pointer(D))) {
    call_fp(static_cast<D>(f), a, r, n);
  } else {
    using FC = __remove_reference_t(F);
    call_obj(f, &FC::operator(), a, r, n);
  }
}

template <class T, class V>
void assign(T& dst, V& v, JitValue& r) {
  if (!accepts<T>(v)) { fail(r, "Incompatible value for C++ assignment"); return; }
  dst = get<T>(v);
}

}
)PRELUDE";

static const char* kStdString = R"PRELUDE(
#include <string>
namespace __jit {
template <> struct string_conv<std::string> {
  static constexpr bool enabled = true;
  static std::string get(JitValue& v) { return v.kind == 4 && v.s ? std::string(v.s) : std::string(); }
  static void put(JitValue& r, const std::string& x) { jit_value_set_string(&r, x.c_str()); }
};
}
)PRELUDE";

static std::once_flag g_llvmInit;

void hxcpp_JIT_register_runtime_symbols();
void hxcpp_JIT_configure_builder(llvm::orc::LLJITBuilder&);

static std::string errToString(llvm::Error E) {
  return llvm::toString(std::move(E));
}

static bool foundIn(clang::DeclContext* DC, clang::NamedDecl* ND) {
  if (!DC) return false;
  for (clang::NamedDecl* X : DC->noload_lookup(ND->getDeclName()))
    if (X->getCanonicalDecl() == ND->getCanonicalDecl()) return true;
  return false;
}

static bool visibleInLookup(clang::Sema&, clang::NamedDecl* ND) {
  return foundIn(ND->getDeclContext()->getRedeclContext(), ND);
}

static bool inIdResolver(clang::Sema& S, clang::NamedDecl* ND) {
  for (auto I = S.IdResolver.begin(ND->getDeclName()), E = S.IdResolver.end(); I != E; ++I)
    if ((*I)->getCanonicalDecl() == ND->getCanonicalDecl()) return true;
  return false;
}

static bool g_traceRepair = std::getenv("HXCPP_JIT_TRACE_REPAIR") != nullptr;
static int  g_repScanned = 0, g_repInvalid = 0, g_repFixed = 0;

static void ensureVisible(clang::Sema& S, clang::NamedDecl* ND, bool useId, const char* why) {
  bool dcOk = visibleInLookup(S, ND);
  bool idOk = useId ? inIdResolver(S, ND) : true;
  if (dcOk && idOk) return;
  ++g_repFixed;
  if (g_traceRepair)
    std::fprintf(stderr, "hxcpp_JIT repair: restoring '%s' (%s) dcLookup=%d idResolver=%d\n",
                 ND->getNameAsString().c_str(), why, (int)dcOk, (int)idOk);
  if (!dcOk) ND->getDeclContext()->getPrimaryContext()->makeDeclVisibleInContext(ND);
  if (!idOk) {
    S.IdResolver.tryAddTopLevelDecl(ND, ND->getDeclName());
    if (S.TUScope) S.TUScope->AddDecl(ND);
  }
}

static clang::FunctionDecl* newestValid(clang::FunctionDecl* FD) {
  clang::FunctionDecl* D = FD->getMostRecentDecl();
  while (D && D->isInvalidDecl()) D = D->getPreviousDecl();
  return D;
}

static void restoreFunctionLookup(clang::Sema& S, clang::DeclContext* DC, bool useId) {
  for (clang::Decl* D : DC->noload_decls()) {
    if (auto* NS = llvm::dyn_cast<clang::NamespaceDecl>(D)) {
      for (clang::NamespaceDecl* R : NS->redecls()) restoreFunctionLookup(S, R, false);
      continue;
    }
    if (auto* LS = llvm::dyn_cast<clang::LinkageSpecDecl>(D)) {
      restoreFunctionLookup(S, LS, useId);
      continue;
    }
    auto* FD = llvm::dyn_cast<clang::FunctionDecl>(D);
    if (!FD || FD->isImplicit()) continue;
    ++g_repScanned;
    if (FD->isInvalidDecl()) ++g_repInvalid;
    clang::FunctionDecl* Keep = newestValid(FD);
    if (Keep && !Keep->isImplicit()) ensureVisible(S, Keep, useId, "function");
  }
}

template <class TU>
static auto collectTUs(TU* T, int) -> decltype(T->redecls(), std::vector<TU*>()) {
  std::vector<TU*> v;
  for (auto* R : T->redecls()) v.push_back(R);
  return v;
}
template <class TU>
static std::vector<TU*> collectTUs(TU* T, long) { return {T}; }

static void repairAfterFailure(JitEngine* e) {
  auto* CI = e->interp->getCompilerInstance();
  if (!CI || !CI->hasSema()) return;
  clang::Sema& S = CI->getSema();
  g_repScanned = g_repInvalid = g_repFixed = 0;
  auto* TU = S.getASTContext().getTranslationUnitDecl();
  auto tus = collectTUs(TU, 0);
  bool useId = tus.size() == 1;
  for (auto* T : tus) restoreFunctionLookup(S, T, useId);
  if (g_traceRepair)
    std::fprintf(stderr, "hxcpp_JIT repair: tus=%zu scanned=%d invalid=%d restored=%d\n", tus.size(),
                 g_repScanned, g_repInvalid, g_repFixed);
}

static int runCode(JitEngine* e, const std::string& code) {
  e->diagText.clear();
  if (auto Err = e->interp->ParseAndExecute(code)) {
    std::string msg = errToString(std::move(Err));
    e->diagStream->flush();
    e->lastError = e->diagText.empty() ? msg : e->diagText;
    repairAfterFailure(e);
    return 1;
  }
  e->lastError.clear();
  return 0;
}

static void* lookup(JitEngine* e, const std::string& name) {
  auto Addr = e->interp->getSymbolAddress(name);
  if (!Addr) {
    e->lastError = "symbol lookup failed for '" + name + "': " + errToString(Addr.takeError());
    return nullptr;
  }
  return reinterpret_cast<void*>(Addr->getValue());
}

static ThunkFn makeThunk(JitEngine* e, const std::string& body) {
  std::string name = "__jit_thunk_" + std::to_string(e->thunkCounter++);
  std::string code = "extern \"C\" void " + name +
                     "(JitValue* __a, JitValue* __r, int __n) { " + body + " }\n";
  if (runCode(e, code)) return nullptr;
  return reinterpret_cast<ThunkFn>(lookup(e, name));
}

static constexpr size_t kThunkCacheMax = 4096;

static ThunkFn cachedThunk(JitEngine* e, std::unordered_map<std::string, ThunkFn>& cache,
                           const char* key, const std::string& body) {
  auto it = cache.find(key);
  if (it != cache.end()) { e->lastError.clear(); return it->second; }
  ThunkFn t = makeThunk(e, body);
  if (t) {
    if (cache.size() >= kThunkCacheMax) cache.clear();
    cache.emplace(key, t);
  }
  return t;
}

extern "C" {

JitEngine* jit_create(const char* const* extra_args, int n_args) {
  std::call_once(g_llvmInit, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    hxcpp_JIT_register_runtime_symbols();
  });

  std::vector<std::string> owned;
  bool hasStd = false, hasResDir = false, hasTemplateParsing = false;
  bool hasVecAlgos = false, hasVecFloatAlgos = false;
  for (int i = 0; i < n_args; ++i) {
    owned.emplace_back(extra_args[i]);
    if (owned.back().find("_USE_STD_VECTOR_ALGORITHMS") != std::string::npos) hasVecAlgos = true;
    if (owned.back().find("_USE_STD_VECTOR_FLOATING_ALGORITHMS") != std::string::npos) hasVecFloatAlgos = true;
    if (owned.back().rfind("-std=", 0) == 0) hasStd = true;
    if (owned.back() == "-resource-dir") hasResDir = true;
    if (owned.back().rfind("-fdelayed-template-parsing", 0) == 0 ||
        owned.back().rfind("-fno-delayed-template-parsing", 0) == 0)
      hasTemplateParsing = true;
  }
  if (!hasStd) owned.emplace_back("-std=c++20");
  if (!hasVecAlgos) owned.emplace_back("-D_USE_STD_VECTOR_ALGORITHMS=0");
  if (!hasVecFloatAlgos) owned.emplace_back("-D_USE_STD_VECTOR_FLOATING_ALGORITHMS=0");
  if (!hasTemplateParsing) owned.emplace_back("-fno-delayed-template-parsing");
  bool hasTsStatics = false;
  for (auto& a : owned)
    if (a.rfind("-fthreadsafe-statics", 0) == 0 || a.rfind("-fno-threadsafe-statics", 0) == 0) hasTsStatics = true;
  if (!hasTsStatics) owned.emplace_back("-fno-threadsafe-statics");
  if (!hasResDir) {
    std::string rd = findResourceDir();
    if (!rd.empty()) {
      owned.emplace_back("-resource-dir");
      owned.emplace_back(rd);
    }
  }
  std::vector<const char*> argv;
  for (auto& s : owned) argv.push_back(s.c_str());

  auto e = std::make_unique<JitEngine>();
  e->life->engine = e.get();
  e->diagStream = std::make_unique<llvm::raw_string_ostream>(e->diagText);

  clang::IncrementalCompilerBuilder B;
  B.SetCompilerArgs(argv);
  auto CI = B.CreateCpp();
  if (!CI) {
    g_createError = errToString(CI.takeError());
    return nullptr;
  }
  (*CI)->getDiagnostics().setClient(
      new clang::TextDiagnosticPrinter(*e->diagStream, (*CI)->getDiagnosticOpts()),
      true);

    auto IEB = std::make_unique<clang::IncrementalExecutorBuilder>();
#ifdef _MSC_VER
  {
    auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!JTMB) {
      g_createError = errToString(JTMB.takeError());
      return nullptr;
    }
    auto JB = std::make_unique<llvm::orc::LLJITBuilder>();
    JB->setJITTargetMachineBuilder(std::move(*JTMB));
    hxcpp_JIT_configure_builder(*JB);
    IEB->JITBuilder = std::move(JB);
  }
#endif
  auto Interp = clang::Interpreter::create(std::move(*CI), std::move(IEB));
  if (!Interp) {
    g_createError = errToString(Interp.takeError());
    return nullptr;
  }
  e->interp = std::move(*Interp);

  if (runCode(e.get(), kPrelude)) {
    g_createError = "prelude failed: " + e->lastError;
    return nullptr;
  }
  return e.release();
}

static std::vector<std::string> splitLines(const char* s) {
  std::vector<std::string> out;
  if (!s || !*s) return out;
  std::string cur;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') { out.push_back(cur); cur.clear(); } else cur += *p;
  }
  out.push_back(cur);
  return out;
}

JitEngine* jit_create_args(const char* args) {
  auto v = splitLines(args);
  std::vector<const char*> p;
  for (auto& s : v) p.push_back(s.c_str());
  return jit_create(p.data(), (int)p.size());
}

JitFn* jit_bind_str(JitEngine* e, const char* name, const char* ret_type, const char* arg_types) {
  auto v = splitLines(arg_types);
  std::vector<const char*> p;
  for (auto& s : v) p.push_back(s.c_str());
  return jit_bind(e, name, ret_type, p.data(), (int)p.size());
}

void jit_destroy(JitEngine* e) { delete e; }

const char* jit_llvm_version(void) { return LLVM_VERSION_STRING; }

const char* jit_last_error(JitEngine* e) {
  return e ? e->lastError.c_str() : g_createError.c_str();
}

int jit_eval(JitEngine* e, const char* code) { return runCode(e, code); }

int jit_enable_std_string(JitEngine* e) { return runCode(e, kStdString); }

int jit_get(JitEngine* e, const char* expr, JitValue* out) {
  ThunkFn t = cachedThunk(e, e->getCache, expr, std::string("__jit::put(*__r, (") + expr + "));");
  if (!t) return 1;
  t(nullptr, out, 0);
  return 0;
}

int jit_set(JitEngine* e, const char* lvalue, JitValue* value) {
  ThunkFn t = cachedThunk(e, e->setCache, lvalue, std::string("__jit::assign(") + lvalue + ", __a[0], *__r);");
  if (!t) return 1;
  JitValue result;
  t(value, &result, 1);
  if (result.kind == -2) { e->lastError = result.s ? result.s : "Invalid assignment"; return 1; }
  return 0;
}

void* jit_symbol(JitEngine* e, const char* ir_name) { return lookup(e, ir_name); }

JitFn* jit_bind(JitEngine* e, const char* name, const char* ret_type,
                const char* const* arg_types, int n_args) {
  std::string callee;
  if (ret_type) {
    callee = std::string("(") + ret_type + "(*)(";
    for (int i = 0; i < n_args; ++i) callee += (i ? "," : "") + std::string(arg_types[i]);
    callee += std::string("))") + name;
  } else {
    callee = name;
  }
  ThunkFn t = makeThunk(e, "__jit::invoke(" + callee + ", __a, __r, __n);");
  if (!t) return nullptr;
  auto* f = new JitFn();
  f->thunk = t;
  f->life = e->life;
  return f;
}

const char* jit_fn_last_error(JitFn* f) {
  if (!f || !f->life->engine) return "C++ JIT session has been disposed";
  return f->life->engine->lastError.c_str();
}

int jit_call(JitFn* f, JitValue* args, int count, JitValue* ret) {
  if (!f || !f->life->engine || !ret) return 1;
  auto* e = f->life->engine;
  f->thunk(args, ret, count);
  if (ret->kind == -2) {
    e->lastError = ret->s ? ret->s : "Invalid C++ call";
    return 1;
  }
  e->lastError.clear();
  return 0;
}

void jit_fn_free(JitFn* f) { delete f; }

JitValue*   jit_value_new(int count) { return new JitValue[count > 0 ? count : 1]; }
void        jit_value_free(JitValue* v) { delete[] v; }
JitValue*   jit_value_at(JitValue* v, int index) { return v + index; }

void jit_value_set_int(JitValue* v, long long x)   { v->kind = JIT_INT;    v->i = x; }
void jit_value_set_double(JitValue* v, double x)   { v->kind = JIT_DOUBLE; v->d = x; }
void jit_value_set_ptr(JitValue* v, void* p)       { v->kind = JIT_PTR;    v->p = p; }
void jit_value_set_string(JitValue* v, const char* s) {
  if (!s) s = "";
  size_t n = std::strlen(s);
  char* copy = static_cast<char*>(std::malloc(n + 1));
  if (copy) std::memcpy(copy, s, n + 1);
  std::free(v->s);
  v->s = copy;
  v->kind = JIT_STRING;
}

void jit_value_set_addr(JitValue* v, double a) { v->kind = JIT_PTR; v->p = reinterpret_cast<void*>((unsigned long long)a); }
double jit_value_get_addr(JitValue* v) { return (double)(unsigned long long)v->p; }

int         jit_value_kind(JitValue* v)       { return v->kind; }
long long   jit_value_get_int(JitValue* v)    { return v->kind == JIT_DOUBLE ? (long long)v->d : v->i; }
double      jit_value_get_double(JitValue* v) { return v->kind == JIT_INT ? (double)v->i : v->d; }
void*       jit_value_get_ptr(JitValue* v)    { return v->p; }
const char* jit_value_get_string(JitValue* v) { return v->s ? v->s : ""; }

}
