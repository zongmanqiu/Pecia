# Pecia 项目开发规则（AI 协作者必读）

> 本文件是**规则路由表**：先读「红线」，再按改动类型查「路由表」到对应文件读详细约束。
> 详细规则只在专门文件里定义一次（单一来源），本文件不重复展开。

## 一、红线（任何改动都适用，违反即返工）

1. **`git commit` 未经用户明确许可不执行**（用户提起并点头才做，禁止反复提醒）。
2. **`main/` 内不出现编译产物**（`.exe/.dll/.lib/.obj/.pdb/.zip`）、发布包或第三方库源码——一律放项目根 `build/`、`release/`、`.thirdparty/`。
3. **临时产物只放 `temp/`**：调试脚本、构建日志、崩溃转储、规划/进度文件，不得写入项目根或 `main/`；编译命令显式指定输出目录。
4. **改完必须重编 + `ctest` 全绿才交付**；改过文档/API/目录后必跑文档门禁（`test_docs`，红灯即中止）。
5. **先实测，再下结论**：与平台/系统行为相关的判断先用最小代码或探针验证，不凭印象（本项目曾多次因此翻车）。
6. **回调边界必须兜异常**：新增线程入口、原生回调（WndProc / IME 钩子）、定时器回调、`Fl::awake` lambda，函数体第一件事 `try { … } catch (...) { 记日志 }`。未捕获异常会 fastfail 成 `0xC0000409` 并绕过崩溃处理器，**连 dump 都拿不到**（详见 `开发指南.md` 第十节 §2）。
7. **`mdview/` 预览保持只读**：不新增鼠标交互（链接点击、hover、复选框、复制按钮）。渲染 + 滚动 + 缩放 + 拖放 + TOC 跳转是完整边界，加交互要连带"预览→文档"反向链路，已明确不做。

## 二、改动路由表（按改动类型读对应文件）

| 改动类型 | 必读文件 | 核心约束 |
|---|---|---|
| C++ 源码（`core/` `editor/` `ui/` `AIChat/` `LuaTool/` `mdview/` `script/`） | `目录结构说明.md`、`开发指南.md` | 分层依赖 `core ← editor ← ui`、`core ← script ← ui`（4 处已知例外见开发指南十节 §5，不得新增）；新增文件/目录须登记 `目录结构说明.md`；`test/` 只测纯逻辑不依赖 GUI；**编译零警告且由 `/W4 /WX` 强制**（第三方目标不加 `/WX`）；**新增 GUI 测试必须复用产品启动序列，不得新增 `*_stubs.cpp`**（十节 §7） |
| 线程 / 崩溃处理 / 配置持久化 / 自动更新（`core/CrashReport.cpp`、`core/Config.cpp`、`core/Updater.cpp`） | `开发指南.md` 第十节「平台契约与已知缺口」 | UI 线程铁律、回调异常兜底、原子写与 `schema_version`、更新包校验与回滚——**改前先读该节，避免把已知缺口当已实现** |
| UI 改动（界面/弹层/窗口装饰/主题着色） | `开发指南.md` 第九节「UI 改动规约」 | 控件只复用 ThemeWidgets.h/DialogBase 不复制；窗口装饰只走 show() 收尾同一条时序；主题切换后重建文本样式表；弹层走 Fl_Menu_Item 公共路径不自绘 |
| 文档（任何 `.md`/`.txt`） | `DOCS_MANIFEST.md` | 逐项核对需同步的文档；`test_docs` 门禁自动校验事实一致性，漏改即构建失败 |
| 构建脚本（`build/*.bat`；打包已改为手动，无对应脚本） | `build/build_readme.md`、`DOCS_MANIFEST.md` | 路径变量集中在 `_common.bat`；工具链由 `msvc_env.bat`（vswhere）探测；禁硬编码绝对路径 |
| Lua API / 脚本 | `script/scripts/lua_api.txt` | API 声明规范（`==Meta==`/`@name`）；新增/修改 API 必须写入该文件 |
| UI 文案（`lang/*.ini`） | `DOCS_MANIFEST.md` | `lang/` 下**16 份全改**，键集必须一致。`test_docs` 只比对 `en`/`zh-CN`，其余 14 份无自动校验（漏改 = 用户看到裸 key） |
| 第三方库（`.thirdparty/` 内源码） | 本文件第三节 + `build/FLTK_PATCHES.md`/`build/LITEHTML_PATCHES.md` + `THIRD-PARTY-NOTICES.md` | 见第三节（最高危） |
| 发版 | `docs/RELEASE_CHECKLIST.md` | 版本号 5 处同步、质量门禁、打包、gitee 发行、自动更新验证 |
| Markdown 预览（`mdview/`） | `开发指南.md` 架构速览 | 自绘预览（非 `Fl_Tile`）：预览与编辑器分栏、分隔条比例持久化；预览**只读**（不做链接点击/hover，见 2026-10-23 决策）；全项目只保留一份 SVG 引擎（mmdr FFI）；无外部运行时 DLL（`dwmapi.dll` 仅系统 DLL 动态取函数） |

## 三、第三方库与补丁归档（最高危规则，全文保留）

- `patches/` 是第三方库源码修改的**唯一权威归档**：凡对 `.thirdparty/` 内库源码的修改，修改后的完整文件须按相对路径放入 `patches/<库名>/`。
- 用户备份 = 备份整个 `main/` 文件夹，因此 `patches/` **只增不减**（只允许新增补丁，不允许删除既有内容）。
- 构建时 `3_patch.bat` 先把 `patches/` 单向覆盖到 `.thirdparty/`（只允许 patches → .thirdparty 方向），并检测补丁源码比已编译库新时重编对应库。
- **禁止反向覆盖**：不得把 `.thirdparty/` 的文件复制回 `patches/` 充当归档——`.thirdparty/` 可能处于未打补丁状态，反向复制会造成归档残缺。
- 修改 `.thirdparty/` 库源码的正确顺序：先核对 `patches/` 归档完整（含全部历史补丁标记）→ 改 `.thirdparty/` → 再从 `.thirdparty/` 复制回归档。
- 禁止只改 `.thirdparty/` 而不归档。`main/` 内的代码与补丁必须能独立重建整个项目（含自写 FFI 壳等无法从网上下载的代码）。

## 四、文档与门禁体系

- `DOCS_MANIFEST.md` 是随代码变化的文档清单，改动完成后逐项核对。
- `test_docs` 单测（`5_build_pecia.bat` 自动运行）校验事实一致性：关键文件存在、`目录结构说明.md` 提到的测试文件存在、`lua_api.txt` 记录的 API 在 LuaEngine 注册、en/zh-CN 键集一致、README 第三方库目录、`DOCS_MANIFEST.md` 列出的文件存在。文档改动后必须保持该测试全绿。
- 多语言 README 位于 `docs/README/`（`README.<lang>.md` ×15）；根 `README.md` 必须留在 `main/` 根目录（Gitee 首页依赖它自动展示）。改动顶层目录树或增删语种时，16 份文件的语言切换链接与目录树须同步（根指向 `docs/…`，`docs/` 内指向 `../…`）。

## 五、提交 / 发布

- 提交前按 `开发指南.md` 第四节清单逐项检查；发版按 `docs/RELEASE_CHECKLIST.md` 执行。
- `git commit` 未获明确许可前不得执行。
