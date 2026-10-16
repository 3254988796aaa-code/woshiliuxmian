# Build seekdb for WebAssembly

Compile seekdb with Emscripten into a WebAssembly engine that runs inside a browser page, together with its JavaScript API and a browser SQL shell.

## Prerequisites

- macOS or Linux host
- Emscripten 6.0.12, the version in `tools/wasm/emscripten-version`, with its `emcc` on `PATH`, for example from `brew install emscripten`
- `rustup`, CMake 3.20 or later, Python 3.12 or later and `curl` on `PATH`
- Node.js 24 or later to run the tests
- About 5 GB of free disk space for the dependencies and the build directories

## Build

Use the WebAssembly entry point. It initializes the dependencies, configures the build, and builds the engine:

```bash
./build.sh release --wasm --init --make
```

`--init` downloads only bison, flex and the source headers the WebAssembly build needs from the native dependency list, unless `deps/3rd` already has them; installs the pinned Rust nightly from `tools/wasm/rust-toolchain-version` with the `wasm32-unknown-emscripten` target; and builds the third-party libraries from the pinned sources in `tools/wasm/dependencies.json` into `build_wasm_deps`. Every run also builds the Rust crates (the memory NIO, `config` and `local`) with `rust/build.py` into `build_wasm_rust_runtime`. Leave out `--init` once the dependencies exist. Without `--make`, `build.sh` only configures the build.

The engine is generated at:

```text
build_wasm_engine/seekdb_wasm_database.mjs    # Emscripten module
build_wasm_engine/seekdb_wasm_database.wasm   # engine
```

Pass CMake options before `--make`, and make options after it, such as `--make -j8`. `./build.sh clean` removes `build_wasm_engine` and `build_wasm_rust_runtime` and keeps `build_wasm_deps`.

## Build directory contents

- `seekdb_wasm_database.mjs` and `seekdb_wasm_database.wasm`: the engine, its Rust libraries and third-party libraries linked into one module, with Emscripten's JavaScript runtime
- `database.mjs`: the JavaScript API, with the `database-worker.mjs`, `worker-server.mjs`, `runtime-host.mjs`, `storage-cleanup-worker.mjs` and `mysql-*.mjs` modules it loads
- `shell.html`, `shell.css`, `shell.mjs` and `shell-*.mjs`: the browser SQL shell
- `engine-version.mjs`: the engine version shown by the shell

Serve these files from `localhost` or over HTTPS with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`, and `.wasm` as `application/wasm`: the engine's threads need `SharedArrayBuffer`. Opened as `file://` URLs, they do not work.

## Use it in a page

Serve the shell from the build directory and open the URL it prints:

```bash
python3 tools/wasm/serve-shell.py --build-dir build_wasm_engine
```

The shell's commands and storage modes are described in `docs/webassembly-shell.md`.

A page uses the engine through `Database`, which runs it in a Worker and speaks the MySQL protocol to it in memory:

```js
import {Database} from './database.mjs';

const db = await Database.open({
  moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url),
  wasmURL: new URL('./seekdb_wasm_database.wasm', import.meta.url),
  storage: 'opfs',  // or 'memory', the default
});
const session = await db.connect();
const text = new TextDecoder();
for await (const event of session.query('SELECT VERSION()')) {
  if (event.kind === 'row') console.log(event.values.map(v => v === null ? null : text.decode(v)));
}
await session.close();
await db.close();
```

- `storage: 'memory'` keeps the data directory in Wasm memory and starts empty on every open. `storage: 'opfs'` keeps it in the `seekdb` directory of the origin private file system, needs OPFS and Web Locks, and can be open in only one page of an origin at a time.
- `query` yields `columns`, `row` and `complete` events. Row values are each column's text bytes as `Uint8Array`, or `null` for SQL NULL.
- Each `Database.open` starts a new Worker and engine. `close()` waits for the engine to shut down and keeps OPFS data; `discard()` terminates the Worker at once and, for OPFS, removes the stored files; `Database.clearPersistentStorage()` removes the stored OPFS files without opening an engine.
- No TCP port is opened: connect through `connect()`.
