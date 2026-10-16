# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libroaring.a")
  message(FATAL_ERROR "Build CRoaring with tools/wasm/build-roaring.sh")
endif()
add_library(roaring STATIC IMPORTED GLOBAL)
set_target_properties(roaring PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libroaring.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
list(APPEND engine_libraries roaring)
