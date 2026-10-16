# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
foreach(component common i18n data)
  if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libicu_${component}.a")
    message(FATAL_ERROR "Build ICU 69.1 with tools/wasm/build-icu.sh")
  endif()
  add_library(icu_${component} STATIC IMPORTED GLOBAL)
  set_target_properties(icu_${component} PROPERTIES
    IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libicu_${component}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
endforeach()
target_link_libraries(icu_i18n INTERFACE icu_common)
target_link_libraries(icu_common INTERFACE icu_data)
list(APPEND engine_libraries icu_i18n)
