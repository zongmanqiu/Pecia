# Pecia — 极简文本编辑器

> **🌐 语言 / Languages**: [中文](README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

---

**[⬇️ 下载最新版 (1.0.0)](https://gitee.com/qiuzongman/curseen/releases/download/1.0.0/Pecia_x64_1.0.0.zip)**

基于 C++17 + FLTK 的 Windows 轻量级文本编辑器。多标签、大文件秒开、多编码支持、Lua 脚本扩展。

> **平台定位**:仅支持 **Windows x64(64 位)**。不提供 32 位版本,也不再维护跨平台
> (Linux/mac/32 位)。构建脚本已内置 64 位校验(见 CMakeLists 顶部),误配 32 位会直接报错。

## 快速开始

### 环境要求(基础环境,除源码和第三方库之外)

- Windows 10/11 x64(1803+ 以上,内置 curl/tar 下载工具)
- **Visual Studio Build Tools 2022 / VS2022**(含 C++ 桌面工作负载)
  - 脚本自带 `build/msvc_env.bat` **自动探测** MSVC 与 Windows SDK(不依赖
    vcvars/vswhere/开发者命令提示符,普通 cmd 双击即可),也可用环境变量
    `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` 指定自定义安装位置
- **CMake ≥ 3.16**(需在 PATH 中;生成器用 **NMake Makefiles**,无需 Ninja)
- **Rust 工具链**(≥ 1.85,支持 edition 2024;用于编译 mmdr 与 RaTeX 的 FFI 渲染库,
  通过 https://rustup.rs 安装,首次构建需联网从 crates.io 拉取依赖)
- **网络连接**(仅首次构建需要:2_download.bat 下载依赖 + 4_build_rust.bat 的
  `cargo fetch` 拉取 crate)
- **git 不需要**(依赖用 curl 下载 + tar 解压)

### 构建(推荐:双击一步)

```bat
REM 下载源码后,只需双击这一个脚本。
REM 它按顺序跑完:1 体检工具链 → 2 下载 6 个第三方库 → 3 应用补丁 →
REM 4 cargo fetch + 编两个 Rust FFI → 5 编 FLTK + CMake 编 Pecia 三件套 →
REM 运行 14 个单测。
REM 首次耗时取决于网络与机器;之后复用缓存,增量编译 + 测试约 1-2 分钟。
build\full.bat
```

### 构建(分步,五个编号脚本)

`main\build\` 下的编号脚本职责单一,可单独重跑任一步:

```bat
REM ① 只读体检:检查 cmake/cargo/cl/nmake/rc/tar/curl,Rust host 须为 msvc
build\1_check_env.bat

REM ② 下载 + 解压第三方库到 .thirdparty/(幂等,已存在则跳过)
build\2_download.bat

REM ③ 把 main/patches/ 单向应用到 .thirdparty/(幂等)
build\3_patch.bat

REM ④ cargo fetch 拉 Rust 依赖 + cargo build --release 编出两个 FFI 静态库
REM   (这一步最容易漏:Rust 也是要编译的,不是拿来就用)
build\4_build_rust.bat

REM ⑤ 编译 FLTK + Pecia 三件套,并跑 ctest
build\5_build_pecia.bat

REM ★ 总入口 = ②→③→④→⑤(等同于双击 full.bat)
build\full.bat
```

> 只改了代码要重编时,直接双击 `5_build_pecia.bat` 即可——它会自己先同步补丁,
> 并按需决定要不要调 `4_build_rust.bat`。

> 第三方库(FLTK/Lua/PCRE2/md4c/litehtml/stb)由 `2_download.bat`
> 按 `build/deps.txt` 中的地址自动下载到项目根 `.thirdparty/`;
> 地址可手动编辑(deps.txt)。Rust 库(mmdr/RaTeX/resvg 等)由 cargo
> 从 crates.io 自动拉取(版本锁定于 FFI crate 的 Cargo.lock)。
> 我们写的 FFI 壳等修改归档在 `main/patches/`,随源码上传。

### 运行测试

构建脚本自动运行单元测试(ctest, 含文档一致性检查 test_docs)。单独运行:

```bat
REM 构建目录在项目根 temp\cmake_build\,可执行文件在项目根 build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> 也可直接跑 `build\5_build_pecia.bat`,它在编译完成后会自动执行 ctest。
> **测试是构建门禁**:任一测试失败 → 脚本 `exit /b 1`,`full.bat` 随即中止,
> 不会报"全部完成"。只想快速编译不看测试时设 `PECIA_SKIP_TESTS=1`。
> (脚本为 Windows 批处理,必须以 **CRLF** 行尾保存,否则 cmd 会解析错乱。)

### 打包发布

```bat
build\pack.bat
```

输出到项目根 `release\`（`release\<版本>\` 目录与 `Pecia-<版本>.zip`，
随附 LICENSE 与 THIRD-PARTY-NOTICES.md），`main/` 内不存放任何发布产物。

---

## 目录结构

```
main/
├── CMakeLists.txt      构建规则(CMake 入口)
├── main.cpp            程序入口
├── README.md           本文档(含架构总览/技术栈/构建说明)
├── 目录结构说明.md      目录与模块规范(接手必读)
├── core/               核心纯逻辑(无 UI)
├── editor/             编辑器控件
├── ui/                 窗口/对话框/工具栏
├── LuaTool/            PeciaLua 独立工具(窗口/入口/管道服务端)
├── AIChat/             PeciaAIChat 独立工具(窗口/入口)
├── script/             Lua 脚本引擎
├── DOCS_MANIFEST.md    文档清单(改代码后逐项核对, test_docs 门禁; 纯开发用, 不随 exe 发布)
├── image/              图片资源(icon/ 应用图标)
├── test/               单元测试源码
├── lang/               语言文件(en.ini/zh-CN.ini)
├── theme/              主题颜色文件(light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            文档案例(展示 Markdown 渲染效果:Mermaid 图表、LaTeX 公式、图片等)
├── patches/            对 .thirdparty 库源码的修改归档(构建时自动覆盖应用)
├── mdview/             Markdown 预览(预览面板/HTML 渲染,含 mmdr FFI 头)
└── build/              构建脚本与构建辅助源码
```

## 技术栈

| 组件 | 版本 | 仓库 |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (公式渲染,内置) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| 构建 | CMake + NMake Makefiles + MSVC | — |

> 提示:可对第三方库源码做本地修改;修改后的完整文件必须归档到
> `main/patches/<库名>/`(构建时由 3_patch.bat 自动覆盖),仅记录文字不完整。
> 升级时从上述仓库拉取新版本后重放补丁(见 build/FLTK_PATCHES.md 的示例)。

---

## 架构总览

> 本文描述**当前源码**（C++17 + FLTK 1.4.5）的真实架构与数据流。
> 目录与模块规范以《目录结构说明.md》为准。

### 一、可执行程序与进程模型

本产品发布为 **1 个主程序 + 2 个独立工具**，三者共享大量 core/ui 源码：

| 可执行程序 | 入口 | 职责 | 单实例 |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | 主文本编辑器 | 可多开（`PECIA_POS` 级联） |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | 独立 Lua 控制台，对主文档跑脚本 | 是（`MUTEX_LUA_TOOL`） |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | 独立 AI 聊天窗口，读主文档选区 | 是（`MUTEX_AI_CHAT`） |

三者通过 **命名管道** 通信（帧格式见 `core/PipeProtocol.h`：`4字节tag + u32小端长度 + 负载`）。主程序启动 `LuaPipeServer`，其 `listenLoop()` 在后台线程监听 per-pid 管道 `\\.\pipe\pecia-lua-<pid>`；收到请求用 `Fl::awake` 派发到 UI 线程执行，再回写 OK/ERR 帧。`PeciaLua` / `PeciaAIChat` 由主程序 `launchTool()` 拉起，命令行传入管道名与语言。

### 二、分层与依赖方向

强制依赖方向（`目录结构说明.md` 亦规定）：

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  可依赖任何层，但只测纯逻辑（不依赖 GUI）
```

- **`core/`（纯逻辑，禁 FLTK/UI）**：Config、CrashReport、I18n、Theme、SearchCore、EncodingCore、FileManager、ShortcutCore、OpLog、AiApiClient（HTTP 后台线程）、PipeClient、UiBridge（依赖倒置接口）。
- **`editor/`**：Document（模型，Fl_Text_Buffer 封装 + 文件 IO + 编码）、Editor（Fl_Text_Editor 扩展：行高亮/URL/空白符/自动缩进）。
- **`ui/`**（唯一可 new Fl_Window / 弹对话框的层）：MainWindow 及按功能拆分的 `MainWindow_*.cpp`、各对话框、菜单表、快捷键、预览面板接线。
- **`script/`**：LuaEngine（并非沙箱——标准库全量开放，`lua_api.txt` 明确声明此信任模型；带 scratch buffer + rex/win API）、ScriptManager（扫描/元信息/folder.ini）、LuaParamParser（--!param 声明）。
- **`mdview/`**：Markdown 预览（md4c→HTML → litehtml 渲染；mermaid/LaTeX 经 Rust FFI 进程内渲染）；`mmdr_ffi` / `ratex_ffi` 静态链接构建产物。

**`UiBridge`：core 与 UI 的唯一连接缝**——`core/UiBridge.h` 定义纯虚 `message()/confirm()`；`ui/UiBridge.cpp` 提供具体实现并经 `setUiBridge()` 注入。这样 `Document`/`FileManager`（core）提示用户时只调用 `uiBridge()->confirm(...)`，不依赖任何 FLTK 对话框类，核心可测。

### 三、配置 / 国际化 / 崩溃处理

- **配置**：`settings.ini`（exe 同级），`core/Config.cpp` 读写；写入白名单机制用于 AI 配置类敏感键。`recent file.ini` 记录最近文件。
- **国际化**：`lang/en.ini`、`lang/zh-CN.ini`，`core/I18n.cpp` 加载；两文件键集必须一致（`test_docs` 门禁校验）。
- **崩溃处理**：`core/CrashReport.cpp` 三个 exe 共用；写 exe 同级 `temp/`（dmp + 栈日志 + 最近操作日志尾部），dmp 保留最近 3 个、7 天清理。

### 四、编辑 / 脚本 / 预览子系统要点

- **多标签编辑**：`MainWindow` 管理 `m_tabsList`（Tab=editor+doc 组合），切换/关闭时先 `buffer(nullptr)` 再 delete doc，避免 FLTK 回调访问已析构缓冲。
- **脚本执行**：`LuaEngine` 对**文档快照**运行脚本（非沙箱，标准库全量开放；入口经命名管道可达，管道已限制为当前用户 DACL），结果经 `applyDocumentSnapshot` 写回。脚本菜单来自 `script/scripts/**`（含 `.lua` + `folder.ini` 名字翻译），构建时复制到 `build/script/`。
- **Markdown 预览**：编辑→`ui/MainWindow_preview.cpp`；`mdview/preprocess` 把 md4c 输出转 HTML、抽 ```` ```math ```` 公式交给 RaTeX（SVG）、mermaid 块交给 mmdr（PNG）；`mdview/container` 是 litehtml 的 document_container 适配；`PreviewPanel` 负责渲染链路。预览自动刷新按档位（1s/5s/10s/30s）。

### 五、构建与测试

- **五个编号脚本 + 总入口**（`main/build/`，职责单一，可单独重跑任一步）：
  - `full.bat`：**总入口**（双击即用），按序调用 ②→③→④→⑤。
  - `1_check_env.bat`：只读工具链体检（cmake/cargo/cl/nmake/rc/tar/curl；
    `rustc -vV` 的 host 须为 `pc-windows-msvc`，否则编不出可链接的 `.lib`）。
  - `2_download.bat`：按 `deps.txt` 下载 + 解压第三方库到 `.thirdparty/`（幂等）。
  - `3_patch.bat`：同步补丁（`main/patches/ → .thirdparty/` **单向**强覆盖）。
  - `4_build_rust.bat`：`cargo fetch` 拉取 Rust 依赖 → `cargo build --release`
    编出 `mermaid_ffi.lib` / `ratex_ffi.lib`（**Rust 也是要编译的**，别漏这一步）。
  - `5_build_pecia.bat`：按时间戳判断 FLTK / 两个 Rust FFI 是否需重编 → NMake 配置并
    **增量**编译（复用 `temp/cmake_build` 缓存；仅当缓存由其他生成器产生时才清空重建）
    → 自动运行 ctest。
  - `_common.bat`：各脚本共用的路径变量、`tar`/`curl` 绝对路径解析、MSVC 环境装载，
    以及 `:fetch_rust_deps` 子例程。
- `build/msvc_env.bat`：探测 MSVC 与 Windows SDK 并设置 `PATH`/`INCLUDE`/`LIB`
  （优先用 `vswhere.exe` 定位任意安装路径；各构建脚本共用）。
- `build/pack.bat`：把 `build/` 内容打包到 `release/<版本>/`。
- `build/deps.txt`：第三方库下载清单（`库名=URL|探针文件`，纯 ASCII——中文注释会让
  `for /f` 按 GBK 解析时吞掉后续行）。
- **单元测试**（`test/`，只测核心纯逻辑）：search/encoding/config/document/match_highlight/param/docs/lua_engine（含 rex 正则）/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke 共 **14** 个目标，ctest 全绿 + `test_docs` 文档一致性门禁。

> 第三方库源码的本地修改一律归档于 `main/patches/`，构建时自动覆盖到 `.thirdparty/`；切勿反向覆盖（详见 AGENTS.md）。第三方库编译告警不计入主程序"零警告 /W4"门禁（门禁只约束 `main/` 第一方源码）。

## License

[GNU AGPL-3.0](LICENSE)。

第三方组件（FLTK/Lua/PCRE2/md4c/litehtml/stb 与 Rust crates）的许可证、
修改（patch）说明及分发义务见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
