#pragma once
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include <filesystem>
std::filesystem::path hxcpp_JIT_mingw_root();
void hxcpp_JIT_configure_mingw_builder(llvm::orc::LLJITBuilder& builder);
