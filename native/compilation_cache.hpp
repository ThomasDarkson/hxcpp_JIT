#pragma once

#include "clang/Basic/SourceManager.h"
#include "clang/Interpreter/PartialTranslationUnit.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include <fstream>
#include <stdexcept>

namespace hxcache {
namespace fs = std::filesystem;
static std::string read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read cache input: " + path.string());
  std::string result((std::istreambuf_iterator<char>(input)), {});
  if (input.bad()) throw std::runtime_error("Failed reading cache input: " + path.string());
  return result;
}
static void atomicWrite(const fs::path& path, llvm::StringRef bytes) {
  llvm::SmallString<256> temporary;
  int fd = -1;
  auto ec = llvm::sys::fs::createUniqueFile(path.string() + ".tmp-%%%%%%", fd, temporary);
  if (ec) throw std::runtime_error(ec.message());
  bool failed=false;
  {
    llvm::raw_fd_ostream output(fd, true);
    output << bytes; output.flush();
    failed=output.has_error();if(failed)output.clear_error();
  }
  if(failed) {
    llvm::sys::fs::remove(temporary);
    throw std::runtime_error("Failed writing compilation cache");
  }
  ec = llvm::sys::fs::rename(temporary, path.string());
  if (ec) {
    llvm::sys::fs::remove(temporary);
    throw std::runtime_error(ec.message());
  }
}
static void invalidate(const fs::path& directory) {
  std::error_code ignored;
  fs::remove(directory / "unit.json", ignored);
  fs::remove(directory / "unit.bc", ignored);
  fs::remove(directory / "unit.obj", ignored);
}
static std::string identity(JitEngine* e, const std::string& code) {
  llvm::json::Array args;
  for (const auto& arg : e->compilerArgs) args.push_back(arg);
  std::map<std::string, bool> sorted;
  for (const auto& feature : llvm::sys::getHostCPUFeatures())
    sorted[feature.getKey().str()] = feature.getValue();
  llvm::json::Object features;
  for (const auto& feature : sorted) features[feature.first] = feature.second;
  llvm::json::Object env;
  for (const char* name : {"INCLUDE", "LIB", "PATH", "VCToolsInstallDir", "WindowsSdkDir", "WindowsSDKVersion", "HXCPP_JIT_RESOURCE_DIR"})
    env[name] = std::getenv(name) ? std::getenv(name) : "";
  llvm::json::Object data{{"schema", 1}, {"llvm", LLVM_VERSION_STRING},
    {"dll", cacheHash(read(cacheLibraryPath()))},
    {"cpu", llvm::sys::getHostCPUName().str()}, {"features", std::move(features)},
    {"args", std::move(args)}, {"environment", std::move(env)},
    {"cwd", fs::current_path().string()}, {"history", e->historyKey}, {"code", code}};
  std::string text; llvm::raw_string_ostream stream(text); stream << llvm::json::Value(std::move(data));
  return cacheHash(stream.str());
}
static llvm::json::Array dependencies(JitEngine* e) {
  llvm::json::Array result;
  auto& manager = e->interp->getCompilerInstance()->getSourceManager();
  for (auto it = manager.fileinfo_begin(); it != manager.fileinfo_end(); ++it) {
    auto buffer = it->second->getBufferIfLoaded();
    if (!buffer) continue;
    auto requested = it->first.getName();
    if ((requested.starts_with("<") && requested.ends_with(">")) || requested.starts_with("input_line_")) continue;
    auto real = it->first.getFileEntry().tryGetRealPathName();
    auto name = fs::absolute(fs::path((real.empty()?requested:real).str())).lexically_normal().string();
    auto digest = cacheHash(buffer->getBuffer());
    if (cacheHash(read(name)) != digest) throw std::runtime_error("Input changed during compilation: " + name);
    result.push_back(llvm::json::Object{{"path", name}, {"sha256", digest}});
  }
  if (result.empty()) throw std::runtime_error("No file dependencies found");
  return result;
}
static bool load(JitEngine* e, const fs::path& directory, const std::string& key, std::string& bitcode) {
  try {
    auto parsed = llvm::json::parse(read(directory / "unit.json"));
    if (!parsed) { llvm::consumeError(parsed.takeError()); return false; }
    auto* record = parsed->getAsObject();
    if (!record || record->getString("key") != key) return false;
    auto* files = record->getArray("dependencies");
    if (!files || files->empty()) return false;
    std::map<std::string,std::string> recorded;
    for (const auto& value : *files) {
      auto* file = value.getAsObject();
      if (!file) return false;
      auto path = file->getString("path"); auto digest = file->getString("sha256");
      if (!path || !digest || cacheHash(read(path->str())) != *digest) return false;
      recorded[path->str()]=digest->str();
    }
    for(const auto& value : dependencies(e)) {
      auto* file=value.getAsObject();
      auto path=file->getString("path");auto digest=file->getString("sha256");
      auto found=recorded.find(path->str());
      if(found==recorded.end() || found->second!=*digest)return false;
    }
    bitcode = read(directory / "unit.bc");
    if (record->getString("bitcode") != cacheHash(bitcode)) return false;
    llvm::LLVMContext temporary;
    auto module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, "cache"), temporary);
    if (!module) { llvm::consumeError(module.takeError()); return false; }
    return true;
  } catch (...) { return false; }
}
}

static int evalCached(JitEngine* e, const std::string& code,
                      const std::string& declarations, const std::string& directory) {
  namespace fs = std::filesystem;
  e->cacheStatus.clear();
  if (runCode(e, declarations)) {hxcache::invalidate(directory);return 1;}
  std::string key;
  bool writable = true;
  try { key = hxcache::identity(e, code); fs::create_directories(directory); }
  catch (const std::exception& error) {
    writable = false; e->cacheStatus = std::string("Cache disabled: ") + error.what();
  }
  std::string bitcode;
  bool hit = writable && hxcache::load(e, directory, key, bitcode);
  if (!hit && writable) hxcache::invalidate(directory);
  e->diagText.clear();
  auto parsed = e->interp->Parse(hit ? ";" : code);
  if (!parsed) {
    auto message = errToString(parsed.takeError());e->diagStream->flush();
    e->lastError = e->diagText.empty() ? message : e->diagText;
    if(hit){hxcache::invalidate(directory);e->cacheStatus="Cache load failed; removed cached code";}
    repairAfterFailure(e);return 1;
  }
  if (!parsed->TheModule) {
    if(hit){hxcache::invalidate(directory);e->cacheStatus="Cache load failed; removed cached code";}
    e->lastError = "No LLVM module produced for cached compilation";return 1;
  }
  auto identifier = "hxcpp_cache_" + key;
  if(writable)parsed->TheModule->setModuleIdentifier(identifier);
  if (hit) {
    auto module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, parsed->TheModule->getModuleIdentifier()), parsed->TheModule->getContext());
    if (!module) {
      hxcache::invalidate(directory);e->cacheStatus="Cache load failed; removed cached code";
      e->lastError = errToString(module.takeError());return 1;
    }
    parsed->TheModule = std::move(*module);
  } else if (writable) {
    try {
      llvm::SmallVector<char, 0> data;llvm::raw_svector_ostream stream(data);
      llvm::WriteBitcodeToFile(*parsed->TheModule, stream);
      llvm::StringRef bytes(data.data(), data.size());
      llvm::json::Object record{{"key", key}, {"bitcode", cacheHash(bytes)},
        {"dependencies", hxcache::dependencies(e)}};
      std::string manifest;llvm::raw_string_ostream out(manifest);out << llvm::json::Value(std::move(record));
      hxcache::atomicWrite(fs::path(directory) / "unit.bc", bytes);
      hxcache::atomicWrite(fs::path(directory) / "unit.json", out.str());
      e->cacheStatus = "Compiled and cached";
    } catch (const std::exception& error) {
      hxcache::invalidate(directory);
      e->cacheStatus = std::string("Compiled; cache write skipped: ") + error.what();
    }
  }
  if (writable) {
    llvm::SmallVector<char,0> data;llvm::raw_svector_ostream stream(data);
    llvm::WriteBitcodeToFile(*parsed->TheModule,stream);
    e->objectCache.activate(directory,parsed->TheModule->getModuleIdentifier(),
        cacheHash(key + (hit ? cacheHash(bitcode) : cacheHash(llvm::StringRef(data.data(),data.size())))));
  }
  if (auto error = e->interp->Execute(*parsed)) {
    e->objectCache.deactivate();
    hxcache::invalidate(directory);
    if(hit)e->cacheStatus="Cache load failed; removed cached code";
    e->lastError = errToString(std::move(error));return 1;
  }
  e->historyKey = cacheHash(e->historyKey + code);
  if (hit) e->cacheStatus = e->objectCache.hit ? "Loaded compiled cache (IR + machine code)" : e->objectCache.attempted ? "Loaded cached IR; rebuilt machine code" : "Loaded cached IR; machine code deferred until first use";
  if (!e->objectCache.warning.empty()) e->cacheStatus += "; object cache: " + e->objectCache.warning;
  e->lastError.clear();return 0;
}
