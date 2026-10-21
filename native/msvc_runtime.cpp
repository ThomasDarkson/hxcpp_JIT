#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unordered_map>

#include <cstddef>
#include <new>
#include <memory>
#include <map>
#include <atomic>
#include <stdexcept>
#include <typeinfo>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#endif

#ifdef _MSC_VER

static int hxTraceLevel() {
  static const int level = []() {
    const char* v = std::getenv("HXCPP_JIT_TRACE_EH");
    return v ? std::atoi(v) : 0;
  }();
  return level;
}

static void hxPrintBacktrace(const char* why);

static void hxDescribe(const void* p) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) { std::fprintf(stderr, " [VirtualQuery failed]"); return; }
  std::fprintf(stderr, " [state=0x%lx prot=0x%lx allocbase=%p]", (unsigned long)mbi.State,
               (unsigned long)mbi.Protect, mbi.AllocationBase);
  if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    std::fprintf(stderr, " bytes:");
    for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %02x", b[i]);
  }
}

static void hxTraceEH(uint64_t base, const RUNTIME_FUNCTION* t, size_t n) {
  static bool printedExpected = false;
  HMODULE crt = GetModuleHandleA("vcruntime140.dll");
  void* expect = crt ? reinterpret_cast<void*>(GetProcAddress(crt, "__CxxFrameHandler3")) : nullptr;
  if (!printedExpected) {
    std::fprintf(stderr, "hxcpp_JIT EH: real __CxxFrameHandler3 = %p\n", expect);
    printedExpected = true;
  }
  for (size_t i = 0; i < n; ++i) {
    if (t[i].UnwindData & 1) continue;
    const uint8_t* u = reinterpret_cast<const uint8_t*>(base) + t[i].UnwindData;
    unsigned flags = u[0] >> 3;
    if (!(flags & 3)) continue;
    std::fprintf(stderr, "  UNWIND byte0=0x%02x prolog=%u codes=%u frame=0x%02x\n", u[0], u[1], u[2], u[3]);
    const uint8_t* h = u + 4 + ((u[2] + 1u) & ~1u) * 2;
    uint32_t rva;
    std::memcpy(&rva, h, 4);
     uint32_t fiRva; std::memcpy(&fiRva, h + 4, 4);
   const int32_t* fi = (const int32_t*)(base + fiRva);
   const int32_t* tm = (const int32_t*)(base + (uint32_t)fi[4]);
   for (int i = 0; i < fi[3]; ++i) {
     const int32_t* ha = (const int32_t*)(base + (uint32_t)tm[i*5+4]);
     for (int j = 0; j < tm[i*5+3]; ++j) {
       const int32_t* ht = ha + j*5;
       std::fprintf(stderr, "  HT adj=%d typeRva=%d(0x%x) -> %p catchObj=%d parentFrame=%d",
                    ht[0], ht[1], (unsigned)ht[1], (void*)(base + (uint32_t)ht[1]), ht[2], ht[4]);
       hxDescribe((void*)(base + (uint32_t)ht[1]));
       std::fprintf(stderr, "\n");
     }
   }
    void* target = reinterpret_cast<void*>(base + rva);
    std::fprintf(stderr, "hxcpp_JIT EH: base=%p func=%p handler rva=0x%x -> %p", reinterpret_cast<void*>(base),
                 reinterpret_cast<void*>(base + t[i].BeginAddress), (unsigned)rva, target);
    hxDescribe(target);
    std::fprintf(stderr, "\n");
  }
}

static LONG CALLBACK hxVeh(PEXCEPTION_POINTERS ep) {
  const EXCEPTION_RECORD* r = ep->ExceptionRecord;
  if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    void* rip = reinterpret_cast<void*>(ep->ContextRecord->Rip);
    std::fprintf(stderr, "hxcpp_JIT EH: ACCESS VIOLATION rip=%p %s address=%p", rip,
                 r->ExceptionInformation[0] == 0 ? "reading" : r->ExceptionInformation[0] == 1 ? "writing" : "executing",
                 reinterpret_cast<void*>(r->ExceptionInformation[1]));
    hxDescribe(rip);
    std::fprintf(stderr, "\n");
    hxPrintBacktrace("access violation");
  } else if (r->ExceptionCode == 0xC0000025 || r->ExceptionCode == 0xC0000026 || r->ExceptionCode == 0xC0000409) {
    char why[64]; std::snprintf(why, sizeof(why), "fatal exception 0x%08lx", (unsigned long)r->ExceptionCode);
    hxPrintBacktrace(why);
  } else if (r->ExceptionCode == 0xE06D7363) {
    std::fprintf(stderr, "hxcpp_JIT EH: C++ throw obj=%p throwinfo=%p imagebase=%p (nparams=%lu)\n",
                 reinterpret_cast<void*>(r->ExceptionInformation[1]),
                 reinterpret_cast<void*>(r->ExceptionInformation[2]),
                 reinterpret_cast<void*>(r->ExceptionInformation[3]), (unsigned long)r->NumberParameters);
  }
  std::fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

static void hxInstallVeh() {
  if (hxTraceLevel() > 0) AddVectoredExceptionHandler(1, hxVeh);
}

namespace {
struct HxImage {
  const void* key;
  uint64_t base;
  std::vector<std::pair<uint64_t, uint64_t>> spans;
};
std::mutex g_hxImgMu;
std::vector<HxImage> g_hxImages;

void hxRegisterImage(const void* key, uint64_t base, const std::vector<std::pair<uint64_t, uint64_t>>& spans) {
  std::lock_guard<std::mutex> g(g_hxImgMu);
  for (auto& im : g_hxImages)
    if (im.key == key) { im.base = base; im.spans = spans; return; }
  g_hxImages.push_back({key, base, spans});
}

void hxUnregisterImage(const void* key) {
  std::lock_guard<std::mutex> g(g_hxImgMu);
  for (size_t i = 0; i < g_hxImages.size(); ++i)
    if (g_hxImages[i].key == key) { g_hxImages.erase(g_hxImages.begin() + i); return; }
}

bool hxFindImageBase(uint64_t addr, uint64_t& base) {
  std::lock_guard<std::mutex> g(g_hxImgMu);
  for (auto& im : g_hxImages)
    for (auto& sp : im.spans)
      if (addr >= sp.first && addr < sp.second) { base = im.base; return true; }
  return false;
}
}

static void hxFixThrowInfo(uint64_t base, const void* throwInfo);
static void hxFixRtti(uint64_t base, const std::vector<std::pair<uint64_t, uint64_t>>& spans);

static void hxFacetRegister(void*) {}
static void hxThrdSleepFor(unsigned long ms) { Sleep(ms); }

static void hxcpp_JIT_throw_exception(void* obj, const void* throwInfo) {
  using RealFn = void (*)(void*, const void*);
  static RealFn real = []() -> RealFn {
    for (const char* dll : {"vcruntime140.dll", "vcruntime140d.dll"})
      if (HMODULE m = GetModuleHandleA(dll))
        if (auto f = GetProcAddress(m, "_CxxThrowException")) return reinterpret_cast<RealFn>(f);
    return nullptr;
  }();
  uint64_t base = 0;
  if (throwInfo && hxFindImageBase(reinterpret_cast<uint64_t>(throwInfo), base)) {
    hxFixThrowInfo(base, throwInfo);
    if (hxTraceLevel() >= 2) {
      const int32_t* ti = static_cast<const int32_t*>(throwInfo);
      const int32_t* cta = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(ti[3]));
      std::fprintf(stderr, "  THROWINFO unwindRva=0x%x nTypes=%d\n", (unsigned)ti[1], cta[0]);
      for (int i = 0; i < cta[0] && i < 8; ++i) {
        const int32_t* ct = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(cta[1 + i]));
        const uint8_t* ty = reinterpret_cast<const uint8_t*>(base + static_cast<uint32_t>(ct[1]));
        std::fprintf(stderr, "  CT[%d] typeRva=0x%x -> %p copyRva=0x%x -> %p", i, (unsigned)ct[1],
                     (const void*)ty, (unsigned)ct[6],
                     reinterpret_cast<void*>(base + static_cast<uint32_t>(ct[6])));
        hxDescribe(ty);
        std::fprintf(stderr, "\n");
      }
    }
    ULONG_PTR params[4] = {0x19930520 , reinterpret_cast<ULONG_PTR>(obj),
                           reinterpret_cast<ULONG_PTR>(throwInfo), static_cast<ULONG_PTR>(base)};
    RaiseException(0xE06D7363 , EXCEPTION_NONCONTINUABLE, 4, params);
  }
  if (real) real(obj, throwInfo);
  std::abort();
}

namespace {
struct HxArena;
std::mutex g_hxArenaMu;
std::vector<HxArena*> g_hxArenas;
static void hxReleaseImageBlocks(HxArena& arena);

struct HxArena {
  std::mutex mu;
  uint8_t* base = nullptr;
  size_t reserved = size_t(1) << 30;
  size_t used = 0, committed = 0;
  std::mutex metadataMu;
  std::mutex tlsMu;
  using TlsAllocation = std::unique_ptr<void, void (*)(void*)>;
  std::map<std::pair<uint64_t, const void*>, TlsAllocation> tlsSlots;
  std::unordered_map<const uint8_t*, uint8_t*> descriptorCopies;
  std::unordered_map<const void*, const void*> throwCopies;
  std::vector<std::pair<size_t, size_t>> imageBlocks;

  HxArena() {
    std::lock_guard<std::mutex> g(g_hxArenaMu);
    g_hxArenas.push_back(this);
  }
  ~HxArena() {
    {
      std::lock_guard<std::mutex> g(g_hxArenaMu);
      g_hxArenas.erase(std::remove(g_hxArenas.begin(), g_hxArenas.end(), this), g_hxArenas.end());
    }
    hxReleaseImageBlocks(*this);
    if (base) VirtualFree(base, 0, MEM_RELEASE);
  }

  bool reserve() {
    std::lock_guard<std::mutex> g(g_hxArenaMu);
    if (!base) base = static_cast<uint8_t*>(VirtualAlloc(nullptr, reserved, MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    return base != nullptr;
  }

  static constexpr size_t poolSize = size_t(1) << 20;
  size_t poolUsed = 0;
  uint8_t* allocTop(size_t size) {
    std::lock_guard<std::mutex> g(mu);
    if (!reserve()) return nullptr;
    uint8_t* pool = base + reserved - poolSize;
    if (poolUsed == 0 && !VirtualAlloc(pool, poolSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return nullptr;
    size = (size + 15) & ~size_t(15);
    if (poolUsed + size > poolSize) return nullptr;
    uint8_t* p = pool + poolUsed;
    poolUsed += size;
    return p;
  }

  uint8_t* alloc(size_t size, size_t align) {
    std::lock_guard<std::mutex> g(mu);
    if (!reserve()) return nullptr;
    if (align < 16) align = 16;
    size_t start = (used + align - 1) & ~(align - 1);
    size_t end = start + (size ? size : 1);
    if (end > reserved - poolSize) return nullptr;
    if (end > committed) {
      size_t newCommitted = (end + 0xFFFF) & ~size_t(0xFFFF);
      if (!VirtualAlloc(base + committed, newCommitted - committed, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
        return nullptr;
      committed = newCommitted;
    }
    used = end;
    return base + start;
  }
  void flush() {
    std::lock_guard<std::mutex> g(mu);
    if (base && used) FlushInstructionCache(GetCurrentProcess(), base, used);
  }
};
HxArena* hxFindArena(uint64_t address) {
  std::lock_guard<std::mutex> g(g_hxArenaMu);
  for (auto* a : g_hxArenas) {
    uint64_t lo = reinterpret_cast<uint64_t>(a->base);
    if (a->base && address >= lo && address - lo < a->reserved) return a;
  }
  return nullptr;
}
HxArena& hxArena(uint64_t address) {
  if (auto* a = hxFindArena(address)) return *a;
  std::fprintf(stderr, "hxcpp_JIT: address does not belong to a live JIT session\n");
  std::abort();
}
}

static void hxFixCatchTypes(uint64_t base, const RUNTIME_FUNCTION* t, size_t n) {
  for (size_t k = 0; k < n; ++k) {
    if (t[k].UnwindData & 1) continue;
    const uint8_t* u = reinterpret_cast<const uint8_t*>(base) + t[k].UnwindData;
    if (!((u[0] >> 3) & 3)) continue;
    const uint8_t* h = u + 4 + ((u[2] + 1u) & ~1u) * 2;
    uint32_t fiRva; std::memcpy(&fiRva, h + 4, 4);
    const int32_t* fi = reinterpret_cast<const int32_t*>(base + fiRva);
    uint32_t magic = static_cast<uint32_t>(fi[0]) & 0x1FFFFFFFu;
    if (magic < 0x19930520u || magic > 0x19930522u) continue;
    int nTry = fi[3];
    if (nTry <= 0 || nTry > 4096) continue;
    const int32_t* tm = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(fi[4]));
    for (int a = 0; a < nTry; ++a) {
      int nCatch = tm[a * 5 + 3];
      int32_t* ha = reinterpret_cast<int32_t*>(base + static_cast<uint32_t>(tm[a * 5 + 4]));
      for (int b = 0; b < nCatch; ++b) {
        int32_t* ht = ha + b * 5;
        if (ht[0] & 0x40) continue;
        const uint8_t* ty = reinterpret_cast<const uint8_t*>(base + static_cast<int64_t>(ht[1]));
        if (ht[1] != 0) {
          static const uint8_t stub[6] = {0xff, 0x25, 0, 0, 0, 0};
          if (std::memcmp(ty, stub, 6) != 0) continue;
          std::memcpy(&ty, ty + 6, 8);
        }
        const char* name = reinterpret_cast<const char*>(ty + 16);
        size_t len = std::strlen(name);
        if (!len) continue;
        uint8_t* copy = hxArena(base).alloc(16 + len + 1, 16);
        if (!copy) continue;
        std::memcpy(copy, ty, 8);
        std::memset(copy + 8, 0, 8);
        std::memcpy(copy + 16, name, len + 1);
        int64_t rva = reinterpret_cast<int64_t>(copy) - static_cast<int64_t>(base);
        if (rva <= 0 || rva > INT32_MAX) continue;
        ht[1] = static_cast<int32_t>(rva);
      }
    }
  }
}

namespace {
class HxSectionMemoryManager : public llvm::SectionMemoryManager {
  std::shared_ptr<HxArena> arena;
  uint64_t lowest = UINT64_MAX;
  std::vector<std::vector<RUNTIME_FUNCTION>> tables;
  std::vector<std::pair<uint64_t, uint64_t>> spans;

    void track(uint8_t* p, uintptr_t size) {
    if (p) {
      uintptr_t n = size ? size : 1;
      lowest = std::min<uint64_t>(lowest, (uint64_t)(uintptr_t)p);
      spans.emplace_back((uint64_t)(uintptr_t)p, (uint64_t)(uintptr_t)p + n);
    }
  }

public:
  explicit HxSectionMemoryManager(std::shared_ptr<HxArena> arena) : arena(std::move(arena)) {}

  uint8_t* allocateCodeSection(uintptr_t Size, unsigned Align, unsigned, llvm::StringRef) override {
    uint8_t* p = arena->alloc(Size, Align);
    track(p, Size);
    return p;
  }
  uint8_t* allocateDataSection(uintptr_t Size, unsigned Align, unsigned, llvm::StringRef, bool) override {
    uint8_t* p = arena->alloc(Size, Align);
    track(p, Size);
    return p;
  }
  bool finalizeMemory(std::string*  = nullptr) override {
    arena->flush();
    return false;
  }

  void registerEHFrames(uint8_t* Addr, uint64_t , size_t Size) override {
    if (lowest == UINT64_MAX) return;
    hxRegisterImage(this, lowest, spans);
    hxFixRtti(lowest, spans);
    size_t n = Size / sizeof(RUNTIME_FUNCTION);
    if (!n) return;
    std::vector<RUNTIME_FUNCTION> copy(reinterpret_cast<RUNTIME_FUNCTION*>(Addr),
                                       reinterpret_cast<RUNTIME_FUNCTION*>(Addr) + n);
    std::sort(copy.begin(), copy.end(), [](const RUNTIME_FUNCTION& a, const RUNTIME_FUNCTION& b) {
      return a.BeginAddress < b.BeginAddress;
    });
    tables.push_back(std::move(copy));
    auto& t = tables.back();
    hxFixCatchTypes(lowest, t.data(), t.size());
    if (hxTraceLevel() >= 2) hxTraceEH(lowest, t.data(), t.size());
    if (!RtlAddFunctionTable(t.data(), (DWORD)t.size(), (DWORD64)lowest)) {
      std::fprintf(stderr, "hxcpp_JIT: RtlAddFunctionTable failed\n");
      tables.pop_back();
    }
  }

  void deregisterEHFrames() override {
    hxUnregisterImage(this);
    for (auto& t : tables) RtlDeleteFunctionTable(t.data());
    tables.clear();
  }
  ~HxSectionMemoryManager() override { deregisterEHFrames(); }
};
}

static bool hxReadable(const void* p, size_t n) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
  return static_cast<const uint8_t*>(p) + n <= static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

static bool hxIsTypeDesc(const uint8_t* p) {
  if (!p || !hxReadable(p, 24)) return false;
  void* vt; std::memcpy(&vt, p, 8);
  if (!vt || !hxReadable(vt, 8)) return false;
  void* cache; std::memcpy(&cache, p + 8, 8);
  if (cache && !hxReadable(cache, 8)) return false;
  if (p[16] != '.') return false;
  size_t n = 0;
  while (n < 512 && p[16 + n]) {
    if (p[16 + n] < 0x20 || p[16 + n] > 0x7e) return false;
    ++n;
  }
  return n >= 2 && n < 512 && hxReadable(p, 16 + n + 1);
}

static bool hxFindRealTypeInfo(uint32_t low, const uint8_t*& out) {
  SYSTEM_INFO si; GetSystemInfo(&si);
  const uint64_t lo = reinterpret_cast<uint64_t>(si.lpMinimumApplicationAddress);
  const uint64_t hiEnd = reinterpret_cast<uint64_t>(si.lpMaximumApplicationAddress);
  const uint8_t* firstHit = nullptr;
  int hits = 0;
  uint64_t p = lo;
  MEMORY_BASIC_INFORMATION mbi;
  while (p < hiEnd && VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi))) {
    const uint64_t rb = reinterpret_cast<uint64_t>(mbi.BaseAddress), re = rb + mbi.RegionSize;
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      for (uint64_t hi = rb & ~0xFFFFFFFFull; hi < re; hi += (1ull << 32)) {
        uint64_t cand = hi | low;
        if (cand < rb || cand + 24 > re) continue;
        if (hxIsTypeDesc(reinterpret_cast<const uint8_t*>(cand))) {
          if (!firstHit) firstHit = reinterpret_cast<const uint8_t*>(cand);
          ++hits;
        }
      }
    }
    if (re <= p) break;
    p = re;
  }
  if (hits > 1)
    std::fprintf(stderr, "hxcpp_JIT EH: warning: %d type descriptors match low32=0x%08x, using %p\n", hits, low,
                 (const void*)firstHit);
  out = firstHit;
  return firstHit != nullptr;
}

static const uint8_t kJmpStub[6] = {0xff, 0x25, 0, 0, 0, 0};

static const uint8_t* hxResolveDesc(uint64_t ta) {
  const bool inArena = hxFindArena(ta) != nullptr;
  if (inArena && hxIsTypeDesc(reinterpret_cast<const uint8_t*>(ta))) return reinterpret_cast<const uint8_t*>(ta);
  if (inArena && hxReadable(reinterpret_cast<const void*>(ta), 14) &&
      std::memcmp(reinterpret_cast<const void*>(ta), kJmpStub, 6) == 0) {
    const uint8_t* tgt; std::memcpy(&tgt, reinterpret_cast<const uint8_t*>(ta) + 6, 8);
    if (hxIsTypeDesc(tgt)) return tgt;
  }
  const uint8_t* real = nullptr;
  hxFindRealTypeInfo(static_cast<uint32_t>(ta), real);
  return real;
}

static uint8_t* hxPoolCopyDesc(const uint8_t* real, HxArena& A) {
  std::lock_guard<std::mutex> g(A.metadataMu);
  auto& copies = A.descriptorCopies;
  auto it = copies.find(real);
  if (it != copies.end()) return it->second;
  const char* name = reinterpret_cast<const char*>(real) + 16;
  size_t len = std::strlen(name);
  uint8_t* copy = A.allocTop(16 + len + 1);
  if (!copy) return nullptr;
  std::memcpy(copy, real, 8);
  std::memset(copy + 8, 0, 8);
  std::memcpy(copy + 16, name, len + 1);
  copies.emplace(real, copy);
  return copy;
}

[[noreturn]] static void hxFatal(const char* what, int i, uint64_t base, const void* a, const void* b) {
  std::fprintf(stderr, "hxcpp_JIT EH: FATAL: catchable type #%d: %s (base=%p, %p, %p)\n", i, what,
               (void*)base, a, b);
  std::fflush(stderr);
  std::abort();
}

static void hxFixThrowInfo(uint64_t base, const void* throwInfo) {
  HxArena& A = hxArena(base);
  const int32_t* ti = static_cast<const int32_t*>(throwInfo);
  int32_t* cta = reinterpret_cast<int32_t*>(base + static_cast<uint32_t>(ti[3]));
  auto rvaOf = [&](const void* p, int i) -> int32_t {
    int64_t r = reinterpret_cast<int64_t>(p) - static_cast<int64_t>(base);
    if (r <= 0 || r > INT32_MAX) hxFatal("copy is out of RVA range of the image base", i, base, p, nullptr);
    return static_cast<int32_t>(r);
  };
  for (int i = 0; i < cta[0] && i < 64; ++i) {
    int32_t* ct = reinterpret_cast<int32_t*>(base + static_cast<uint32_t>(cta[1 + i]));

    if (hxReadable(ct, 14) && std::memcmp(ct, kJmpStub, 6) == 0) {
      uint64_t tgt; std::memcpy(&tgt, reinterpret_cast<const uint8_t*>(ct) + 6, 8);
      uint64_t srcBase = 0;
      if (!hxReadable(reinterpret_cast<const void*>(tgt), 28) || !hxFindImageBase(tgt, srcBase))
        hxFatal("CatchableType stub target is not in known JIT memory", i, base, ct, (void*)tgt);
      const int32_t* src = reinterpret_cast<const int32_t*>(tgt);
      const uint8_t* real = hxResolveDesc(srcBase + static_cast<uint32_t>(src[1]));
      if (!real) hxFatal("cannot resolve type descriptor of shared CatchableType", i, base, (void*)tgt, nullptr);
      uint8_t* tcopy = hxPoolCopyDesc(real, A);
      uint8_t* nct = A.allocTop(32);
      if (!tcopy || !nct) hxFatal("top pool exhausted", i, base, nullptr, nullptr);
      int32_t n[7];
      std::memcpy(n, src, sizeof(n));
      n[1] = rvaOf(tcopy, i);
      if (src[6]) {
        uint8_t* st = A.allocTop(16);
        if (!st) hxFatal("top pool exhausted", i, base, nullptr, nullptr);
        uint64_t fn = srcBase + static_cast<uint32_t>(src[6]);
        std::memcpy(st, kJmpStub, 6);
        std::memcpy(st + 6, &fn, 8);
        n[6] = rvaOf(st, i);
      }
      std::memcpy(nct, n, sizeof(n));
      cta[1 + i] = rvaOf(nct, i);
      continue;
    }

    const uint64_t ta = base + static_cast<uint32_t>(ct[1]);
    const uint64_t lo = reinterpret_cast<uint64_t>(A.base), hi = lo + A.reserved;
    if (ta >= lo && ta < hi && hxIsTypeDesc(reinterpret_cast<const uint8_t*>(ta))) continue;
    const uint8_t* real = hxResolveDesc(ta);
    if (!real) {
      std::fprintf(stderr, "  CT raw ints:");
      for (int k = 0; k < 7; ++k) std::fprintf(stderr, " %08x", (unsigned)ct[k]);
      std::fprintf(stderr, "\n");
      hxFatal("unresolvable type descriptor", i, base, (void*)ta, nullptr);
    }
    uint8_t* copy = hxPoolCopyDesc(real, A);
    if (!copy) hxFatal("top pool exhausted", i, base, nullptr, nullptr);
    ct[1] = rvaOf(copy, i);
  }
}

static bool hxStubTarget(uint64_t& addr) {
  if (!hxReadable(reinterpret_cast<const void*>(addr), 14)) return false;
  const uint8_t* q = reinterpret_cast<const uint8_t*>(addr);
  if (std::memcmp(q, kJmpStub, 6) != 0) return false;
  uint64_t t; std::memcpy(&t, q + 6, 8);
  addr = t;
  return true;
}

static void hxFixRtti(uint64_t base, const std::vector<std::pair<uint64_t, uint64_t>>& spans) {
  HxArena& A = hxArena(base);
  int nCol = 0, nFixed = 0;
  auto rvaTop = [&](const void* p) -> int32_t {
    int64_t r = reinterpret_cast<int64_t>(p) - static_cast<int64_t>(base);
    if (r <= 0 || r > INT32_MAX) hxFatal("RTTI copy out of RVA range", -1, base, p, nullptr);
    return static_cast<int32_t>(r);
  };
  auto rvaAny = [&](uint64_t abs, int32_t& out) -> bool {
    int64_t r = static_cast<int64_t>(abs) - static_cast<int64_t>(base);
    if (r < INT32_MIN || r > INT32_MAX) return false;
    out = static_cast<int32_t>(r);
    return true;
  };

  auto fixCol = [&](int32_t* c) -> bool {
    bool any = false;
    {
      const uint64_t ta = base + static_cast<uint32_t>(c[3]);
      const uint8_t* real = hxResolveDesc(ta);
      if (real && reinterpret_cast<uint64_t>(real) != ta)
        if (uint8_t* cp = hxPoolCopyDesc(real, A)) { c[3] = rvaTop(cp); any = true; }
    }
    uint64_t hAddr = base + static_cast<int64_t>(c[4]);
    uint64_t hSb = base;
    const bool hMoved = hxStubTarget(hAddr);
    if (hMoved && !hxFindImageBase(hAddr, hSb)) return any;
    if (!hxReadable(reinterpret_cast<const void*>(hAddr), 16)) return any;
    const int32_t* h = reinterpret_cast<const int32_t*>(hAddr);
    const int n = h[2];
    if ((h[0] != 0 && h[0] != 1) || n <= 0 || n > 256) return any;
    uint64_t aAddr = hSb + static_cast<int64_t>(h[3]);
    uint64_t aSb = hSb;
    const bool aMoved = hxStubTarget(aAddr);
    if (aMoved && !hxFindImageBase(aAddr, aSb)) return any;
    if (!hxReadable(reinterpret_cast<const void*>(aAddr), 4 * static_cast<size_t>(n))) return any;
    const int32_t* arr = reinterpret_cast<const int32_t*>(aAddr);

    std::vector<int32_t> na(n);
    bool changed = hMoved || aMoved || aSb != base || hSb != base;
    for (int i = 0; i < n; ++i) {
      na[i] = arr[i];
      uint64_t bAddr = aSb + static_cast<int64_t>(arr[i]);
      uint64_t bSb = aSb;
      const bool bMoved = hxStubTarget(bAddr);
      if (bMoved && !hxFindImageBase(bAddr, bSb)) continue;
      if (!hxReadable(reinterpret_cast<const void*>(bAddr), 28)) continue;
      const int32_t* bd = reinterpret_cast<const int32_t*>(bAddr);
      const uint64_t tAddr = bSb + static_cast<uint32_t>(bd[0]);
      const uint8_t* real = hxResolveDesc(tAddr);
      const bool tBad = !real || reinterpret_cast<uint64_t>(real) != tAddr;
      if (!(bMoved || bSb != base || aSb != base || tBad)) continue;
      if (!real) continue;
      uint8_t* tcopy = hxPoolCopyDesc(real, A);
      uint8_t* nb = A.allocTop(32);
      if (!tcopy || !nb) hxFatal("top pool exhausted (RTTI)", i, base, nullptr, nullptr);
      int32_t v[7];
      std::memcpy(v, bd, sizeof(v));
      v[0] = rvaTop(tcopy);
      int32_t conv = 0;
      v[6] = rvaAny(bSb + static_cast<int64_t>(bd[6]), conv) ? conv : 0;
      std::memcpy(nb, v, sizeof(v));
      na[i] = rvaTop(nb);
      changed = true;
    }
    if (!changed) return any;
    uint8_t* narr = A.allocTop(4 * static_cast<size_t>(n));
    uint8_t* nh = A.allocTop(16);
    if (!narr || !nh) hxFatal("top pool exhausted (RTTI)", -1, base, nullptr, nullptr);
    std::memcpy(narr, na.data(), 4 * static_cast<size_t>(n));
    int32_t hv[4];
    std::memcpy(hv, h, sizeof(hv));
    hv[3] = rvaTop(narr);
    std::memcpy(nh, hv, sizeof(hv));
    c[4] = rvaTop(nh);
    return true;
  };

  for (auto& sp : spans) {
    uint64_t p = (sp.first + 3) & ~uint64_t(3);
    for (; p + 24 <= sp.second; p += 4) {
      if (p < base) continue;
      int32_t* c = reinterpret_cast<int32_t*>(p);
      if (c[0] != 1 || c[3] == 0 || c[4] == 0 || c[5] != static_cast<int32_t>(p - base)) continue;
      ++nCol;
      if (fixCol(c)) ++nFixed;
    }
  }
  if (nCol && hxTraceLevel() > 0)
    std::fprintf(stderr, "hxcpp_JIT EH: RTTI: base=%p %d COL(s), %d repaired\n", reinterpret_cast<void*>(base), nCol, nFixed);
}

#pragma section(".hxpool", read, write, execute)
__declspec(allocate(".hxpool")) static uint8_t g_hxImgPool[1 << 18];
extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
std::mutex g_hxPoolMu;
std::vector<std::pair<size_t, size_t>> g_hxPoolFree = {{0, sizeof(g_hxImgPool)}};

static void hxReleaseImageBlocks(HxArena& arena) {
  std::lock_guard<std::mutex> g(g_hxPoolMu);
  for (auto block : arena.imageBlocks) g_hxPoolFree.push_back(block);
  std::sort(g_hxPoolFree.begin(), g_hxPoolFree.end());
  size_t out = 0;
  for (auto block : g_hxPoolFree) {
    if (out && g_hxPoolFree[out - 1].first + g_hxPoolFree[out - 1].second == block.first)
      g_hxPoolFree[out - 1].second += block.second;
    else g_hxPoolFree[out++] = block;
  }
  g_hxPoolFree.resize(out);
  arena.imageBlocks.clear();
}
}

static uint8_t* hxImgAlloc(size_t n, HxArena& arena) {
  std::lock_guard<std::mutex> g(g_hxPoolMu);
  static bool prepared = false;
  if (!prepared) {
    DWORD oldProt = 0;
    if (!VirtualProtect(g_hxImgPool, sizeof(g_hxImgPool), PAGE_EXECUTE_READWRITE, &oldProt)) {
      std::fprintf(stderr, "hxcpp_JIT EH: VirtualProtect(.hxpool) failed, error %lu\n", (unsigned long)GetLastError());
      std::fflush(stderr);
      return nullptr;
    }
    prepared = true;
  }
  n = (n + 15) & ~size_t(15);
  for (size_t i = 0; i < g_hxPoolFree.size(); ++i) {
    auto& block = g_hxPoolFree[i];
    if (block.second < n) continue;
    size_t offset = block.first;
    arena.imageBlocks.emplace_back(offset, n);
    block.first += n;
    block.second -= n;
    if (!block.second) g_hxPoolFree.erase(g_hxPoolFree.begin() + i);
    return g_hxImgPool + offset;
  }
  return nullptr;
}

static const void* hxImageThrowInfo(uint64_t jb, const void* throwInfo) {
  HxArena& arena = hxArena(jb);
  static std::mutex mu;
  std::lock_guard<std::mutex> g(mu);
  auto& cache = arena.throwCopies;
  auto it = cache.find(throwInfo);
  if (it != cache.end()) return it->second;

  hxFixThrowInfo(jb, throwInfo);
  const uint64_t hb = reinterpret_cast<uint64_t>(&__ImageBase);
  auto rel = [&](const void* p) -> int32_t {
    int64_t r = reinterpret_cast<int64_t>(p) - static_cast<int64_t>(hb);
    if (r <= 0 || r > INT32_MAX) hxFatal("image pool out of RVA range", -1, hb, p, nullptr);
    return static_cast<int32_t>(r);
  };
  auto stubFor = [&](int32_t jitRva) -> int32_t {
    if (!jitRva) return 0;
    uint64_t target = jb + static_cast<int64_t>(jitRva);
    uint8_t* st = hxImgAlloc(16, arena);
    if (!st) hxFatal("image pool exhausted", -1, hb, nullptr, nullptr);
    std::memcpy(st, kJmpStub, 6);
    std::memcpy(st + 6, &target, 8);
    return rel(st);
  };

  const int32_t* ti = static_cast<const int32_t*>(throwInfo);
  const int32_t* cta = reinterpret_cast<const int32_t*>(jb + static_cast<uint32_t>(ti[3]));
  const int n = cta[0];
  if (n < 0 || n > 64) hxFatal("bad CatchableTypeArray", n, jb, throwInfo, nullptr);
  int32_t* nti = reinterpret_cast<int32_t*>(hxImgAlloc(16, arena));
  int32_t* ncta = reinterpret_cast<int32_t*>(hxImgAlloc(4 * (1 + n), arena));
  if (!nti || !ncta) hxFatal("image pool exhausted", -1, hb, nullptr, nullptr);
  ncta[0] = n;
  for (int i = 0; i < n; ++i) {
    const int32_t* ct = reinterpret_cast<const int32_t*>(jb + static_cast<uint32_t>(cta[1 + i]));
    const uint8_t* desc = reinterpret_cast<const uint8_t*>(jb + static_cast<uint32_t>(ct[1]));
    if (!hxIsTypeDesc(desc)) hxFatal("type descriptor still invalid after fix", i, jb, desc, nullptr);
    const char* name = reinterpret_cast<const char*>(desc) + 16;
    const size_t len = std::strlen(name);
    uint8_t* dcopy = hxImgAlloc(16 + len + 1, arena);
    int32_t* nct = reinterpret_cast<int32_t*>(hxImgAlloc(28, arena));
    if (!dcopy || !nct) hxFatal("image pool exhausted", i, hb, nullptr, nullptr);
    std::memcpy(dcopy, desc, 8);
    std::memset(dcopy + 8, 0, 8);
    std::memcpy(dcopy + 16, name, len + 1);
    std::memcpy(nct, ct, 28);
    nct[1] = rel(dcopy);
    nct[6] = stubFor(ct[6]);
    ncta[1 + i] = rel(nct);
  }
  nti[0] = ti[0];
  nti[1] = stubFor(ti[1]);
  nti[2] = 0;
  nti[3] = rel(ncta);
  cache.emplace(throwInfo, nti);
  return nti;
}

using HxExcPtrCopyFn = void (*)(void*, const void*, const void*);
static HxExcPtrCopyFn g_hxRealExcPtrCopy = nullptr;

static void hxcpp_JIT_exception_ptr_copy(void* out, const void* obj, const void* throwInfo) {
  uint64_t jb = 0;
  if (throwInfo && hxFindImageBase(reinterpret_cast<uint64_t>(throwInfo), jb))
    throwInfo = hxImageThrowInfo(jb, throwInfo);
  g_hxRealExcPtrCopy(out, obj, throwInfo);
}

static std::string hxFindExport(HMODULE m, const char* needle) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(m);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dd.VirtualAddress) return std::string();
  auto ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(b + dd.VirtualAddress);
  auto names = reinterpret_cast<const DWORD*>(b + ex->AddressOfNames);
  for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
    const char* nm = reinterpret_cast<const char*>(b + names[i]);
    if (std::strstr(nm, needle)) return nm;
  }
  return std::string();
}

static std::string hxNearestExport(HMODULE m, uint64_t addr) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(m);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dd.VirtualAddress) return std::string();
  auto ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(b + dd.VirtualAddress);
  auto names = reinterpret_cast<const DWORD*>(b + ex->AddressOfNames);
  auto ords = reinterpret_cast<const WORD*>(b + ex->AddressOfNameOrdinals);
  auto funcs = reinterpret_cast<const DWORD*>(b + ex->AddressOfFunctions);
  const uint64_t rva = addr - reinterpret_cast<uint64_t>(m);
  uint64_t best = 0; const char* bestName = nullptr;
  for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
    uint64_t f = funcs[ords[i]];
    if (f <= rva && f >= best && !(f >= dd.VirtualAddress && f < dd.VirtualAddress + dd.Size)) {
      best = f; bestName = reinterpret_cast<const char*>(b + names[i]);
    }
  }
  if (!bestName) return std::string();
  char buf[64]; std::snprintf(buf, sizeof(buf), "+0x%llx", (unsigned long long)(rva - best));
  return std::string(bestName) + buf;
}

static void hxPrintBacktrace(const char* why) {
  std::fprintf(stderr, "hxcpp_JIT: %s\n", why);
  void* frames[48];
  const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
  for (USHORT i = 0; i < n; ++i) {
    const uint64_t a = reinterpret_cast<uint64_t>(frames[i]);
    if (hxFindArena(a)) {
      std::fprintf(stderr, "  #%u %p  [JIT code]\n", (unsigned)i, frames[i]);
      continue;
    }
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(frames[i]), &m))
      GetModuleFileNameA(m, path, MAX_PATH);
    const char* nm = std::strrchr(path, '\\');
    nm = nm ? nm + 1 : path;
    std::string sym = m ? hxNearestExport(m, a) : std::string();
    std::fprintf(stderr, "  #%u %p  %s+0x%llx  %s\n", (unsigned)i, frames[i], nm,
                 (unsigned long long)(a - reinterpret_cast<uint64_t>(m)), sym.c_str());
  }
  std::fflush(stderr);
}

static void hxTerminateHandler() {
  hxPrintBacktrace("std::terminate() called: an exception was thrown and no handler matched, or an EH table is invalid");
  std::abort();
}

static LONG CALLBACK hxImageBaseFixVeh(PEXCEPTION_POINTERS ep) {
  EXCEPTION_RECORD* r = ep->ExceptionRecord;
  if (r->ExceptionCode == 0xE06D7363 && r->NumberParameters >= 4 && r->ExceptionInformation[0] == 0x19930520 &&
      r->ExceptionInformation[2] != 0 && r->ExceptionInformation[3] == 0) {
    uint64_t base = 0;
    if (hxFindImageBase(static_cast<uint64_t>(r->ExceptionInformation[2]), base)) {
      hxFixThrowInfo(base, reinterpret_cast<const void*>(r->ExceptionInformation[2]));
      r->ExceptionInformation[3] = static_cast<ULONG_PTR>(base);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void hxInstallEhFixes() {
  static std::once_flag once;
  std::call_once(once, [] { AddVectoredExceptionHandler(1, hxImageBaseFixVeh); });
  std::set_terminate(hxTerminateHandler);
}
#endif

llvm::Error hxcpp_JIT_define_runtime_symbols(llvm::orc::LLJIT&, void*);

void hxcpp_JIT_configure_builder(llvm::orc::LLJITBuilder& JB) {
#ifdef _MSC_VER
  hxInstallEhFixes();
  auto arena = std::make_shared<HxArena>();
  JB.setNotifyCreatedCallback([arena](llvm::orc::LLJIT& J) {
    return hxcpp_JIT_define_runtime_symbols(J, arena.get());
  });
  JB.setObjectLinkingLayerCreator(
      [arena](llvm::orc::ExecutionSession& ES, llvm::jitlink::JITLinkMemoryManager&)
          -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
        auto L = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(
            ES, [arena](const llvm::MemoryBuffer&) -> std::unique_ptr<llvm::RuntimeDyld::MemoryManager> {
              return std::make_unique<HxSectionMemoryManager>(arena);
            });
        L->setOverrideObjectFlagsWithResponsibilityFlags(true);
        L->setAutoClaimResponsibilityForObjectSymbols(true);
        return std::unique_ptr<llvm::orc::ObjectLayer>(std::move(L));
      });
#endif
}

void hxcpp_JIT_register_runtime_symbols() {
#ifdef _MSC_VER
  hxInstallVeh();
  using llvm::sys::DynamicLibrary;
  using NewFn      = void* (*)(std::size_t);
  using DeleteFn   = void (*)(void*);
  using SizedDelFn = void (*)(void*, std::size_t);
  DynamicLibrary::AddSymbol("??2@YAPEAX_K@Z",   reinterpret_cast<void*>(static_cast<NewFn>(&::operator new)));
  DynamicLibrary::AddSymbol("??_U@YAPEAX_K@Z",  reinterpret_cast<void*>(static_cast<NewFn>(&::operator new[])));
  DynamicLibrary::AddSymbol("??3@YAXPEAX@Z",    reinterpret_cast<void*>(static_cast<DeleteFn>(&::operator delete)));
  DynamicLibrary::AddSymbol("??3@YAXPEAX_K@Z",  reinterpret_cast<void*>(static_cast<SizedDelFn>(&::operator delete)));
  DynamicLibrary::AddSymbol("??_V@YAXPEAX@Z",   reinterpret_cast<void*>(static_cast<DeleteFn>(&::operator delete[])));
  DynamicLibrary::AddSymbol("??_V@YAXPEAX_K@Z", reinterpret_cast<void*>(static_cast<SizedDelFn>(&::operator delete[])));
  DynamicLibrary::AddSymbol("??_7type_info@@6B@",
                            *reinterpret_cast<void* const*>(&typeid(int)));
#endif
}

struct HxEmuTlsControl {
  std::size_t size;
  std::size_t align;
  union { std::uintptr_t index; void* address; } object;
  void* templ;
};

#ifdef _MSC_VER
static void* hxcpp_JIT_emutls_get_address(HxEmuTlsControl* c) {
  HxArena& arena = hxArena(reinterpret_cast<uint64_t>(c));
  std::lock_guard<std::mutex> g(arena.tlsMu);
  static std::atomic<uint64_t> nextThread{0};
  static thread_local const uint64_t thread = nextThread.fetch_add(1, std::memory_order_relaxed);
  auto key = std::make_pair(thread, static_cast<const void*>(c));
  auto it = arena.tlsSlots.find(key);
  if (it != arena.tlsSlots.end()) return it->second.get();
  std::size_t al = c->align < sizeof(void*) ? sizeof(void*) : c->align;
  std::size_t sz = c->size ? c->size : 1;
  HxArena::TlsAllocation allocation(_aligned_malloc(sz, al), &_aligned_free);
  if (!allocation) std::abort();
  void* p = allocation.get();
  if (c->templ) std::memcpy(p, c->templ, c->size); else std::memset(p, 0, sz);
  arena.tlsSlots.emplace(key, std::move(allocation));
  return p;
}

static void* hxCopyTypeDesc(const std::type_info& ti, HxArena& arena) {
  const char* name = ti.raw_name();
  size_t len = std::strlen(name);
  uint8_t* p = arena.allocTop(16 + len + 1);
  if (!p) return nullptr;
  std::memcpy(p, &ti, 8);
  std::memset(p + 8, 0, 8);
  std::memcpy(p + 16, name, len + 1);
  return p;
}
#endif

llvm::Error hxcpp_JIT_define_runtime_symbols(llvm::orc::LLJIT& J, void* arenaContext) {
#ifdef _MSC_VER
  using namespace llvm::orc;
  using NewFn = void* (*)(std::size_t);
  using DelFn = void (*)(void*);
  using SizedDelFn = void (*)(void*, std::size_t);
  auto& ES = J.getExecutionSession();
  auto fn = llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable;
  SymbolMap M;
  M[ES.intern("??2@YAPEAX_K@Z")]   = {ExecutorAddr::fromPtr((void*)static_cast<NewFn>(&::operator new)), fn};
  M[ES.intern("??_U@YAPEAX_K@Z")]  = {ExecutorAddr::fromPtr((void*)static_cast<NewFn>(&::operator new[])), fn};
  M[ES.intern("??3@YAXPEAX@Z")]    = {ExecutorAddr::fromPtr((void*)static_cast<DelFn>(&::operator delete)), fn};
  M[ES.intern("??3@YAXPEAX_K@Z")]  = {ExecutorAddr::fromPtr((void*)static_cast<SizedDelFn>(&::operator delete)), fn};
  M[ES.intern("??_V@YAXPEAX@Z")]   = {ExecutorAddr::fromPtr((void*)static_cast<DelFn>(&::operator delete[])), fn};
  M[ES.intern("??_V@YAXPEAX_K@Z")] = {ExecutorAddr::fromPtr((void*)static_cast<SizedDelFn>(&::operator delete[])), fn};
  {
    M[ES.intern("malloc")]           = {ExecutorAddr::fromPtr((void*)&::malloc), fn};
    M[ES.intern("calloc")]           = {ExecutorAddr::fromPtr((void*)&::calloc), fn};
    M[ES.intern("realloc")]          = {ExecutorAddr::fromPtr((void*)&::realloc), fn};
    M[ES.intern("free")]             = {ExecutorAddr::fromPtr((void*)&::free), fn};
    M[ES.intern("_msize")]           = {ExecutorAddr::fromPtr((void*)&::_msize), fn};
    M[ES.intern("_aligned_malloc")]  = {ExecutorAddr::fromPtr((void*)&::_aligned_malloc), fn};
    M[ES.intern("_aligned_realloc")] = {ExecutorAddr::fromPtr((void*)&::_aligned_realloc), fn};
    M[ES.intern("_aligned_free")]    = {ExecutorAddr::fromPtr((void*)&::_aligned_free), fn};
  }
  M[ES.intern("_CxxThrowException")] = {ExecutorAddr::fromPtr((void*)&hxcpp_JIT_throw_exception), fn};
  M[ES.intern("__emutls_get_address")] = {ExecutorAddr::fromPtr((void*)&hxcpp_JIT_emutls_get_address), fn};
  M[ES.intern("?_Facet_Register@std@@YAXPEAV_Facet_base@1@@Z")] = {ExecutorAddr::fromPtr((void*)&hxFacetRegister), fn};
  M[ES.intern("_Thrd_sleep_for")] = {ExecutorAddr::fromPtr((void*)&hxThrdSleepFor), fn};
  M[ES.intern("??_7type_info@@6B@")] = {ExecutorAddr::fromPtr(*(void* const*)&typeid(int)), llvm::JITSymbolFlags::Exported};

  for (const char* dll : {"msvcp140.dll", "msvcp140d.dll", "vcruntime140.dll", "vcruntime140d.dll"}) {
    HMODULE m = GetModuleHandleA(dll);
    if (!m) continue;
    std::string name = hxFindExport(m, "__ExceptionPtrCopyException");
    if (name.empty()) continue;
    FARPROC f = GetProcAddress(m, name.c_str());
    if (!f) continue;
    g_hxRealExcPtrCopy = reinterpret_cast<HxExcPtrCopyFn>(f);
    M[ES.intern(name)] = {ExecutorAddr::fromPtr((void*)&hxcpp_JIT_exception_ptr_copy), fn};
    if (hxTraceLevel() > 0) std::fprintf(stderr, "hxcpp_JIT EH: overriding %s from %s\n", name.c_str(), dll);
    break;
  }

  struct { const char* sym; const std::type_info* ti; } descs[] = {
      {"??_R0?AVexception@std@@@8",        &typeid(std::exception)},
      {"??_R0?AVruntime_error@std@@@8",    &typeid(std::runtime_error)},
      {"??_R0?AVlogic_error@std@@@8",      &typeid(std::logic_error)},
      {"??_R0?AVinvalid_argument@std@@@8", &typeid(std::invalid_argument)},
      {"??_R0?AVout_of_range@std@@@8",     &typeid(std::out_of_range)},
      {"??_R0?AVdomain_error@std@@@8",     &typeid(std::domain_error)},
      {"??_R0?AVlength_error@std@@@8",     &typeid(std::length_error)},
      {"??_R0?AVrange_error@std@@@8",      &typeid(std::range_error)},
      {"??_R0?AVoverflow_error@std@@@8",   &typeid(std::overflow_error)},
      {"??_R0?AVunderflow_error@std@@@8",  &typeid(std::underflow_error)},
      {"??_R0?AVbad_alloc@std@@@8",        &typeid(std::bad_alloc)},
      {"??_R0?AVbad_cast@std@@@8",         &typeid(std::bad_cast)},
  };
  for (auto& d : descs)
    if (void* p = hxCopyTypeDesc(*d.ti, *static_cast<HxArena*>(arenaContext)))
      M[ES.intern(d.sym)] = {ExecutorAddr::fromPtr(p), llvm::JITSymbolFlags::Exported};

  return J.getMainJITDylib().define(absoluteSymbols(std::move(M)));
#else
  return llvm::Error::success();
#endif
}
