#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Sourced by Rust builders after setting repo_root and build_dir.
mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"
sdk_version="$(cat "${repo_root}/tools/wasm/emscripten-version")"
rust_version="$(cat "${repo_root}/tools/wasm/rust-toolchain-version")"
if ! emcc --version | head -n 1 | grep -Eq " ${sdk_version//./\\.}([ -]|$)"; then
  echo "Activate Emscripten ${sdk_version} before building Rust." >&2
  exit 1
fi
export RUSTUP_TOOLCHAIN="${rust_version}"
export CARGO_TARGET_DIR="${build_dir}/target"
export CARGO_HOME="${build_dir}/cargo"
SEEKDB_RUSTC="$(rustup which rustc --toolchain "${rust_version}")"
export SEEKDB_RUSTC
export SEEKDB_RUST_SYSROOT="${build_dir}/sysroot"
python3 "${repo_root}/tools/wasm/prepare-rust-sysroot.py" \
  "$("${SEEKDB_RUSTC}" --print sysroot)" "${SEEKDB_RUST_SYSROOT}"
cat > "${build_dir}/rustc-wrapper" <<'WRAPPER'
#!/usr/bin/env bash
exec "${SEEKDB_RUSTC}" --sysroot "${SEEKDB_RUST_SYSROOT}" "$@"
WRAPPER
chmod +x "${build_dir}/rustc-wrapper"
export RUSTC="${build_dir}/rustc-wrapper"
# The prebuilt standard library does not establish the shared-memory ABI.
# Rebuild it with the same features as this crate. Rust never unwinds across
# the C boundary; seekdb's C++ code retains its separate JS exception model.
export RUSTFLAGS="-Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort"
