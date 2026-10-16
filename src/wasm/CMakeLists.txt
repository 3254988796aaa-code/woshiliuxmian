# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(seekdb_wasm LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure this target with Emscripten emcmake")
endif()
get_filename_component(SEEKDB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SEEKDB_WASM_RUST_DIR "${SEEKDB_ROOT}/build_wasm_rust_runtime" CACHE PATH
  "Directory built by tools/wasm/build-rust-nio.sh")
# Engine sources throughout include config_bridge.h and local_bridge.h, which
# rust/build.py exports into SEEKDB_WASM_RUST_DIR/exports/include.
include_directories("${SEEKDB_ROOT}/rust/config/include" "${SEEKDB_ROOT}/rust/local/include"
  "${SEEKDB_WASM_RUST_DIR}/exports/include")
add_compile_definitions(_NO_EXCEPTION __STDC_LIMIT_MACROS __STDC_CONSTANT_MACROS
  DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR)
add_compile_options(-fno-strict-aliasing)
# The parser uses longjmp and VSAG propagates C++ exceptions. Use Emscripten's
# JS exception model consistently, including library cleanup. Native Wasm EH
# currently triggers a V8 optimizer OOM on this generated SQL parser with the
# pinned SDK/Node, even at -Oz; do not disable JIT checks to hide that failure.
add_compile_options(-fexceptions)
add_link_options(-fexceptions)
# The warnings native clang builds turn off, except -Wformat: long is 32-bit on
# wasm32, so %ld given an int64_t prints the wrong value here.
include("${SEEKDB_ROOT}/cmake/clang_warnings.cmake")
set(disabled_warnings ${SEEKDB_CLANG_DISABLED_WARNINGS})
list(REMOVE_ITEM disabled_warnings -Wno-format)
# Emscripten's clang is newer than the pinned native Clang 17. These are the
# diagnostics src/oblib/CMakeLists.txt also turns off for the macOS 27 host LLVM.
include(CheckCXXCompilerFlag)
foreach(warning vla-cxx-extension unnecessary-virtual-specifier nontrivial-memcall
    cast-function-type-mismatch)
  string(MAKE_C_IDENTIFIER "HAS_${warning}" supported)
  check_cxx_compiler_flag("-W${warning}" ${supported})
  if(${supported})
    list(APPEND disabled_warnings "-Wno-${warning}")
  endif()
endforeach()
# One quoted argument keeps each $<COMPILE_LANGUAGE:...> group, which spans
# several list items, in a single entry.
add_compile_options("${disabled_warnings}")
include("${SEEKDB_ROOT}/cmake/wasm/runtime.cmake")
include("${SEEKDB_ROOT}/cmake/generate_inner_tables.cmake")
seekdb_generate_inner_tables("${SEEKDB_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
include("${SEEKDB_ROOT}/cmake/wasm/parser.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/engine.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/startup-sources.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/engine-libraries.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/roaring.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/xml.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/icu.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/s2.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/protobuf.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/vsag.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/nio.cmake")
include("${SEEKDB_ROOT}/cmake/wasm/database.cmake")
