#pragma once
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <filesystem>
#include <mutex>
#include <map>

class CompilationObjectCache : public llvm::ObjectCache {
  std::mutex mutex;
  std::string identifier;
  struct Entry {std::string directory,key;};
  std::map<std::string,Entry> entries;
public:
  bool hit = false;
  bool attempted = false;
  std::string warning;
  void activate(std::string dir, std::string module, std::string identity) {
    std::lock_guard<std::mutex> lock(mutex);
    identifier=std::move(module);entries[identifier]={std::move(dir),std::move(identity)};
    hit=false;attempted=false;warning.clear();
  }
  void deactivate() {
    std::lock_guard<std::mutex> lock(mutex);entries.erase(identifier);identifier.clear();
  }
  std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module* module) override {
    std::lock_guard<std::mutex> lock(mutex);
    auto entry=entries.find(module->getModuleIdentifier());if(entry==entries.end())return nullptr;
    if(module->getModuleIdentifier()==identifier)attempted=true;
    const auto& directory=entry->second.directory;const auto& key=entry->second.key;
    auto file=llvm::MemoryBuffer::getFile(directory+"/unit.obj",false);
    if(!file)return nullptr;
    auto bytes=(*file)->getBuffer();
    if(bytes.size()<130 || bytes[64]!='\n' || bytes[129]!='\n' || bytes.substr(65,64)!=key)return nullptr;
    auto object=bytes.drop_front(130);
    if(cacheHash(object)!=bytes.take_front(64))return nullptr;
    if(module->getModuleIdentifier()==identifier)hit=true;
    return llvm::MemoryBuffer::getMemBufferCopy(object,"cached object");
  }
  void notifyObjectCompiled(const llvm::Module* module, llvm::MemoryBufferRef object) override {
    std::lock_guard<std::mutex> lock(mutex);
    auto entry=entries.find(module->getModuleIdentifier());if(entry==entries.end())return;
    const auto& directory=entry->second.directory;const auto& key=entry->second.key;
    llvm::SmallString<256> temporary;int fd=-1;
    auto ec=llvm::sys::fs::createUniqueFile(directory+"/unit.obj.tmp-%%%%%%",fd,temporary);
    if(ec){warning=ec.message();return;}
    bool failed=false;
    {
      llvm::raw_fd_ostream output(fd,true);
      output<<cacheHash(object.getBuffer())<<'\n'<<key<<'\n'<<object.getBuffer();output.flush();
      failed=output.has_error();if(failed)output.clear_error();
    }
    if(!failed)ec=llvm::sys::fs::rename(temporary,directory+"/unit.obj");
    if(failed || ec){llvm::sys::fs::remove(temporary);warning=failed?"Failed writing cached object":ec.message();}
  }
};
