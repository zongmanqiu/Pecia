# 文档清单（改代码时逐项核对）

每次对源码/API/目录结构/配置做出改动后，**按下表逐项检查**对应的
文档文件是否需要同步修改。改完后运行 `build/5_build_pecia.bat`（或 `build/full.bat`）——
`test_docs` 单测会自动验证文档与实际的"事实一致性"（API 注册、
测试清单、语言键对齐、第三方库目录），任何一处漏改都会变红。

| 文件 | 覆盖内容 | 需要更新的时候 |
|------|----------|----------------|
| `README.md` | 项目介绍、下载、界面、环境要求、构建入口、技术栈、顶层目录 | 构建流程变化、新增/移除技术依赖、新增顶层目录、架构描述变化 |
| `docs/` | 文档目录:`docs/README/` 子目录(15 份语言 README + 8 张界面截图)、发行流程清单 RELEASE_CHECKLIST.md 与产品演示文稿 intro*.pptx | 新增/删除语种、修改相对链接、更新演示文稿、或 main/ 顶层目录树变化时 |
| `目录结构说明.md` | 每个目录/文件的职责、测试清单、新增代码规则 | 新增/删除/重命名/改职责的任何文件或目录；新增测试 |
| `开发指南.md` | 架构速览、开发流程、改代码后需同步的文档对照、提交前检查清单、UI 改动规约（复用与刷新）、版本号位置、**第十节「平台契约与已知缺口」**（UI 线程/异常边界/持久化/更新/分层例外的现状与缺口，均带文件:行号） | 开发流程/检查项/版本号位置/构建方式/UI 复用与刷新约定变化时；**修复第十节某条缺口后必须同步删改对应条目** |
| `script/scripts/lua_api.txt` | Lua 脚本指南（==Meta==/@name 声明规范 + editor/win/regex API、params、pecia_lang、folder.ini、--!pui；随 scripts 文件夹复制到 build/script，PeciaLua 帮助按钮读取；末尾可追加 AI 提示词，由 PeciaAIChat 拼接进 system 消息） | 新增/修改/删除任何 Lua API 或脚本声明语法 |
| `lang/en.ini` `lang/zh-CN.ini` + 其余 14 份（ar/de/es/fr/hi/id/it/ja/ko/pt-BR/ru/tr/vi/zh-TW） | 官方 UI 文案：en/zh-CN 为母本，其余为翻译 | 新增/修改/删除任何 I18n 键——**16 份全改，键集一致**。注意 `test_docs` 只比对 en↔zh-CN，其余 14 份漏改不会被门禁发现 |
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
| `build/ensure_header_deps.ps1` | 手动恢复工具：怀疑构建残留陈旧 `.obj` 时删除项目对象（构建脚本不再自动调用；Makefile 依赖现由 `CMAKE_DEPENDS_USE_COMPILER=FALSE` + CMake 自带 include 扫描器负责） | 依赖探测策略或清理范围变化时 |
| `build/5_build_pecia.bat` | 编译 FLTK+Pecia 三件套并跑 ctest(**真门禁**:测试失败即整条构建失败) | 编译流程/按需重编/依赖校验逻辑变化时 |
| `build/_common.bat` | 编号脚本共用的路径变量、tar/curl 解析、MSVC 环境装载与 `:fetch_rust_deps` 子例程 | 路径变量定义/tool 解析/环境探测入口变化时 |
| `build/msvc_env.bat` | MSVC/Windows SDK 环境自动探测(vswhere) | 环境探测逻辑变化时 |
| `.gitattributes` | 行尾与二进制属性(**.bat/.cmd 强制 CRLF**——cmd.exe 需要, LF 会导致解析错乱) | 新增文件类型/调整行尾策略时 |
| `docs/RELEASE_CHECKLIST.md` | 版本发行流程清单(版本号同步/质量门禁/打包/gitee 发行/自动更新验证) | 发行步骤或自动更新地址规则变化时 |
| `docs/STRESS_TEST_PLAN.md` | 发版前压力测试计划(大文档/大文本/极限结构/多标签/长时/异常/脚本/打包链路,含实测基线与判定边界) | 压力用例、判据、基线数据或已知问题清单变化时 |
| `HISTORY.md` | 更新历史(每版本在顶部增量追加,旧内容保持不变) | 每次发新版本时(见 docs/RELEASE_CHECKLIST.md) |
| `build/build_readme.md` | 构建流程总说明(工具/依赖分类/Rust FFI 归属/编号脚本/常见坑) | 构建流程/依赖分类/工具链要求/FFI 归属说明变化时 |
| `script/scripts/` | 内置默认 Lua 脚本（构建时复制到 build/script） | 新增/修改/删除默认脚本时（保持与 build/script 同步） |

规则：
1. **先改代码，后查清单**：任何改动完成后过一遍上表。
2. **test_docs 是门禁**：文档漏改导致的事实不一致（如 API 未记录、
   键集不齐）会直接让构建失败。
