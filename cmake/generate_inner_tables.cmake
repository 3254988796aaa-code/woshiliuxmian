# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0

function(seekdb_generate_inner_tables repo output)
  set(generator
    "${repo}/src/share/inner_table/generate_inner_table_schema.py")
  set(def_file
    "${repo}/src/share/inner_table/ob_inner_table_schema_def.py")
  set(init_data
    "${repo}/src/share/inner_table/ob_inner_table_init_data.py")
  set(inputs
    "${generator}"
    "${def_file}"
    "${init_data}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${inputs})

  set(input_digest "")
  foreach(input IN LISTS inputs)
    file(SHA256 "${input}" input_sha256)
    string(APPEND input_digest
      "${input}:${input_sha256}\n")
  endforeach()
  string(SHA256 digest "${input_digest}")
  set(stamp "${output}/.inner_table_schema.sha256")
  set(current_digest "")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" current_digest)
    string(STRIP "${current_digest}"
      current_digest)
  endif()
  if(NOT "${current_digest}" STREQUAL "${digest}" OR
     NOT EXISTS "${output}/share/inner_table/ob_inner_table_schema.h" OR
     NOT EXISTS "${output}/share/inner_table/local_table_schema.h" OR
     NOT EXISTS "${output}/observer/virtual_table/ob_all_virtual_local_tables.cpp")
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${generator}"
        --def-file "${def_file}"
        --share-output-dir "${output}/share/inner_table"
        --observer-output-dir "${output}/observer/virtual_table"
        --quiet
      RESULT_VARIABLE generator_result)
    if(NOT generator_result EQUAL 0)
      message(FATAL_ERROR "Failed to generate inner-table schema sources")
    endif()
    file(WRITE "${stamp}" "${digest}\n")
  endif()
endfunction()
