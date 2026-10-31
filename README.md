# Pecia — 极简文本编辑器

> **🌐**
 [中文](README.md)
 | [English](docs/README/README.en.md)
 | [日本語](docs/README/README.ja.md)
 | [한국어](docs/README/README.ko.md)
 | [Deutsch](docs/README/README.de.md)
 | [Français](docs/README/README.fr.md)
 | [Español](docs/README/README.es.md)
 | [Русский](docs/README/README.ru.md)
 | [Português-BR](docs/README/README.pt-BR.md)
 | [Italiano](docs/README/README.it.md)
 | [繁體中文](docs/README/README.zh-TW.md)
 | [العربية](docs/README/README.ar.md)
 | [हिन्दी](docs/README/README.hi.md)
 | [Indonesia](docs/README/README.id.md)
 | [Türkçe](docs/README/README.tr.md)
 | [Tiếng Việt](docs/README/README.vi.md)

---

**[⬇️ 下载最新版 (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)**
 ｜ **[⬇️ 下载最新版 (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)**
 ｜ [Gitee主仓](https://gitee.com/qiuzongman/pecia)
 ｜ [GitHub镜像](https://github.com/zongmanqiu/Pecia)
 ｜ [B站视频演示](https://www.bilibili.com/video/BV144Yo6oEz9/)

基于 C++17 + FLTK 的 Windows 轻量级文本编辑器。多标签、大文件秒开、多编码支持、Lua 脚本扩展。新增：侧边栏（文件树/大纲）、字体选择对话框、窗口阴影；Markdown 预览对超大文档分块渲染。

> **平台定位**:仅支持 **Windows x64(64 位)**。不提供 32 位版本,也不再维护跨平台
> (Linux/mac/32 位)。构建脚本已内置 64 位校验(见 CMakeLists 顶部),误配 32 位会直接报错。

## 软件界面

- Markdown预览
  - ![0.MD预览](./docs/README/0.Makrodnw预览.png)
- 多标签页
  - ![2.多标签页](./docs/README/1.多标签页.png)
- 极简状态
  - ![2.极简状态](./docs/README/2.极简状态.png)
- 选项设置
  - ![3.选项](./docs/README/3.选项.png)
- 快捷键设置
  - ![4.快捷键](./docs/README/4.快捷键.png)
- AI对话
  - ![5.AI对话](./docs/README/5.AI对话.png)
- Lua控制台
  - ![6.Lua控制台](./docs/README/6.Lua控制台.gif)
- Lua脚本
  - ![7.Lua脚本](./docs/README/7.Lua脚本.gif)

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
REM 运行 26 个单测。
REM 首次耗时取决于网络与机器;之后复用缓存,增量编译 + 测试约 1-2 分钟。
build\full.bat
```

> 分步构建脚本、测试与打包详见 `build/build_readme.md`;发版流程见
> `docs/RELEASE_CHECKLIST.md`。架构设计(进程模型/分层/子系统)见 `开发指南.md`
> 开篇「架构速览」。

---

## 目录结构

```
main/
├── CMakeLists.txt      构建规则(CMake 入口)
├── main.cpp            程序入口
├── README.md           本文档(项目介绍/下载/界面/构建入口)
├── docs/               文档目录(docs/README/ 下其他语言 README + 界面截图;intro.pptx)
├── 目录结构说明.md      目录与模块规范(接手必读)
├── 开发指南.md         开发/发版流程/架构速览与提交前检查清单
├── AGENTS.md           工程约定(协作者/AI 规则)
├── HISTORY.md          版本更新历史(发版时在顶部追加新版本小节,旧内容不改)
├── LICENSE             AGPL-3.0 官方逐字全文(UTF-8,无 BOM;不得改动条款)
├── THIRD-PARTY-NOTICES.md  第三方库声明(名称/版本/许可证/补丁数量)
├── .gitignore          版本控制忽略规则
├── .gitattributes      行尾符与文本属性规则
├── core/               核心纯逻辑(无 UI)
├── editor/             编辑器控件
├── ui/                 窗口/对话框/工具栏
├── LuaTool/            PeciaLua 独立工具(窗口/入口/管道服务端)
├── AIChat/             PeciaAIChat 独立工具(窗口/入口)
├── script/             Lua 脚本引擎
├── DOCS_MANIFEST.md    文档清单(改代码后逐项核对, test_docs 门禁; 纯开发用, 不随 exe 发布)
├── image/              图片资源(pecia.ico / ai_bot.ico / lua.ico …)
├── test/               单元测试源码
├── lang/               语言文件(16 × .ini)
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
> mmdr 内含 resvg 0.47(全项目唯一 SVG 光栅化引擎:mermaid 图与 LaTeX 公式的 SVG
> 均由它光栅化为 PNG);RaTeX 只生成 SVG 字符串,不直接依赖 resvg。

---

## License

[GNU AGPL-3.0](LICENSE)。

第三方组件（FLTK/Lua/PCRE2/md4c/litehtml/stb 与 Rust crates）的许可证、
修改（patch）说明及分发义务见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
