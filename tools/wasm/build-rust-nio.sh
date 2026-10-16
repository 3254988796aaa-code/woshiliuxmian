#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${repo_root}/build_wasm_rust_runtime}"
source "${repo_root}/tools/wasm/rust-build-env.sh"
# rust/build.py builds the workspace and exports the static libraries and the
# generated C++ headers listed in rust/exports.json. The config and local cxx
# bridges compile with em++ and must share the engine's thread and exception model.
CXXFLAGS_wasm32_unknown_emscripten="-pthread -fexceptions" \
python3 "${repo_root}/rust/build.py" --target wasm32-unknown-emscripten \
  --target-dir "${CARGO_TARGET_DIR}" --output "${build_dir}/exports" \
  -- --no-default-features --features sql-nio/memory-transport \
  -Zbuild-std=std,panic_abort
python3 "${repo_root}/tools/wasm/rust-nio-manifest.py" "${build_dir}" --record
