# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
# Host parser generators are distinct from target-side Wasm dependencies.
set(SEEKDB_HOST_DEVTOOLS "${SEEKDB_ROOT}/deps/3rd/usr/local/oceanbase/devtools" CACHE PATH
  "Host tools containing bison 2.4.1, flex and share/bison")
find_program(SEEKDB_HOST_BISON bison HINTS "${SEEKDB_HOST_DEVTOOLS}/bin"
  NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_program(SEEKDB_HOST_FLEX flex HINTS "${SEEKDB_HOST_DEVTOOLS}/bin"
  NO_CMAKE_FIND_ROOT_PATH REQUIRED)
set(SEEKDB_BISON_DATA "${SEEKDB_HOST_DEVTOOLS}/share/bison" CACHE PATH "Bison 2.4.1 data")
set(SEEKDB_HEADER_DEPS "${SEEKDB_ROOT}/deps/3rd/usr/local/oceanbase/deps/devel/include"
  CACHE PATH "Source headers for target-independent dependencies")
set(SEEKDB_WASM_DEPS "${SEEKDB_ROOT}/build_wasm_deps" CACHE PATH
  "Dependencies cross-compiled by tools/wasm/build-deps.sh")
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libcrypto.a")
  message(FATAL_ERROR "Build Wasm dependencies with tools/wasm/build-deps.sh first")
endif()
add_library(crypto STATIC IMPORTED)
set_target_properties(crypto PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libcrypto.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
if(NOT EXISTS "${SEEKDB_HEADER_DEPS}/fast_float/fast_float.h")
  message(FATAL_ERROR "fast_float source headers are required in SEEKDB_HEADER_DEPS")
endif()
# Expose selected headers without rewriting already verified adaptations on
# every configure (that would invalidate the full engine's dependency graph).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SEEKDB_ROOT}/tools/wasm/prepare-headers.py"
  "${SEEKDB_ROOT}/tools/wasm/dependencies.json")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${SEEKDB_ROOT}/tools/wasm/prepare-headers.py"
  --include "${CMAKE_CURRENT_BINARY_DIR}/third_party/include"
  --source "${SEEKDB_HEADER_DEPS}"
  --target-prefix "${SEEKDB_WASM_DEPS}"
  COMMAND_ERROR_IS_FATAL ANY)
set(parser_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/sql/parser")
set(parser_generated_sources)
foreach(name ftsblex_lex.c ftsparser_tab.c sql_parser_mysql_mode_lex.c
    sql_parser_mysql_mode_tab.c type_name.c)
  list(APPEND parser_generated_sources "${parser_generated_dir}/${name}")
endforeach()
set(parser_generated_headers)
foreach(name ftsblex_lex.h ftsparser_tab.h sql_parser_mysql_mode_lex.h sql_parser_mysql_mode_tab.h)
  list(APPEND parser_generated_headers "${parser_generated_dir}/${name}")
endforeach()
add_custom_command(OUTPUT ${parser_generated_sources} ${parser_generated_headers}
  COMMAND bash "${SEEKDB_ROOT}/tools/bazel_migration/generate_sql_parser.sh"
    "${SEEKDB_HOST_BISON}" "${SEEKDB_HOST_FLEX}" "${SEEKDB_BISON_DATA}"
    "${parser_generated_dir}" "${SEEKDB_ROOT}/src/sql/parser"
    "${SEEKDB_ROOT}/src/query/api/query/parser/ob_item_type.h"
  DEPENDS
    "${SEEKDB_ROOT}/tools/bazel_migration/generate_sql_parser.sh"
    "${SEEKDB_ROOT}/src/sql/parser/ftsblex.l"
    "${SEEKDB_ROOT}/src/sql/parser/ftsparser.y"
    "${SEEKDB_ROOT}/src/sql/parser/sql_parser_mysql_mode.l"
    "${SEEKDB_ROOT}/src/sql/parser/sql_parser_mysql_mode.y"
    "${SEEKDB_ROOT}/src/sql/parser/gen_type_name.sh"
    "${SEEKDB_ROOT}/src/sql/parser/ob_item_type.h"
    "${SEEKDB_ROOT}/src/query/api/query/parser/ob_item_type.h"
  VERBATIM)
add_library(charset STATIC ${CHARSET_SOURCES})
add_library(parser STATIC ${PARSER_SOURCES}
  ${parser_generated_sources} ${parser_generated_headers})
# The large generated SQL state machine must remain compact for browser JITs.
set_source_files_properties(${parser_generated_sources} PROPERTIES COMPILE_OPTIONS "-Oz")
foreach(target charset parser)
  target_compile_features(${target} PRIVATE cxx_std_20)
  # Match the native OBLib allowance for legacy charset register declarations.
  target_compile_options(${target} PRIVATE -pthread -sUSE_ZLIB=1 -Wno-register)
  target_include_directories(${target} PUBLIC
    "${SEEKDB_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${parser_generated_dir}"
    "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/src/oblib"
    "${SEEKDB_ROOT}/src/oblib/easy" "${SEEKDB_ROOT}/src/oblib/easy/include"
    "${SEEKDB_ROOT}/src/query/api" "${SEEKDB_ROOT}/src/data_plane/api"
    "${SEEKDB_ROOT}/src/sql/parser" "${SEEKDB_ROOT}/include")
endforeach()
target_link_libraries(parser PUBLIC
  charset memory runtime_support crypto)
