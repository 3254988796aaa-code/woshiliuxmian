# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
set(SEEKDB_WASM_RUST_DIR "${SEEKDB_ROOT}/build_wasm_rust_runtime" CACHE PATH
  "Directory built by tools/wasm/build-rust-nio.sh")
set(nio_archive "${SEEKDB_WASM_RUST_DIR}/exports/libsql_nio.a")
if(NOT EXISTS "${nio_archive}")
  message(FATAL_ERROR "Build Rust memory NIO with tools/wasm/build-rust-nio.sh ${SEEKDB_WASM_RUST_DIR}")
endif()
add_library(nio STATIC IMPORTED GLOBAL)
add_custom_target(nio_check
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_ROOT}/tools/wasm/rust-nio-manifest.py"
    "${SEEKDB_WASM_RUST_DIR}"
  VERBATIM)
add_dependencies(nio nio_check)
set_target_properties(nio PROPERTIES
  IMPORTED_LOCATION "${nio_archive}"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_ROOT}/rust/sql-nio/include")
list(APPEND engine_libraries nio)

# rust/build.py exports all C++ interfaces from the shared Rust archive.
foreach(crate config local logger)
  set(archive "${SEEKDB_WASM_RUST_DIR}/exports/lib${crate}.a")
  if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "Build the Rust crates with tools/wasm/build-rust-nio.sh ${SEEKDB_WASM_RUST_DIR}")
  endif()
  add_library(${crate} STATIC IMPORTED GLOBAL)
  add_dependencies(${crate} nio_check)
  set_target_properties(${crate} PROPERTIES IMPORTED_LOCATION "${archive}")
  list(APPEND engine_libraries ${crate})
endforeach()
