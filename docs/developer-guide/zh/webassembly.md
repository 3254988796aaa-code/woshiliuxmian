# 构建 WebAssembly 版 seekdb

本文介绍如何用 Emscripten 将 seekdb 编译为在浏览器页面中运行的 WebAssembly 引擎，同时生成它的 JavaScript API 和浏览器 SQL shell。

## 前置条件

- macOS 或 Linux 主机
- Emscripten 6.0.12（即 `tools/wasm/emscripten-version` 中的版本），且其 `emcc` 在 `PATH` 中，例如通过 `brew install emscripten` 安装
- 已将 `rustup`、CMake 3.20 及以上、Python 3.12 及以上和 `curl` 加入 `PATH`
- 运行测试需要 Node.js 24 及以上
- 约 5 GB 可用磁盘空间，用于依赖和构建目录

## 构建

使用 WebAssembly 构建入口，一次完成依赖初始化、配置和编译：

```bash
./build.sh release --wasm --init --make
```

`--init` 只从原生依赖列表中下载 WebAssembly 构建需要的 bison、flex 和源码头文件，`deps/3rd` 中已经有这些包时跳过；安装 `tools/wasm/rust-toolchain-version` 中固定版本的 Rust nightly 及 `wasm32-unknown-emscripten` target；并用 `tools/wasm/dependencies.json` 中固定版本的源码编译第三方库，安装到 `build_wasm_deps`。每次执行还会用 `rust/build.py` 把 Rust crate（memory NIO、`config` 和 `local`）编译到 `build_wasm_rust_runtime`。依赖准备好后可以去掉 `--init`。不加 `--make` 时，`build.sh` 只生成构建规则。

引擎位于：

```text
build_wasm_engine/seekdb_wasm_database.mjs    # Emscripten 模块
build_wasm_engine/seekdb_wasm_database.wasm   # 引擎
```

CMake 选项写在 `--make` 之前，make 选项写在它之后，例如 `--make -j8`。`./build.sh clean` 会删除 `build_wasm_engine` 和 `build_wasm_rust_runtime`，保留 `build_wasm_deps`。

## 构建目录内容

- `seekdb_wasm_database.mjs` 和 `seekdb_wasm_database.wasm`：引擎、它的 Rust 库和第三方库链接成的一个模块，以及 Emscripten 的 JavaScript 运行时
- `database.mjs`：JavaScript API，以及它加载的 `database-worker.mjs`、`worker-server.mjs`、`runtime-host.mjs`、`storage-cleanup-worker.mjs` 和 `mysql-*.mjs`
- `shell.html`、`shell.css`、`shell.mjs` 和 `shell-*.mjs`：浏览器 SQL shell
- `engine-version.mjs`：shell 显示的引擎版本

这些文件需要通过 `localhost` 或 HTTPS 提供，响应带上 `Cross-Origin-Opener-Policy: same-origin` 和 `Cross-Origin-Embedder-Policy: require-corp`，`.wasm` 的类型为 `application/wasm`：引擎的线程需要 `SharedArrayBuffer`。以 `file://` 方式打开无法运行。

## 在页面中使用

用构建目录启动 shell，打开它输出的 URL：

```bash
python3 tools/wasm/serve-shell.py --build-dir build_wasm_engine
```

shell 的命令和存储模式见 `docs/webassembly-shell.md`。

页面通过 `Database` 使用引擎。它在 Worker 中运行引擎，并在内存中用 MySQL 协议与引擎通信：

```js
import {Database} from './database.mjs';

const db = await Database.open({
  moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url),
  wasmURL: new URL('./seekdb_wasm_database.wasm', import.meta.url),
  storage: 'opfs',  // 或默认的 'memory'
});
const session = await db.connect();
const text = new TextDecoder();
for await (const event of session.query('SELECT VERSION()')) {
  if (event.kind === 'row') console.log(event.values.map(v => v === null ? null : text.decode(v)));
}
await session.close();
await db.close();
```

- `storage: 'memory'` 把数据目录放在 Wasm 内存中，每次打开都是空的。`storage: 'opfs'` 把它放在 origin private file system 的 `seekdb` 目录中，需要浏览器支持 OPFS 和 Web Locks，同一个 origin 同时只能有一个页面打开。
- `query` 依次产生 `columns`、`row` 和 `complete` 事件。行中每一列的值是该列文本的 `Uint8Array` 字节，SQL NULL 为 `null`。
- 每次 `Database.open` 都会启动新的 Worker 和引擎。`close()` 等待引擎关闭并保留 OPFS 数据；`discard()` 立即终止 Worker，OPFS 模式下还会删除存储的文件；`Database.clearPersistentStorage()` 不打开引擎，直接删除存储的 OPFS 文件。
- 不会打开 TCP 端口，只能通过 `connect()` 连接。
