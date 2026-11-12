#pragma once
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include <filesystem>
#include "shared_library.hpp"
std::filesystem::path hxcpp_JIT_mingw_root();
void hxcpp_JIT_configure_mingw_builder(llvm::orc::LLJITBuilder& builder,
    std::shared_ptr<hxjit::LibraryRegistry> libraries);
