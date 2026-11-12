#include "mingw_runtime.hpp"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Object/ObjectFile.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#if !defined(__MINGW32__) || !defined(__clang__) || !defined(_WIN64)
#error "The MinGW backend requires x64 LLVM-MinGW and matching LLVM libraries."
#endif

extern "C" void* __emutls_get_address(void*);

namespace fs = std::filesystem;
using namespace llvm;
using namespace llvm::orc;

fs::path hxcpp_JIT_mingw_root() {
  if (const wchar_t* root = _wgetenv(L"HXCPP_JIT_MINGW_ROOT")) return fs::path(root);
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&hxcpp_JIT_mingw_root), &module))
    throw std::runtime_error("Cannot locate the MinGW backend DLL");
  wchar_t path[32768];
  DWORD count = GetModuleFileNameW(module, path, 32768);
  if (!count || count >= 32768) throw std::runtime_error("Cannot locate the MinGW backend DLL");
  return fs::path(std::wstring(path, count)).parent_path() / "mingw";
}

namespace {
int mingwVsnprintf(char* destination, size_t capacity, const char* format, va_list args) {
  return std::vsnprintf(destination, capacity, format, args);
}
int mingwSnprintf(char* destination, size_t capacity, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = mingwVsnprintf(destination, capacity, format, args);
  va_end(args);
  return result;
}
auto snprintfImport = &mingwSnprintf;
auto vsnprintfImport = &mingwVsnprintf;

class MinGWMemoryManager : public SectionMemoryManager {
  static constexpr size_t capacity = size_t(128) << 20;
  uint8_t* base = nullptr;
  size_t used = 0;
  struct UnwindSection { uint8_t* address; size_t size; };
  std::vector<UnwindSection> unwindSections;
  std::map<std::string, size_t> originalUnwindSizes;
  std::vector<RUNTIME_FUNCTION> table;
  bool tableRegistered = false;
  std::mutex mutex;
  uint8_t* allocate(uintptr_t size, unsigned alignment) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!base) base = static_cast<uint8_t*>(VirtualAlloc(nullptr, capacity, MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!base) report_fatal_error("Cannot reserve MinGW JIT memory");
    size_t align = std::max<size_t>(alignment, 16);
    size_t offset = (used + align - 1) & ~(align - 1);
    size = std::max<uintptr_t>(size, 1);
    if (offset > capacity || size > capacity - offset)
      report_fatal_error("MinGW JIT object exceeds its 128 MiB allocation limit");
    if (!VirtualAlloc(base + offset, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
      report_fatal_error("Cannot commit MinGW JIT memory");
    used = offset + size;
    return base + offset;
  }
  void registerAllUnwindSections() {
    std::lock_guard<std::mutex> lock(mutex);
    if (tableRegistered || unwindSections.empty()) return;
    for (const auto& section : unwindSections) {
      if (section.size % sizeof(RUNTIME_FUNCTION))
        report_fatal_error("Invalid MinGW JIT unwind section size");
      auto* first = reinterpret_cast<RUNTIME_FUNCTION*>(section.address);
      table.insert(table.end(), first, first + section.size / sizeof(RUNTIME_FUNCTION));
    }
    if (table.empty()) return;
    std::sort(table.begin(), table.end(), [](const auto& a, const auto& b) { return a.BeginAddress < b.BeginAddress; });
    if (!RtlAddFunctionTable(table.data(), static_cast<DWORD>(table.size()), reinterpret_cast<DWORD64>(base)))
      report_fatal_error("Cannot register MinGW JIT unwind table");
    tableRegistered = true;
    for (const auto& entry : table) {
      if (entry.BeginAddress >= entry.EndAddress || entry.EndAddress > used)
        report_fatal_error("Invalid MinGW JIT unwind range");
      for (DWORD offset : {entry.BeginAddress, entry.EndAddress - 1}) {
        DWORD64 foundBase = 0;
        auto* found = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(base) + offset, &foundBase, nullptr);
        if (!found || foundBase != reinterpret_cast<DWORD64>(base))
          report_fatal_error("Windows cannot find a registered MinGW JIT helper unwind entry");
      }
    }
    if (std::getenv("HXCPP_JIT_TRACE_EH")) {
      std::fprintf(stderr, "MinGW unwind: base=%p sections=%zu functions=%zu (including COMDAT)\n",
          static_cast<void*>(base), unwindSections.size(), table.size());
      std::fflush(stderr);
    }
  }
public:
  explicit MinGWMemoryManager(std::map<std::string, size_t> sizes)
      : originalUnwindSizes(std::move(sizes)) {}
  uint8_t* allocateCodeSection(uintptr_t size, unsigned align, unsigned, StringRef) override {
    return allocate(size, align);
  }
  uint8_t* allocateDataSection(uintptr_t size, unsigned align, unsigned, StringRef name, bool) override {
    auto* address = allocate(size, align);
    if (size && (name == ".pdata" || name.starts_with(".pdata$"))) {
      std::lock_guard<std::mutex> lock(mutex);
      if (tableRegistered) report_fatal_error("MinGW unwind section allocated after finalization");
      auto original = originalUnwindSizes.find(name.str());
      if (original == originalUnwindSizes.end() || original->second > size)
        report_fatal_error("Cannot match MinGW unwind allocation to its original COFF section");
      unwindSections.push_back({address, original->second});
    }
    return address;
  }
  bool finalizeMemory(std::string* error = nullptr) override {
    registerAllUnwindSections();
    if (!FlushInstructionCache(GetCurrentProcess(), base, used)) {
      if (error) *error = "Cannot flush MinGW JIT instruction cache";
      return true;
    }
    return false;
  }
  void registerEHFrames(uint8_t*, uint64_t, size_t) override {
    registerAllUnwindSections();
  }
  void deregisterEHFrames() override {
    if (tableRegistered) RtlDeleteFunctionTable(table.data());
    tableRegistered = false;
    table.clear();
  }
  ~MinGWMemoryManager() override {
    deregisterEHFrames();
    if (base) VirtualFree(base, 0, MEM_RELEASE);
  }
};

class MinGWImports : public DefinitionGenerator {
  std::vector<HMODULE> modules;
  std::map<std::string, std::unique_ptr<void*>> imports;
  std::shared_ptr<hxjit::LibraryRegistry> libraries;
public:
  explicit MinGWImports(std::shared_ptr<hxjit::LibraryRegistry> libraries)
      : libraries(std::move(libraries)) {}
  ~MinGWImports() override { for (auto module : modules) FreeLibrary(module); }
  Error load(const fs::path& path, bool system = false) {
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, system ? LOAD_LIBRARY_SEARCH_SYSTEM32 :
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) return createStringError(inconvertibleErrorCode(), "Cannot load MinGW runtime %s (Windows error %lu)",
                                         path.string().c_str(), GetLastError());
    modules.push_back(module);
    return Error::success();
  }
  Error loadRuntime(const fs::path& path) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(0, path.filename().c_str(), &module)) {
      modules.push_back(module);
      return Error::success();
    }
    return load(path);
  }
  void addBackend() {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&hxcpp_JIT_mingw_root), &module)) modules.push_back(module);
  }
  Error tryToGenerate(LookupState&, LookupKind, JITDylib& jd, JITDylibLookupFlags,
                      const SymbolLookupSet& symbols) override {
    SymbolMap found;
    for (const auto& symbol : symbols) {
      std::string name = (*symbol.first).str();
      bool indirect = name.rfind("__imp_", 0) == 0;
      const char* exported = name.c_str() + (indirect ? 6 : 0);
      void* address = nullptr;
      for (HMODULE module : modules)
        if (auto proc = GetProcAddress(module, exported)) { address = reinterpret_cast<void*>(proc); break; }
      if (!address) {
        if (auto* loaded = libraries->find(name))
          found[symbol.first] = {ExecutorAddr::fromPtr(loaded), JITSymbolFlags::Exported};
        continue;
      }
      auto flags = JITSymbolFlags::Exported;
      if (indirect) {
        auto& slot = imports[name];
        if (!slot) slot = std::make_unique<void*>(address);
        address = slot.get();
      }
      found[symbol.first] = {ExecutorAddr::fromPtr(address), flags};
    }
    if (found.empty()) return Error::success();
    return jd.define(absoluteSymbols(std::move(found)));
  }
};

Error configureRuntime(LLJIT& jit, std::shared_ptr<hxjit::LibraryRegistry> libraries) {
  try {
    auto root = hxcpp_JIT_mingw_root();
    auto imports = std::make_unique<MinGWImports>(std::move(libraries));
    imports->addBackend();
    for (const wchar_t* name : {L"libunwind.dll", L"libc++.dll"})
      if (auto error = imports->loadRuntime(root / "bin" / name)) return error;
    for (const wchar_t* name : {L"ucrtbase.dll", L"kernel32.dll", L"ntdll.dll"})
      if (auto error = imports->load(fs::path(name), true)) return error;
    auto& jd = jit.getMainJITDylib();
    SymbolMap runtime;
    runtime[jit.getExecutionSession().intern("__emutls_get_address")] = {
        ExecutorAddr::fromPtr(&__emutls_get_address), JITSymbolFlags::Exported | JITSymbolFlags::Callable};
    auto& session = jit.getExecutionSession();
    auto functionFlags = JITSymbolFlags::Exported | JITSymbolFlags::Callable;
    runtime[session.intern("snprintf")] = {ExecutorAddr::fromPtr(&mingwSnprintf), functionFlags};
    runtime[session.intern("vsnprintf")] = {ExecutorAddr::fromPtr(&mingwVsnprintf), functionFlags};
    runtime[session.intern("__imp_snprintf")] = {ExecutorAddr::fromPtr(&snprintfImport), JITSymbolFlags::Exported};
    runtime[session.intern("__imp_vsnprintf")] = {ExecutorAddr::fromPtr(&vsnprintfImport), JITSymbolFlags::Exported};
    if (auto error = jd.define(absoluteSymbols(std::move(runtime)))) return error;
    jd.addGenerator(std::move(imports));
    std::vector<fs::path> archives;
    auto lib = root / "x86_64-w64-mingw32" / "lib";
    for (const char* name : {"libmingwex.a", "libmoldname.a"}) archives.push_back(lib / name);
    auto clang = root / "lib" / "clang";
    if (fs::exists(clang))
      for (const auto& entry : fs::recursive_directory_iterator(clang))
        if (entry.is_regular_file() && entry.path().filename() == "libclang_rt.builtins-x86_64.a" &&
            entry.path().parent_path().filename() == "windows")
          archives.push_back(entry.path());
    if (archives.size() != 3)
      return createStringError(inconvertibleErrorCode(), "Expected exactly one Windows x64 compiler-rt builtins archive in bundled MinGW");
    for (const auto& archive : archives) {
      auto generator = StaticLibraryDefinitionGenerator::Load(jit.getObjLinkingLayer(), archive.string().c_str());
      if (!generator) return generator.takeError();
      jd.addGenerator(std::move(*generator));
    }
    return Error::success();
  } catch (const std::exception& error) {
    return createStringError(inconvertibleErrorCode(), "%s", error.what());
  }
}
}

void hxcpp_JIT_configure_mingw_builder(LLJITBuilder& builder,
    std::shared_ptr<hxjit::LibraryRegistry> libraries) {
  builder.setNotifyCreatedCallback([libraries = std::move(libraries)](LLJIT& jit) {
    return configureRuntime(jit, libraries);
  });
  builder.setObjectLinkingLayerCreator([](ExecutionSession& session, jitlink::JITLinkMemoryManager&)
      -> Expected<std::unique_ptr<ObjectLayer>> {
    auto layer = std::make_unique<RTDyldObjectLinkingLayer>(session,
        [](const MemoryBuffer& buffer) -> std::unique_ptr<RuntimeDyld::MemoryManager> {
          auto objectFile = object::ObjectFile::createObjectFile(buffer.getMemBufferRef());
          if (!objectFile) report_fatal_error(Twine("Cannot read MinGW COFF unwind metadata: ") + toString(objectFile.takeError()));
          std::map<std::string, size_t> sizes;
          for (const auto& section : (*objectFile)->sections()) {
            auto name = section.getName();
            if (!name) report_fatal_error(Twine("Cannot read MinGW COFF section name: ") + toString(name.takeError()));
            if (*name != ".pdata" && !name->starts_with(".pdata$")) continue;
            size_t size = section.getSize();
            if (size % sizeof(RUNTIME_FUNCTION))
              report_fatal_error("Original MinGW COFF unwind payload is not a multiple of RUNTIME_FUNCTION");
            auto inserted = sizes.emplace(name->str(), size);
            if (!inserted.second && inserted.first->second != size)
              report_fatal_error("Ambiguous duplicate MinGW COFF unwind section names");
          }
          return std::make_unique<MinGWMemoryManager>(std::move(sizes));
        });
    layer->setOverrideObjectFlagsWithResponsibilityFlags(true);
    layer->setAutoClaimResponsibilityForObjectSymbols(true);
    return std::unique_ptr<ObjectLayer>(std::move(layer));
  });
}
