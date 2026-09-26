# Pecia 项目开发规则

本文件是 Pecia 仓库的工程约定。开发流程与提交前清单另见 `开发指南.md`，
发行步骤见 `docs/RELEASE_CHECKLIST.md`。

## 1. 目录职责

- `main/`：只放源码与随源码的文档——代码、`patches/`（库修改归档与自写
  FFI 壳源码）、`build/`（构建脚本与 patch 记录）、`docs/`（多语言 README）、
  `LICENSE`、`THIRD-PARTY-NOTICES.md`、语言/脚本/测试等。
- 项目根：`build/`（exe 输出）、`temp/`（构建中间目录）、`release/`（发布包）、
  `.thirdparty/`（第三方库本体）。
- `main/` 内**不得**出现编译产物（`.exe/.dll/.lib/.obj/.pdb/.zip` 等）、
  发布包或第三方库源码——一律放在项目根对应目录。

## 2. 临时产物

- 调试脚本、构建日志、编译中间文件、崩溃转储、规划/进度文件一律只放
  `temp/`，不得写入项目根或 `main/`。
- 编译命令需显式指定输出目录（如 `cl /Fo:...\temp\ ...`）。
- 清理时只删除自己在 `temp/` 内创建的文件。

## 3. 文档维护

- `DOCS_MANIFEST.md` 是随代码变化的文档清单，改动完成后逐项核对。
- `test_docs` 单测（`5_build_pecia.bat` 自动运行）校验事实一致性：关键文件
  存在、`目录结构说明.md` 提到的测试文件、`lua_api.txt` 记录的 API 是否在
  LuaEngine 注册、en/zh-CN 键集一致、README 第三方库目录、
  `DOCS_MANIFEST.md` 列出的文件存在。文档改动后必须保持该测试全绿。
- 改代码/API/目录结构时同步更新 `README.md`、`目录结构说明.md`、
  `script/scripts/lua_api.txt`；新增 API 写入 `lua_api.txt`，新增目录/文件
  职责写入 `目录结构说明.md`，清单文件本身变化时更新 `DOCS_MANIFEST.md`。
- 多语言 README 位于 `docs/`（`README.<lang>.md` ×15）；根 `README.md`
  必须留在 `main/` 根目录（Gitee 首页依赖它自动展示）。改动顶层目录树或
  增删语种时，16 份文件的语言切换链接与目录树须同步（根指向 `docs/…`，
  `docs/` 内指向 `../…`）。

## 4. 第三方库与补丁归档

- `patches/` 是第三方库源码修改的**唯一权威归档**：凡对 `.thirdparty/` 内
  库源码的修改，修改后的完整文件须按相对路径放入 `patches/<库名>/`。
- 用户备份 = 备份整个 `main/` 文件夹，因此 `patches/` **只增不减**（只允许
  新增补丁，不允许删除既有内容）。
- 构建时 `3_patch.bat` 先把 `patches/` 单向覆盖到 `.thirdparty/`（只允许
  patches → .thirdparty 方向），并检测补丁源码比已编译库新时重编对应库。
- **禁止反向覆盖**：不得把 `.thirdparty/` 的文件复制回 `patches/` 充当归档
  ——`.thirdparty/` 可能处于未打补丁状态，反向复制会造成归档残缺。
- 修改 `.thirdparty/` 库源码的正确顺序：先核对 `patches/` 归档完整（含全部
  历史补丁标记）→ 改 `.thirdparty/` → 再从 `.thirdparty/` 复制回归档。
- 禁止只改 `.thirdparty/` 而不归档。`main/` 内的代码与补丁必须能独立重建
  整个项目（含自写 FFI 壳等无法从网上下载的代码）。

## 5. Markdown 预览与外部依赖

- 预览为两栏（编辑 | 预览），使用 Fl_Tile，分隔条可拖动但比例限制在
  **20%–80%**，每次开启默认 50/50。
- 预览核心源码（md4c 转换、litehtml 容器适配、预览面板）在 `mdview/`，是
  主程序的一部分（非独立 exe）。
- 全项目只保留一份 SVG 引擎：crates.io 的 mmdr（内含 resvg），通过 FFI
  （`mmdr_mermaid_to_png` / `mmdr_svg_to_png`）进程内调用；禁止再引入独立
  resvg/lunasvg 等。
- 外部运行时文件仅 `pecia_regex.dll`（按需加载）；代码中禁止硬编码绝对
  路径，一律相对 exe 定位。

## 6. 构建

- 总入口：双击 `build/full.bat`，按序执行 `1_check_env.bat`（体检）→
  `2_download.bat`（下载依赖）→ `3_patch.bat`（应用补丁）→
  `4_build_rust.bat`（编 Rust FFI）→ `5_build_pecia.bat`（编译 + ctest）。
- 只改代码重编时直接跑 `5_build_pecia.bat`：它会自行同步补丁，并按需调用
  `4_build_rust.bat`。
- 生成器统一用 NMake Makefiles：
  `-G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release`（VS 生成器在部分 Build
  Tools 环境探测不到编译器，勿用）。
- 工具链探测：`build/msvc_env.bat`（用 `vswhere.exe` 定位 MSVC 与 Windows
  SDK，设置 `PATH`/`INCLUDE`/`LIB`，由各脚本以 `call` 调用）。
- 构建成功后，`CMakeLists.txt` 的 `pecia_copy_licenses` 目标自动把
  `LICENSE`/`THIRD-PARTY-NOTICES.md`/`licenses/` 同步进 `build/`，使该目录
  可直接分发，与 `release/` 发布包一致。

## 7. 提交 / 发布

- 提交前按 `开发指南.md` 第四节清单逐项检查；发版按
  `docs/RELEASE_CHECKLIST.md` 执行。
- `git commit` 未获明确许可前不得执行。