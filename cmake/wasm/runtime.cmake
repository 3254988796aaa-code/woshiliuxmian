# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(runtime_inventory "${CMAKE_CURRENT_BINARY_DIR}/wasm-runtime-sources.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SEEKDB_ROOT}/src/oblib/oblib_source_inventory.bzl"
  "${SEEKDB_ROOT}/src/sql/sql_source_inventory.bzl"
  "${SEEKDB_ROOT}/tools/wasm/emit-runtime-sources.py"
  "${SEEKDB_ROOT}/tools/wasm/runtime-components.json"
  "${SEEKDB_ROOT}/tools/cmake/emit_bazel_source_inventory.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${SEEKDB_ROOT}/tools/wasm/emit-runtime-sources.py"
  --repo "${SEEKDB_ROOT}" --output "${runtime_inventory}"
  COMMAND_ERROR_IS_FATAL ANY)
include("${runtime_inventory}")
add_library(memory_objects OBJECT EXCLUDE_FROM_ALL ${ALLOCATOR_SOURCES})
target_compile_features(memory_objects PRIVATE cxx_std_20)
target_compile_options(memory_objects PRIVATE -pthread)
target_include_directories(memory_objects PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
add_library(memory STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:memory_objects>)
add_library(runtime_support STATIC EXCLUDE_FROM_ALL ${RUNTIME_SUPPORT_SOURCES})
target_compile_features(runtime_support PRIVATE cxx_std_20)
target_compile_options(runtime_support PRIVATE -pthread -sUSE_ZLIB=1)
target_include_directories(runtime_support PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
