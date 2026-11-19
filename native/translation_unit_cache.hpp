#pragma once

#include "clang/CodeGen/CodeGenAction.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Threading.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string_view>
#include <system_error>

class CachedTranslationUnitAction final : public clang::EmitLLVMOnlyAction {
  bool trackDependencies;

  void EndSourceFileAction() override {
    if (trackDependencies) {
      try {
        dependencies = hxcache::dependencies(getCompilerInstance().getSourceManager());
      } catch (const std::exception& error) {
        cacheWarning = error.what();
      }
    }
    clang::EmitLLVMOnlyAction::EndSourceFileAction();
  }

public:
  llvm::json::Array dependencies;
  std::string cacheWarning;

  CachedTranslationUnitAction(llvm::LLVMContext& context, bool trackDependencies)
      : clang::EmitLLVMOnlyAction(&context), trackDependencies(trackDependencies) {}
};

struct CachedSource {
  llvm::StringRef path;
  std::filesystem::path directory;
  std::string key;
  clang::PartialTranslationUnit* unit;
  std::string bitcode;
  std::string digest;
  llvm::json::Array dependencies;
  std::string cacheWarning;
  std::string diagnostics;
  std::string error;
  std::chrono::steady_clock::duration compileTime{};
};

static void compileTranslationUnit(const std::vector<std::string>& compilerArgs,
                                  CachedSource& source, bool trackDependencies) {
  llvm::LLVMContext context;
  llvm::raw_string_ostream diagnostics(source.diagnostics);
  std::vector<const char*> args;
  for (const auto& arg : compilerArgs) args.push_back(arg.c_str());
  clang::IncrementalCompilerBuilder builder;
  builder.SetCompilerArgs(args);
  auto compiler = builder.CreateCpp();
  if (!compiler) throw std::runtime_error(errToString(compiler.takeError()));
  auto& ci = **compiler;
  ci.getLangOpts().IncrementalExtensions = false;
  ci.getFrontendOpts().Inputs.clear();
  ci.getFrontendOpts().Inputs.emplace_back(source.path, clang::InputKind(clang::Language::CXX));
  ci.getDiagnostics().setClient(
      new clang::TextDiagnosticPrinter(diagnostics, ci.getDiagnosticOpts()), true);

  CachedTranslationUnitAction action(context, trackDependencies);
  if (!ci.ExecuteAction(action)) {
    diagnostics.flush();
    throw std::runtime_error(source.diagnostics.empty() ? "C++ compilation failed" : source.diagnostics);
  }
  auto module = action.takeModule();
  if (!module) throw std::runtime_error("No LLVM module produced");
  source.dependencies = std::move(action.dependencies);
  source.cacheWarning = std::move(action.cacheWarning);
  llvm::raw_string_ostream output(source.bitcode);
  llvm::WriteBitcodeToFile(*module, output);
  source.digest = cacheHash(output.str());
}

static size_t prioritizeCachedFiles(std::vector<CachedSource*>& queue, std::string_view priorities) {
  auto next = queue.begin();
  while (!priorities.empty() && next != queue.end()) {
    const auto separator = priorities.find(',');
    auto name = priorities.substr(0, separator);
    const auto first = name.find_first_not_of(" \t");
    if (first != std::string_view::npos) {
      name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
      auto match = std::find_if(next, queue.end(), [name](const CachedSource* source) {
        return std::filesystem::path(source->path.str()).filename().string() == name;
      });
      if (match != queue.end()) {
        std::rotate(next, match, match + 1);
        ++next;
      }
    }
    if (separator == std::string_view::npos) break;
    priorities.remove_prefix(separator + 1);
  }
  return static_cast<size_t>(next - queue.begin());
}

static size_t compileCacheMisses(const std::vector<std::string>& compilerArgs,
                                 const std::vector<CachedSource*>& misses,
                                 size_t maxWorkers, bool trackDependencies) {
  if (misses.empty()) return 0;
  const size_t limit = llvm::llvm_is_multithreaded() ? std::min(maxWorkers, misses.size()) : 1;
  std::atomic<size_t> next{0};
  auto compile = [&] {
    for (size_t index = next.fetch_add(1); index < misses.size(); index = next.fetch_add(1)) {
      auto& source = *misses[index];
      const auto started = std::chrono::steady_clock::now();
      try {
        compileTranslationUnit(compilerArgs, source, trackDependencies);
      } catch (const std::exception& error) {
        source.error = error.what();
      } catch (...) {
        source.error = "Unknown C++ compilation failure";
      }
      source.compileTime = std::chrono::steady_clock::now() - started;
    }
  };
  std::vector<std::future<void>> workers;
  for (size_t i = 1; i < limit; ++i) {
    try {
      workers.push_back(std::async(std::launch::async, compile));
    } catch (const std::system_error&) {
      break;
    }
  }
  compile();
  for (auto& worker : workers) worker.get();
  return workers.size() + 1;
}

static std::string cachedFilesMilliseconds(std::chrono::steady_clock::duration elapsed) {
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + " ms";
}

static void appendSlowestCachedFiles(std::string& status, std::vector<CachedSource*>& misses) {
  if (misses.empty()) return;
  const size_t count = std::min<size_t>(5, misses.size());
  std::partial_sort(misses.begin(), misses.begin() + count, misses.end(),
      [](const CachedSource* left, const CachedSource* right) {
        if (left->compileTime != right->compileTime) return left->compileTime > right->compileTime;
        return left->path < right->path;
      });
  status += "\nJIT slowest compiled files:";
  for (size_t i = 0; i < count; ++i) {
    const auto& source = *misses[i];
    status += "\n  " + std::filesystem::path(source.path.str()).filename().string() +
        " took " + cachedFilesMilliseconds(source.compileTime);
  }
}

static clang::PartialTranslationUnit& parseCacheInput(JitEngine* e, llvm::StringRef code) {
  e->diagText.clear();
  auto parsed = e->interp->Parse(code);
  if (!parsed) {
    auto message = errToString(parsed.takeError());
    e->diagStream->flush();
    repairAfterFailure(e);
    throw std::runtime_error(e->diagText.empty() ? message : e->diagText);
  }
  if (!parsed->TheModule) throw std::runtime_error("No LLVM module produced for cached files");
  return *parsed;
}

static int evalCachedFiles(JitEngine* e, llvm::StringRef input, const std::string& declarations,
                            const std::filesystem::path& directory, int maxWorkers, const char* compileFirst) {
  namespace fs = std::filesystem;
  using Clock = std::chrono::steady_clock;
  const auto cacheStarted = Clock::now();
  e->cacheStatus.clear();
  if (maxWorkers < 1) throw std::runtime_error("Compile worker limit must be at least 1");

  llvm::SmallVector<llvm::StringRef> sources;
  input.split(sources, '\n', -1, false);
  if (sources.empty()) throw std::runtime_error("Choose at least one C++ source file");
  llvm::StringSet<> unique;
  for (auto source : sources) {
    if (!unique.insert(source).second) throw std::runtime_error("Duplicate C++ source: " + source.str());
  }

  std::string identity;
  std::string warning;
  try {
    identity = hxcache::identity(e, "separate translation units v1");
    fs::create_directories(directory);
  } catch (const std::exception& error) {
    identity.clear();
    warning = error.what();
  }

  std::vector<CachedSource> batch;
  batch.reserve(sources.size());
  std::vector<CachedSource*> misses;
  const bool trace = std::getenv("HXCPP_JIT_TRACE_CACHE") != nullptr;
  for (auto source : sources) {
    auto pathKey = cacheHash(source);
    auto& parsed = parseCacheInput(e, ";");
    batch.push_back({source, directory / "sources" / pathKey, cacheHash(identity + pathKey), &parsed});
    auto& entry = batch.back();
    std::string bitcode;
    std::string missReason;
    if (!identity.empty() && hxcache::load(nullptr, entry.directory, entry.key, bitcode, missReason)) {
      auto cached = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, source),
                                           parsed.TheModule->getContext());
      if (cached) {
        parsed.TheModule = std::move(*cached);
        entry.digest = cacheHash(bitcode);
      } else {
        missReason = errToString(cached.takeError());
      }
    }
    bool hit = !entry.digest.empty();
    if (trace) {
      std::fprintf(stderr, "hxcpp_JIT cache %s: %s%s%s\n", hit ? "hit" : "miss",
          source.str().c_str(), missReason.empty() ? "" : "; ", missReason.c_str());
    }
    if (!hit) misses.push_back(&entry);
  }

  auto compileQueue = misses;
  const char* priority = compileFirst ? compileFirst : std::getenv("HXCPP_JIT_COMPILE_FIRST");
  const size_t prioritized = priority ? prioritizeCachedFiles(compileQueue, priority) : 0;

  const auto compileStarted = Clock::now();
  const auto workers = compileCacheMisses(e->compilerArgs, compileQueue, maxWorkers, !identity.empty());
  const auto compileEnded = Clock::now();
  if (trace) std::fprintf(stderr, "hxcpp_JIT cache: %zu compile worker(s) for %zu miss(es)\n", workers, misses.size());
  std::string errors;
  for (auto* source : misses) {
    if (!source->error.empty()) {
      if (!errors.empty()) errors += '\n';
      errors += source->path.str() + ": " + source->error;
    } else if (!source->diagnostics.empty()) {
      std::fputs(source->diagnostics.c_str(), stderr);
    }
  }
  if (!errors.empty()) throw std::runtime_error(errors);

  for (auto* source : misses) {
    auto& module = source->unit->TheModule;
    auto compiled = llvm::parseBitcodeFile(llvm::MemoryBufferRef(source->bitcode, source->path),
                                           module->getContext());
    if (!compiled) throw std::runtime_error(source->path.str() + ": " + errToString(compiled.takeError()));
    module = std::move(*compiled);
    if (!identity.empty() && source->cacheWarning.empty()) {
      try {
        fs::create_directories(source->directory);
        hxcache::save(source->directory, source->key, source->bitcode, std::move(source->dependencies));
      } catch (const std::exception& error) {
        source->cacheWarning = error.what();
      }
    }
    if (warning.empty() && !source->cacheWarning.empty())
      warning = source->path.str() + ": " + source->cacheWarning;
    std::string().swap(source->bitcode);
  }

  auto& interface = parseCacheInput(e, declarations);
  std::string fingerprints;
  for (auto& source : batch) {
    fingerprints += cacheHash(source.path) + source.digest;
    if (!identity.empty()) {
      auto identifier = "hxcpp_cached_tu_" + source.key;
      source.unit->TheModule->setModuleIdentifier(identifier);
      e->objectCache.activate(source.directory.string(), identifier, cacheHash(source.key + source.digest));
    }
  }

  const auto registrationStarted = Clock::now();
  auto executor = e->interp->getExecutionEngine();
  if (!executor) throw std::runtime_error(errToString(executor.takeError()));
  for (auto& source : batch) {
    if (auto error = executor->addModule(*source.unit))
      throw std::runtime_error(errToString(std::move(error)));
  }
  if (auto error = executor->addModule(interface))
    throw std::runtime_error(errToString(std::move(error)));
  const auto initializationStarted = Clock::now();
  if (auto error = executor->runCtors())
    throw std::runtime_error(errToString(std::move(error)));

  const auto initializationEnded = Clock::now();

  e->historyKey = cacheHash(e->historyKey + "\ncached translation units\n" +
      cacheHash(declarations) + fingerprints);
  e->cacheStatus = "Reused " + std::to_string(sources.size() - misses.size()) + " cached IR; compiled " +
      std::to_string(misses.size()) + " source file(s)";
  if (!warning.empty()) e->cacheStatus += "; cache write skipped: " + warning;
  if (!e->objectCache.warning.empty()) e->cacheStatus += "; object cache: " + e->objectCache.warning;
  e->cacheStatus += "\nJIT timings: cache lookup/IR load " + cachedFilesMilliseconds(compileStarted - cacheStarted) +
      ", compile batch " + cachedFilesMilliseconds(compileEnded - compileStarted) +
      " (" + std::to_string(workers) + " workers), IR/cache preparation " +
      cachedFilesMilliseconds(registrationStarted - compileEnded) +
      ", module registration " + cachedFilesMilliseconds(initializationStarted - registrationStarted) +
      ", JIT materialization/initializers " + cachedFilesMilliseconds(initializationEnded - initializationStarted) +
      " (ORC compile pool: " + std::to_string(e->codegenThreads) + " threads)";
  if (prioritized != 0) {
    e->cacheStatus += "\nJIT compile queue priority:";
    for (size_t i = 0; i < prioritized; ++i)
      e->cacheStatus += " " + std::filesystem::path(compileQueue[i]->path.str()).filename().string();
  }
  appendSlowestCachedFiles(e->cacheStatus, misses);
  e->lastError.clear();
  return 0;
}
