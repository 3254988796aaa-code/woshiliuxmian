# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Warnings that clang builds of seekdb turn off. src/oblib/CMakeLists.txt adds
# them for native builds on top of -Wall -Wextra -Werror, and the WebAssembly
# project (src/wasm/CMakeLists.txt) adds them as well.
set(SEEKDB_CLANG_DISABLED_WARNINGS
  -Wno-deprecated -Wno-address-of-packed-member -Wno-sign-compare -Wno-tautological-compare
  -Wno-psabi -Wno-c99-designator -Wno-int-in-bool-context -Wno-sizeof-array-div
  -Wno-implicit-const-int-float-conversion -Wno-fortify-source -Wno-non-c-typedef-for-linkage
  -Wno-uninitialized-const-reference -Wno-unused-function -Wno-atomic-alignment
  -Wno-tautological-constant-out-of-range-compare -Wno-unused-parameter
  -Wno-string-plus-int -Wno-misleading-indentation -Wno-unused-private-field
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual -Wno-reserved-user-defined-literal
  -Wno-self-assign -Wno-inconsistent-missing-override -Wno-mismatched-tags -Wno-unused-variable
  -Wno-invalid-offsetof -Wno-unevaluated-expression -Wno-constant-logical-operand>
  $<$<COMPILE_LANGUAGE:C>:-Wno-unused-parameter>
  -Wno-unused-but-set-variable -Wno-null-pointer-subtraction -Wno-compound-token-split-by-macro
  -Wno-packed-non-pod -Wno-format -Wno-shorten-64-to-32 -Wno-unused-but-set-parameter -Wno-single-bit-bitfield-constant-conversion
  -Wno-covered-switch-default
  -Wno-bitwise-instead-of-logical -Wno-suggest-override -Wno-non-virtual-dtor -Wno-cast-qual
  -Wno-unused-value -Wno-macro-redefined -Wno-reinterpret-base-class -Wno-register
  -Wno-implicit-fallthrough -Wno-deprecated-non-prototype
  -Wno-ambiguous-reversed-operator -Wno-invalid-partial-specialization
  -Wno-string-concatenation
  -Wno-vla)
