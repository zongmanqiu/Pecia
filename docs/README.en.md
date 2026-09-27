> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

---

# Pecia — Minimalist Text Editor

**[⬇️ Download Latest (1.0.3)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.3/Pecia_x64_1.0.3.zip)**

A lightweight text editor for Windows built with C++17 + FLTK. Multi-tab, instant opening of large files, multiple encoding support, Lua scripting extensions.

> **Platform scope**: Supports **Windows x64 (64-bit) only**. No 32-bit version is provided, and cross-platform support (Linux/macOS/32-bit) is no longer maintained. The build scripts include a built-in 64-bit check (see the top of CMakeLists); attempting to build for 32-bit will produce an error.

## Quick Start

### Prerequisites (basic environment, excluding source code and third-party libraries)

- Windows 10/11 x64 (1803+ or later, with built-in curl/tar download tools)
- **Visual Studio Build Tools 2022 / VS2022** (with C++ Desktop workload)
  - The script includes `build/msvc_env.bat` which **auto-detects** MSVC and Windows SDK (does not depend on vcvars/vswhere/Developer Command Prompt — a regular cmd double-click works). You can also use the environment variables `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` to specify custom install locations
- **CMake ≥ 3.16** (must be in PATH; generator uses **NMake Makefiles**, no Ninja needed)
- **Rust toolchain** (≥ 1.85, supporting edition 2024; used to compile the mmdr and RaTeX FFI rendering libraries, installed via https://rustup.rs; first build requires an internet connection to fetch dependencies from crates.io)
- **Internet connection** (only needed for first build: 2_download.bat downloads dependencies + `cargo fetch` in 4_build_rust.bat pulls crates)
- **git is not required** (dependencies are downloaded with curl + extracted with tar)

### Build (recommended: one-click)

```bat
REM After downloading the source, just double-click this single script.
REM It runs everything in sequence: 1 check toolchain → 2 download 6 third-party libraries → 3 apply patches →
REM 4 cargo fetch + compile two Rust FFI libraries → 5 compile FLTK + CMake builds Pecia trio →
REM run 14 unit tests.
REM First build time depends on network and machine; subsequent builds reuse the cache, incremental compile + test takes about 1-2 minutes.
build\full.bat
```

### Build (step-by-step, five numbered scripts)

The numbered scripts under `main/build/` have single responsibilities and can be re-run individually:

```bat
REM ① Read-only toolchain check: cmake/cargo/cl/nmake/rc/tar/curl; Rust host must be msvc
build\1_check_env.bat

REM ② Download + extract third-party libraries to .thirdparty/ (idempotent, skips if already present)
build\2_download.bat

REM ③ Apply patches from main/patches/ to .thirdparty/ (one-way, idempotent)
build\3_patch.bat

REM ④ cargo fetch pulls Rust dependencies + cargo build --release compiles two FFI static libraries
REM   (this step is most commonly forgotten: Rust also needs to be compiled, it's not ready-to-use)
build\4_build_rust.bat

REM ⑤ Compile FLTK + Pecia trio, and run ctest
build\5_build_pecia.bat

REM ★ Full entry = ①→②→③→④→⑤ (equivalent to double-clicking full.bat)
build\full.bat
```

> When you've only changed code and need to rebuild, just double-click `5_build_pecia.bat` — it will automatically sync patches first and decide whether to invoke `4_build_rust.bat` as needed.

> Third-party libraries (FLTK/Lua/PCRE2/md4c/litehtml/stb) are automatically downloaded by `2_download.bat` from URLs listed in `build/deps.txt` to the project root `.thirdparty/` directory;
> URLs can be manually edited (deps.txt). Rust libraries (mmdr/RaTeX/resvg, etc.) are automatically fetched by cargo
> from crates.io (versions locked by the FFI crate's Cargo.lock).
> Our FFI wrapper modifications and other changes are archived in `main/patches/`, included with the source code.

### Running Tests

Build scripts automatically run unit tests (ctest, including documentation consistency checks via test_docs). To run separately:

```bat
REM Build directory is at project root temp\cmake_build\, executables at project root build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> You can also directly run `build\5_build_pecia.bat`, which automatically executes ctest after compilation.
> **Tests are build gates**: any test failure → script `exit /b 1`, `full.bat` aborts immediately,
> and will not report "all complete". To skip tests for a quick build, set `PECIA_SKIP_TESTS=1`.
> (Scripts are Windows batch files and must be saved with **CRLF** line endings, otherwise cmd will parse them incorrectly.)

### Packaging Release

```bat
build\pack.bat
```

Outputs to project root `release\` (`release\<version>\` directory and `Pecia-<version>.zip`,
along with LICENSE and THIRD-PARTY-NOTICES.md). No release artifacts are stored inside `main/`.

---

## Directory Structure

```
main/
├── CMakeLists.txt      Build rules (CMake entry point)
├── main.cpp            Program entry point
├── README.md           This document (including architecture overview/tech stack/build instructions)
├── docs/               README in other languages (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Directory and module conventions (essential reading for onboarding)
├── 开发指南.md         Development/release workflow and pre-commit checklist
├── AGENTS.md           Engineering conventions (contributors/AI rules)
├── HISTORY.md          Version history (append a new version section at the top on release; keep old content unchanged)
├── LICENSE             Verbatim official AGPL-3.0 text (UTF-8, no BOM; terms must not be modified)
├── THIRD-PARTY-NOTICES.md  Third-party library notices (name/version/license/patch count)
├── .gitignore          Version-control ignore rules
├── .gitattributes      Line-ending and text attribute rules
├── core/               Core pure logic (no UI)
├── editor/             Editor widget
├── ui/                 Windows/dialogs/toolbars
├── LuaTool/            PeciaLua standalone tool (window/entry/pipeline server)
├── AIChat/             PeciaAIChat standalone tool (window/entry)
├── script/             Lua script engine
├── DOCS_MANIFEST.md    Documentation manifest (check each item after code changes, test_docs gate; dev-only, not shipped with exe)
├── image/              Image resources (icon/ application icon)
├── test/               Unit test source code
├── lang/               Language files (en.ini/zh-CN.ini)
├── theme/              Theme color files (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Documentation examples (showcasing Markdown rendering: Mermaid diagrams, LaTeX formulas, images, etc.)
├── patches/            Archive of modifications to .thirdparty library source code (automatically applied during build)
├── mdview/             Markdown preview (preview panel/HTML rendering, including mmdr FFI headers)
└── build/              Build scripts and build helper source code
```

## Tech Stack

| Component | Version | Repository |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (formula rendering, built-in) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Tip: You can make local modifications to third-party library source code; the complete modified files must be archived in
> `main/patches/<library_name>/` (automatically overwritten during build by 3_patch.bat), not just textual diffs.
> When upgrading, pull the new version from the above repositories and replay the patches (see build/FLTK_PATCHES.md for an example).

---

## Architecture Overview

> This section describes the actual architecture and data flow of the **current source code** (C++17 + FLTK 1.4.5).
> For directory and module conventions, refer to "目录结构说明.md" (Directory Structure Guide).

### 1. Executables and Process Model

This product is released as **1 main program + 2 standalone tools**, all sharing a large amount of core/ui source code:

| Executable | Entry Point | Responsibility | Single Instance |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Main text editor | Can run multiple instances (`PECIA_POS` cascade) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Standalone Lua console, runs scripts on main document | Yes (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Standalone AI chat window, reads main document selection | Yes (`MUTEX_AI_CHAT`) |

The three communicate via **named pipes** (frame format see `core/PipeProtocol.h`: `4-byte tag + u32 little-endian length + payload`). The main program launches `LuaPipeServer`, whose `listenLoop()` listens in a background thread on a per-pid pipe `\\.\pipe\pecia-lua-<pid>`; incoming requests are dispatched to the UI thread for execution via `Fl::awake`, then OK/ERR frames are written back. `PeciaLua` / `PeciaAIChat` are launched by the main program's `launchTool()`, which passes the pipe name and language via command line.

### 2. Layering and Dependency Direction

Enforced dependency direction (also specified in "目录结构说明.md"):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  can depend on any layer, but only tests pure logic (no GUI dependency)
```

- **`core/` (pure logic, FLTK/UI forbidden)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (HTTP background thread), PipeClient, UiBridge (dependency inversion interface).
- **`editor/`**: Document (model, Fl_Text_Buffer wrapper + file IO + encoding), Editor (Fl_Text_Editor extension: line highlighting/URL/whitespace/auto-indent).
- **`ui/`** (the only layer that may create Fl_Window / pop up dialogs): MainWindow and its functionally split `MainWindow_*.cpp` files, various dialogs, menu tables, shortcuts, preview panel wiring.
- **`script/`**: LuaEngine (not sandboxed — standard library fully open, `lua_api.txt` explicitly documents this trust model; includes scratch buffer + rex/win API), ScriptManager (scanning/metadata/folder.ini), LuaParamParser (--!param declarations).
- **`mdview/`**: Markdown preview (md4c→HTML → litehtml rendering; mermaid/LaTeX rendered in-process via Rust FFI); `mmdr_ffi` / `ratex_ffi` static link build artifacts.

**`UiBridge`: the sole connection seam between core and UI** — `core/UiBridge.h` defines pure virtual `message()/confirm()`; `ui/UiBridge.cpp` provides the concrete implementation, injected via `setUiBridge()`. This way, `Document`/`FileManager` (core) prompts the user only by calling `uiBridge()->confirm(...)`, without depending on any FLTK dialog class, making the core testable.

### 3. Configuration / Internationalization / Crash Handling

- **Configuration**: `settings.ini` (same directory as exe), read/written by `core/Config.cpp`; write whitelist mechanism used for sensitive AI config keys. `recent file.ini` records recent files.
- **Internationalization**: `lang/en.ini`, `lang/zh-CN.ini`, loaded by `core/I18n.cpp`; both files must have identical key sets (`test_docs` gate enforces this).
- **Crash handling**: `core/CrashReport.cpp` shared by all three exes; writes to `temp/` next to the exe (dmp + stack trace + tail of recent operation log); keeps the latest 3 dmp files, cleaned up after 7 days.

### 4. Editing / Scripting / Preview Subsystem Highlights

- **Multi-tab editing**: `MainWindow` manages `m_tabsList` (Tab = editor+doc combination); on switch/close, `buffer(nullptr)` is called before deleting doc to avoid FLTK callback access to a destroyed buffer.
- **Script execution**: `LuaEngine` runs scripts on **document snapshots** (not sandboxed, standard library fully open; entry point accessible via named pipe, which is restricted to the current user's DACL); results are written back via `applyDocumentSnapshot`. Script menus come from `script/scripts/**` (including `.lua` + `folder.ini` name translations), copied to `build/script/` during build.
- **Markdown preview**: Editing → `ui/MainWindow_preview.cpp`; `mdview/preprocess` converts md4c output to HTML, extracts ```` ```math ```` formulas for RaTeX (SVG), and mermaid blocks for mmdr (PNG); `mdview/container` is the litehtml document_container adapter; `PreviewPanel` handles the rendering pipeline. Preview auto-refresh by tier (1s/5s/10s/30s).

### 5. Build and Test

- **Five numbered scripts + master entry** (`main/build/`, each with a single responsibility, individually re-runnable):
  - `full.bat`: **Master entry** (double-click to use), sequentially calls ①→②→③→④→⑤.
  - `1_check_env.bat`: Read-only toolchain check (cmake/cargo/cl/nmake/rc/tar/curl; `rustc -vV` host must be `pc-windows-msvc`, otherwise linkable `.lib` files cannot be produced).
  - `2_download.bat`: Downloads + extracts third-party libraries to `.thirdparty/` per `deps.txt` (idempotent).
  - `3_patch.bat`: Syncs patches (`main/patches/ → .thirdparty/`, **one-way** force overwrite).
  - `4_build_rust.bat`: `cargo fetch` pulls Rust dependencies → `cargo build --release` compiles `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust also needs to be compiled**, don't skip this step).
  - `5_build_pecia.bat`: Checks timestamps to determine whether FLTK / the two Rust FFIs need recompilation → NMake configures and **incrementally** compiles (reuses `temp/cmake_build` cache; only clears and rebuilds if the cache was produced by a different generator) → automatically runs ctest.
  - `_common.bat`: Shared path variables, `tar`/`curl` absolute path resolution, MSVC environment setup, and the `:fetch_rust_deps` subroutine.
- `build/msvc_env.bat`: Detects MSVC and Windows SDK and sets `PATH`/`INCLUDE`/`LIB` (uses `vswhere.exe` to locate any install path; shared by all build scripts).
- `build/pack.bat`: Packages `build/` contents into `release/<version>/`.
- `build/deps.txt`: Third-party library download manifest (`library_name=URL|probe file`, pure ASCII — Chinese comments cause `for /f` to parse as GBK and skip subsequent lines).
- **Unit tests** (`test/`, testing only core pure logic): search/encoding/config/document/match_highlight/param/docs/lua_engine (including rex regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke — total of **14** targets, all ctest green + `test_docs` documentation consistency gate.

> All local modifications to third-party library source code are archived in `main/patches/`, automatically applied to `.thirdparty/` during build; never overwrite in the reverse direction (see AGENTS.md for details). Third-party library compilation warnings do not count toward the main program's "zero warnings /W4" gate (the gate only applies to first-party source code in `main/`).

## License

[GNU AGPL-3.0](../LICENSE).

Licenses, modification (patch) notes, and distribution obligations for third-party components (FLTK/Lua/PCRE2/md4c/litehtml/stb and Rust crates) are listed in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
