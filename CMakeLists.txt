cmake_minimum_required(VERSION 3.16)
project(hxcpp_JIT CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "hxcpp_JIT only supports Windows.")
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

get_filename_component(_llvm_bin "${LLVM_TOOLS_BINARY_DIR}" ABSOLUTE)
set(CLANG_RESOURCE_DIR "")
foreach(_exe "${_llvm_bin}/clang" "${_llvm_bin}/clang.exe")
  if(EXISTS "${_exe}" AND CLANG_RESOURCE_DIR STREQUAL "")
    execute_process(COMMAND "${_exe}" -print-resource-dir
                    OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endforeach()
file(TO_CMAKE_PATH "${CLANG_RESOURCE_DIR}" CLANG_RESOURCE_DIR)

add_library(hxcpp_JIT SHARED native/hxcpp_JIT.cpp native/msvc_runtime.cpp)
target_include_directories(hxcpp_JIT PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} native)
target_compile_definitions(hxcpp_JIT PRIVATE NOMINMAX ${LLVM_DEFINITIONS})
if(NOT CLANG_RESOURCE_DIR STREQUAL "")
  target_compile_definitions(hxcpp_JIT PRIVATE HXCPP_JIT_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}")
endif()

if(MSVC)
  set_source_files_properties(native/hxcpp_JIT.cpp PROPERTIES COMPILE_OPTIONS "/GR-")
else()
  set_source_files_properties(native/hxcpp_JIT.cpp PROPERTIES COMPILE_OPTIONS "-fno-rtti")
endif()

llvm_map_components_to_libnames(_llvm_libs core support orcjit native bitreader bitwriter targetparser)
target_link_libraries(hxcpp_JIT PRIVATE clangInterpreter clangFrontend ${_llvm_libs})
set_target_properties(hxcpp_JIT PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_link_libraries(hxcpp_JIT PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(hxcpp_JIT PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
