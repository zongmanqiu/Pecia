# 文档清单（改代码时逐项核对）

每次对源码/API/目录结构/配置做出改动后，**按下表逐项检查**对应的
文档文件是否需要同步修改。改完后运行 `build/5_build_pecia.bat`（或 `build/full.bat`）——
`test_docs` 单测会自动验证文档与实际的"事实一致性"（API 注册、
测试清单、语言键对齐、第三方库目录），任何一处漏改都会变红。

| 文件 | 覆盖内容 | 需要更新的时候 |
|------|----------|----------------|
| `README.md` | 构建方式、环境要求、技术栈、顶层目录、架构总览 | 构建流程变化、新增/移除技术依赖、新增顶层目录、架构描述变化 |
| `docs/` | 文档目录:其他语言版本的 README(`README.<lang>.md` ×15,语言切换互跳 + 回指根 `../README.md`)与产品演示文稿 intro*.pptx | 新增/删除语种、修改相对链接、更新演示文稿、或 main/ 顶层目录树变化时 |
| `目录结构说明.md` | 每个目录/文件的职责、测试清单、新增代码规则 | 新增/删除/重命名/改职责的任何文件或目录；新增测试 |
| `script/scripts/lua_api.txt` | Lua 脚本指南（==Meta==/@name 声明规范 + editor/win/regex API、params、pecia_lang、folder.ini、--!pui；随 scripts 文件夹复制到 build/script，PeciaLua 帮助按钮读取） | 新增/修改/删除任何 Lua API 或脚本声明语法 |
| `lang/en.ini` `lang/zh-CN.ini` | 官方 UI 文案 | 新增/修改/删除任何 I18n 键（两文件键集必须一致） |
| `THIRD-PARTY-NOTICES.md` | 第三方组件许可证清单、修改说明与分发义务（随发布包分发） | 新增/移除/更换/修改任何第三方依赖（含 Rust crate 版本变化）时 |
| `build/FLTK_PATCHES.md` | FLTK 源码修改记录 | 修改 `.thirdparty/fltk-*` 源码时（升级 FLTK 按此重放） |
| `build/LITEHTML_PATCHES.md` | litehtml 源码修改记录（超长非 CJK 词拆词断行） | 修改 `.thirdparty/litehtml-*` 源码时（升级 litehtml 按此重放） |
| `AGENTS.md` | 项目规则 | 本文件自身规则变化 |
| `build/deps.txt` | 第三方库下载清单(2_download.bat 读取,`name=url\|marker` 格式,marker 为完整性探针) | 新增/移除/更换依赖库或下载地址、调整探针文件时 |
| `build/full.bat` | 总入口脚本(双击即用,按序调用 2→3→4→5) | 构建总流程/脚本编排变化时 |
| `build/1_check_env.bat` | 只读工具链体检(cmake/cargo/cl/nmake/rc/tar/curl + Rust host 校验) | 工具链要求/检查项变化时 |
| `build/2_download.bat` | 下载+解压第三方库到 .thirdparty/(含探针校验与残缺自动修复) | 下载/解压/完整性校验逻辑变化时 |
| `build/3_patch.bat` | 把 main/patches/ 单向应用到 .thirdparty/ | 补丁同步范围/逻辑变化时 |
| `build/4_build_rust.bat` | cargo fetch + cargo build --release 编两个 Rust FFI 静态库 | FFI 依赖拉取/构建逻辑变化时 |
| `build/5_build_pecia.bat` | 编译 FLTK+Pecia 三件套并跑 ctest(**真门禁**:测试失败即整条构建失败) | 编译流程/按需重编/依赖校验逻辑变化时 |
| `build/_common.bat` | 编号脚本共用的路径变量、tar/curl 解析、MSVC 环境装载与 `:fetch_rust_deps` 子例程 | 路径变量定义/tool 解析/环境探测入口变化时 |
| `build/msvc_env.bat` | MSVC/Windows SDK 环境自动探测(vswhere) | 环境探测逻辑变化时 |
| `.gitattributes` | 行尾与二进制属性(**.bat/.cmd 强制 CRLF**——cmd.exe 需要, LF 会导致解析错乱) | 新增文件类型/调整行尾策略时 |
| `build/pack.bat` | 打包 build/ 内容到 release/<版本>/ | 分发打包逻辑变化时 |
| `build/build_readme.md` | 构建流程总说明(工具/依赖分类/Rust FFI 归属/编号脚本/常见坑) | 构建流程/依赖分类/工具链要求/FFI 归属说明变化时 |
| `script/scripts/` | 内置默认 Lua 脚本（构建时复制到 build/script） | 新增/修改/删除默认脚本时（保持与 build/script 同步） |

规则：
1. **先改代码，后查清单**：任何改动完成后过一遍上表。
2. **test_docs 是门禁**：文档漏改导致的事实不一致（如 API 未记录、
   键集不齐）会直接让构建失败。
