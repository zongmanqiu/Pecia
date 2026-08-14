# 文档清单（改代码时逐项核对）

每次对源码/API/目录结构/配置做出改动后，**按下表逐项检查**对应的
文档文件是否需要同步修改。改完后运行 `build\build.bat`——
`test_docs` 单测会自动验证文档与实际的"事实一致性"（API 注册、
测试清单、语言键对齐、第三方库目录），任何一处漏改都会变红。

| 文件 | 覆盖内容 | 需要更新的时候 |
|------|----------|----------------|
| `README.md` | 构建方式、环境要求、技术栈、顶层目录 | 构建流程变化、新增/移除技术依赖、新增顶层目录 |
| `目录结构说明.md` | 每个目录/文件的职责、测试清单、新增代码规则 | 新增/删除/重命名/改职责的任何文件或目录；新增测试 |
| `script/scripts/lua_api.txt` | Lua 脚本指南（==Meta==/@name 声明规范 + editor/win/regex API、params、pecia_lang、folder.ini、--!pui；随 scripts 文件夹复制到 build/script，PeciaLua 帮助按钮读取） | 新增/修改/删除任何 Lua API 或脚本声明语法 |
| `lang/en.txt` `lang/zh-CN.txt` | 官方 UI 文案 | 新增/修改/删除任何 I18n 键（两文件键集必须一致） |
| `build/FLTK_PATCHES.md` | FLTK 源码修改记录 | 修改 `.thirdparty/fltk-*` 源码时（升级 FLTK 按此重放） |
| `build/LITEHTML_PATCHES.md` | litehtml 源码修改记录（超长非 CJK 词拆词断行） | 修改 `.thirdparty/litehtml-*` 源码时（升级 litehtml 按此重放） |
| `AGENTS.md` | 项目规则 | 本文件自身规则变化 |
| `build/deps.txt` | 第三方库下载清单(setup.bat 读取,URL 可编辑) | 新增/移除/更换依赖库或下载地址时 |
| `build/setup.bat` / `build/vcvars_detect.bat` | 一键依赖准备与 vcvars 探测(环境要求见 README) | 构建流程/环境探测逻辑变化时 |
| `plugin/pecia_regex.h` | 正则引擎 DLL 导出接口（代码即文档，改动时同步 script/scripts/lua_api.txt 的 regex 部分） | regex API 变化时 |
| `script/scripts/` | 内置默认 Lua 脚本（构建时复制到 build/script） | 新增/修改/删除默认脚本时（保持与 build/script 同步） |

规则：
1. **先改代码，后查清单**：任何改动完成后过一遍上表。
2. **test_docs 是门禁**：文档漏改导致的事实不一致（如 API 未记录、
   键集不齐）会直接让构建失败。
3. 用户文档（代码架构、改进清单、更新日志）不在此清单，
   由用户决定是否更新。
