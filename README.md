# Pecia — 极简文本编辑器

基于 C++17 + FLTK 的 Windows 轻量级文本编辑器。多标签、大文件秒开、多编码支持、Lua 脚本扩展。

## 快速开始

### 环境要求(基础环境,除源码和第三方库之外)

- Windows 10/11 x64(1803+ 以上,内置 curl/tar 下载工具)
- **Visual Studio Build Tools 2022**(含 C++ 桌面工作负载,提供 vcvars64.bat;
  VS2022/2019 全系列亦可,脚本自动探测,也可用环境变量 `PECIA_VCVARS` 指定)
- **CMake ≥ 3.16 + Ninja**(需在 PATH 中)
- **Rust 工具链**(≥ 1.85,支持 edition 2024;用于编译 mmdr 与 RaTeX 的 FFI 渲染库,
  通过 https://rustup.rs 安装,首次构建需联网从 crates.io 拉取依赖)
- **网络连接**(仅首次构建需要:setup.bat 下载依赖 + cargo 拉取 crate)

### 构建(两步)

```bat
REM 第一步(仅首次/依赖变更时):下载第三方库到 .thirdparty/、应用补丁、
REM 编译 FLTK 与 mmdr 库。约 10 分钟,已就绪的部分自动跳过,可重复运行。
build\setup.bat

REM 第二步(日常开发):CMake 全量构建 Pecia + 单测。
REM (Pecia 目标启用 LTO:/GL+/LTCG + Rust thin LTO,链接阶段多花约 1 分钟)
build\build.bat
```

> 第三方库(FLTK/Lua/PCRE2/md4c/litehtml/stb)由 setup.bat
> 按 `build/deps.txt` 中的地址自动下载到项目根 `.thirdparty/`;
> 地址可手动编辑(deps.txt)。Rust 库(mmdr/RaTeX/resvg 等)由 cargo
> 从 crates.io 自动拉取(版本锁定于 FFI crate 的 Cargo.lock)。
> 我们写的 FFI 壳等修改归档在 `main/patches/`,随源码上传。

### 运行测试

构建脚本自动运行单元测试(ctest, 含文档一致性检查 test_docs)。单独运行:

```bat
build\ctest --test-dir build_cmake
```

### 打包发布

```bat
build\pack.bat
```

---

## 目录结构

```
main/
├── CMakeLists.txt      构建规则(CMake 入口)
├── main.cpp            程序入口
├── README.md           本文档
├── 目录结构说明.md      目录与模块规范(接手必读)
├── core/               核心纯逻辑(无 UI)
├── editor/             编辑器控件
├── ui/                 窗口/对话框/工具栏
├── LuaTool/            PeciaLua 独立工具(窗口/入口/管道服务端)
├── AIChat/             PeciaAIChat 独立工具(窗口/入口)
├── script/             Lua 脚本引擎
├── plugin/             独立 DLL 封装(pecia_regex 正则引擎, 按需加载)
├── DOCS_MANIFEST.md    文档清单(改代码后逐项核对, test_docs 门禁; 纯开发用, 不随 exe 发布)
├── image/              图片资源(icon/ 应用图标)
├── test/               单元测试源码
├── lang/               语言文件(en/zh-CN)
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
| md4c | 2026.08 master | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | master | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (公式渲染,内置) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| 构建 | CMake + Ninja + MSVC | — |

> 提示:可对第三方库源码做本地修改;修改后的完整文件必须归档到
> `main/patches/<库名>/`(构建时由 setup.bat 自动覆盖),仅记录文字不完整。
> 升级时从上述仓库拉取新版本后重放补丁(见 build/FLTK_PATCHES.md 的示例)。

## License

待定(见 LICENSE 占位)。
