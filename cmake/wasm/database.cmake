# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Browser/Node module with the real database lifecycle and memory protocol ABI.
# WasmFS provides both storage modes: its memory backend by default, and the
# origin private file system mounted at the data directory on request.
add_executable(database EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/src/wasm/database_runtime.cpp"
  "${SEEKDB_ROOT}/src/wasm/futex_wait_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/wasmfs_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/wasmfs_fd_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/chunked_memory_backend.cpp")
target_include_directories(database PRIVATE "${EMSCRIPTEN_ROOT_PATH}/system/lib/wasmfs")
target_link_libraries(database PRIVATE ${engine_libraries})
target_compile_definitions(database PRIVATE SEEKDB_WASMFS=1)
# The generated loader keeps its own name next to the database.mjs facade.
set_target_properties(database PROPERTIES SUFFIX ".mjs" OUTPUT_NAME seekdb_wasm_database)
# Keep the deployable JS facade and Worker next to the generated module/Wasm.
# configure_file also makes source updates trigger CMake regeneration.
set(modules database database-worker storage-cleanup-worker worker-server runtime-host mysql-client mysql-transport
  mysql-wire mysql-auth shell shell-sql shell-examples shell-format)
foreach(module ${modules})
  configure_file("${SEEKDB_ROOT}/src/wasm/${module}.mjs" "${CMAKE_CURRENT_BINARY_DIR}/${module}.mjs" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/src/wasm/engine-version.mjs.in"
  "${CMAKE_CURRENT_BINARY_DIR}/engine-version.mjs" @ONLY)
foreach(asset shell.html shell.css)
  configure_file("${SEEKDB_ROOT}/src/wasm/${asset}" "${CMAKE_CURRENT_BINARY_DIR}/${asset}" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/src/wasm/database-console.html"
  "${CMAKE_CURRENT_BINARY_DIR}/database-console.html" COPYONLY)
set(database_link_options
  -Oz --emit-symbol-map -pthread -msimd128 -sUSE_ZLIB=1 -sPROXY_TO_PTHREAD=1
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sPTHREAD_POOL_SIZE=64 -sPTHREAD_POOL_SIZE_STRICT=2 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648
  -sSTACK_SIZE=2097152 -sASSERTIONS=2 -sEXIT_RUNTIME=1 -Wl,--error-limit=0
  -Wl,--wrap=emscripten_futex_wait
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_nio_memory_read','_nio_memory_write','_nio_memory_close']"
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getValue']")
# wasmfs_adapter.cpp wraps these calls to adjust WasmFS behavior and report failures.
set(wasmfs_wrapped_calls __syscall_openat __syscall_newfstatat __syscall_stat64 __syscall_lstat64
  __syscall_fstat64 __syscall_mkdirat __syscall_fcntl64 __syscall_renameat __syscall_ftruncate64
  __syscall_truncate64 __syscall_fallocate __syscall_getcwd __syscall_getdents64 __syscall_unlinkat
  __syscall_rmdir __syscall_statfs64 __syscall_fstatfs64 __syscall_fdatasync __syscall_faccessat
  __syscall_chdir __syscall_ioctl __syscall_utimensat __wasi_fd_pread __wasi_fd_pwrite __wasi_fd_sync
  __wasi_fd_seek __wasi_fd_close __syscall_dup3)
set(wasmfs_wrap_options)
foreach(call ${wasmfs_wrapped_calls})
  list(APPEND wasmfs_wrap_options "-Wl,--wrap=${call}")
endforeach()
target_link_options(database PRIVATE ${database_link_options} -sWASMFS
  ${wasmfs_wrap_options})

# The same engine on the legacy JavaScript file system, in-memory mode only,
# kept for comparison. Serve it with --build-dir build_wasm_engine/memfs.
add_executable(database_memfs EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/src/wasm/database_runtime.cpp"
  "${SEEKDB_ROOT}/src/wasm/futex_wait_adapter.cpp")
target_link_libraries(database_memfs PRIVATE ${engine_libraries})
set_target_properties(database_memfs PROPERTIES SUFFIX ".mjs" OUTPUT_NAME seekdb_wasm_database
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/memfs")
foreach(module ${modules})
  configure_file("${SEEKDB_ROOT}/src/wasm/${module}.mjs" "${CMAKE_CURRENT_BINARY_DIR}/memfs/${module}.mjs" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/src/wasm/engine-version.mjs.in"
  "${CMAKE_CURRENT_BINARY_DIR}/memfs/engine-version.mjs" @ONLY)
target_link_options(database_memfs PRIVATE ${database_link_options})
