# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
foreach(library vsag_kernels vsag_core vsag_hnsw vsag_blas vsag_antlr cpuinfo fmt)
  if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/lib${library}.a")
    message(FATAL_ERROR "Build VSAG with tools/wasm/build-vsag.sh")
  endif()
  add_library(${library} STATIC IMPORTED GLOBAL)
  set_target_properties(${library} PROPERTIES
    IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/lib${library}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include;${SEEKDB_WASM_DEPS}/include/seekdb-vsag")
endforeach()
target_link_libraries(vsag_kernels INTERFACE cpuinfo)
target_link_libraries(vsag_core INTERFACE vsag_kernels fmt)
target_link_libraries(vsag_hnsw INTERFACE vsag_core roaring vsag_blas vsag_antlr)
target_include_directories(vsag_hnsw INTERFACE "${SEEKDB_WASM_DEPS}/include/roaring")
target_compile_definitions(vsag_hnsw INTERFACE VSAG_DISABLE_STATIC_HNSW HAVE_LIBAIO=0)
list(APPEND engine_libraries vsag_hnsw)
