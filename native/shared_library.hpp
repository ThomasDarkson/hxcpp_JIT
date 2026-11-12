#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hxjit {

class LibraryRegistry {
#ifdef _WIN32
  using Handle = HMODULE;
#else
  using Handle = void*;
#endif
  std::vector<std::pair<std::filesystem::path, Handle>> libraries;
  std::map<std::string, std::unique_ptr<void*>> imports;

public:
  LibraryRegistry() = default;
  LibraryRegistry(const LibraryRegistry&) = delete;
  LibraryRegistry& operator=(const LibraryRegistry&) = delete;

  ~LibraryRegistry() {
    for (auto entry = libraries.rbegin(); entry != libraries.rend(); ++entry) {
#ifdef _WIN32
      FreeLibrary(entry->second);
#else
      dlclose(entry->second);
#endif
    }
  }

  void load(const std::filesystem::path& path) {
    const auto full = std::filesystem::canonical(path);
    for (const auto& entry : libraries)
      if (entry.first == full) return;
#ifdef _WIN32
    Handle handle = LoadLibraryExW(full.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle) throw std::runtime_error("Cannot load library " + full.u8string() +
        " (Windows error " + std::to_string(GetLastError()) + ")");
#else
    Handle handle = dlopen(full.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) throw std::runtime_error("Cannot load library " + full.string() + ": " + dlerror());
#endif
    try { libraries.emplace_back(full, handle); }
    catch (...) {
#ifdef _WIN32
      FreeLibrary(handle);
#else
      dlclose(handle);
#endif
      throw;
    }
  }

  void* find(const std::string& name) {
    const bool indirect = name.rfind("__imp_", 0) == 0;
    const char* exported = name.c_str() + (indirect ? 6 : 0);
    void* address = nullptr;
    for (const auto& entry : libraries) {
#ifdef _WIN32
      address = reinterpret_cast<void*>(GetProcAddress(entry.second, exported));
#else
      address = dlsym(entry.second, exported);
#endif
      if (address) break;
    }
    if (!address || !indirect) return address;
    auto& slot = imports[name];
    if (!slot) slot = std::make_unique<void*>(address);
    return slot.get();
  }
};

}
