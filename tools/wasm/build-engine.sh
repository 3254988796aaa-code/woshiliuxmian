#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${repo_root}/build_wasm_engine}"
sdk_version="$(cat "${repo_root}/tools/wasm/emscripten-version")"
if ! emcc --version | head -n 1 | grep -Eq " ${sdk_version//./\\.}([ -]|$)"; then
  echo "Activate Emscripten ${sdk_version} before building seekdb." >&2
  exit 1
fi
emcmake cmake -S "${repo_root}/src/wasm" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release "${@:2}"
cmake --build "${build_dir}" --parallel 4
