# Pecia 项目规则（AGENTS.md）

本文件只对 `E:\tmp\AI.Agent\Pecia` 工作文件夹生效。任何 AI 工具在此文件夹工作时必须遵守。

## 临时产物规则（硬性）

1. **所有 AI 临时产物一律只放 `temp/` 目录**，包括：
   - 调试/验证脚本（.ps1、.py、.cpp 等）
   - 构建日志、命令输出（禁止重定向到项目根）
   - 编译中间文件（.obj、.exe）
   - 崩溃转储、分析工具
   - 规划/进度文件（task_plan.md、findings.md、progress.md 等）
2. **禁止向项目根目录写入任何文件**，除非用户明确要求。
3. 编译命令必须显式指定输出目录，例如：
   `cl /Fo:E:\tmp\Reasonix\Pecia\temp\ ...`
   或先 `cd temp` 再编译。
4. 删除临时文件时只动 `temp/` 内自己创建的文件。

## main/ 目录纯净性（2026-08-18 定案，硬性）

1. `main/` 只放源码与随源码的文档：代码、`main/patches/`（修改归档与
   FFI 壳源码）、`main/build/`（构建脚本与 patch 记录文档）、
   `main/LICENSE`、`main/THIRD-PARTY-NOTICES.md`、语言/脚本/测试等。
2. **禁止**在 `main/` 内出现任何编译产物（`.exe` `.dll` `.lib` `.obj`
   `.pdb` `.zip` 等）与发布包——发布包统一输出到**项目根 `release/`**
   （`pack.bat` 已内置：`OUT=%ROOT%\..\release\%VERSION%`）。
3. **禁止**在 `main/` 内放置第三方库本体——一律在 `.thirdparty/`；
   Rust 库源码由 cargo 从 crates.io 拉取（编译产物与 `target/` 都在
   `.thirdparty/<crate>/` 内，不进仓库）。

## 项目文件（禁止触碰，除非用户要求）

- `main/`（源码）、`build/`（exe 输出）、`temp/`（构建中间目录：`temp/cmake_build`、
  `temp/download_cache`）
- `.thirdparty/`（第三方依赖）、`mdview/`

## 文档维护规则（标准流程）

1. **文档清单**：`main/DOCS_MANIFEST.md` 列出全部随代码变化的
   文档文件及其更新时机。每次改动完成后**逐项核对**该清单。
2. **文档一致性检查是构建流程的一部分**：`test_docs` 单测（`5_build_pecia.bat`
   自动运行）验证：关键文件存在性、目录结构说明.md 提到的测试文件、
   lua_api.txt 记录的 API 是否在 LuaEngine 注册、en/zh-CN 键集一致、
   README 第三方库目录、DOCS_MANIFEST.md 列出的文件存在。文档改动
   后必须保持此测试全绿。
3. `main/README.md`、`main/目录结构说明.md`、`main/script/scripts/lua_api.txt`
   属于项目文档：改代码/API/目录结构时**同步更新**（不新增文件、不删除
   文件、不改职责的变更也应核对文档描述是否仍然成立）。
4. 新增 API 必须写进 `main/script/scripts/lua_api.txt`（AI 生成时同步）；
   新增目录/文件职责写进 `main/目录结构说明.md`；清单文件本身
   变化时更新 `main/DOCS_MANIFEST.md`。

## 惯例

- 备份快照：修改前按 `.bf.c++\MMDD-HHMM 说明` 命名建立副本（沿用现有约定）
- 构建：**双击 `main\build\full.bat`**（总入口，按序调用
  `1_check_env.bat` 体检 → `2_download.bat` 下载依赖 → `3_patch.bat`
  应用补丁 → `4_build_rust.bat` 编 Rust FFI → `5_build_pecia.bat` 编译+单测）。
  体检环境也可单独跑 `1_check_env.bat`（只读，不下载不编译）。
  输出到 `build\Pecia.exe`；构建成功后由 `main/CMakeLists.txt` 的
  `pecia_copy_licenses` 目标自动把
  `LICENSE`/`THIRD-PARTY-NOTICES.md`/`licenses\` 同步进 `build\`，
  使该目录可直接分发，与 `release\` 发布包内容一致。
  （老版一体化脚本 `setup.bat` / `build.bat` 已于 2026-09-12 删除，职责由 ②③④⑤ 取代。）
- 工具链探测：`main\build\msvc_env.bat`（用 `vswhere.exe` 定位 MSVC + Windows SDK，
  设置 `PATH`/`INCLUDE`/`LIB`；由各构建脚本以 `call` 调用，普通 cmd 即可）
- 生成器：一律 **NMake Makefiles**（`-G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release`）；
  VS 生成器在部分 Build Tools 环境探测不到编译器，勿用

## Markdown 预览集成规范（2026-08 定案）

1. **分栏限制**：预览布局为两栏（编辑 | 预览），使用 Fl_Tile，
   分隔条可拖动但比例限制在 **20%-80%** 之间，每次开启默认 50/50 均分。
2. **代码归属**：mdview 预览核心源码（md4c 转换、litehtml 容器适配、
   预览面板）**复制进 `main/mdview/`**，成为 Pecia 主程序的一部分（非独立 exe）。
3. **第三方库**：一律放外部 `.thirdparty/` 目录，main/ 内禁止存放第三方库源码。
4. **Rust 构建产物**：
   - **FFI 壳源码**（我们写的代码，随仓库上传）：`main/patches/mermaid-rs-renderer-0.3.1/mmdr-ffi/`（mermaid+SVG）
     与 `main/patches/RaTeX-0.1.14/ratex-ffi/`（LaTeX 公式），依赖从 crates.io 拉取
   - **Rust 库源码不手动下载**：mmdr / RaTeX / resvg 等全部由 cargo 从 crates.io 拉取
     （Cargo.lock 锁定版本，归档于各 FFI crate 目录）
   - cargo 中间产物（target/）：留在 `.thirdparty/<crate>/target/`（cargo 默认位置），
     该目录不进仓库；`main/` 内禁止出现任何 cargo 产物
   - 编译结果 `.lib` 位于 `.thirdparty/<crate>/target/release/`（如
     `mmdr-ffi/target/release/mermaid_ffi.lib`）供 CMake 链接；
     `main/` 只放轻量源码与文档
5. **外部运行时文件**：除 `pecia_regex.dll`（按需加载的正则引擎）外无其他外部件；
   代码中禁止硬编码绝对路径，一律相对 exe 定位。
6. **resvg 唯一性**：全项目只允许一份 resvg（crates.io 的 mmdr 依赖内 0.47），
   通过 FFI（mmdr_mermaid_to_png / mmdr_svg_to_png）进程内调用，
   禁止再引入独立 resvg/lunasvg 等 SVG 引擎。
7. **第三方库修改归档（硬性，防丢失防线，2026-08-11 强化）**：
   - **`main/patches/` 是全部库源码修改的「唯一权威」归档**。凡对
     `.thirdparty/` 内库源码的任何修改，修改后的**完整文件**必须放入
     `main/patches/<库名>/`（保持相对路径），文字记录保留在
     `main/build/FLTK_PATCHES.md` 等文档。
   - **用户备份有效性的前提**：用户手动备份 = 备份整个 `main/` 文件夹，
     `main/patches/` 随之完整保存；拿到 `main/` 即可完美复现构建结果。
     因此 `main/patches/` 内容**永远只增不减**（只允许添加新 patch 标记，
     不允许删除既有 patch 内容）。
   - **构建强制同步（已写死在 `3_patch.bat`，每次构建前自动执行）**：
     `main/build/3_patch.bat` 每次
     构建前**必须先**把 `main/patches/` 的内容覆盖到 `.thirdparty/`
     对应位置（`copy /Y`，只允许 patches → .thirdparty 方向），并检测补丁源码比
     已编译 lib 新时**重编对应库**。禁止依赖"手工保证 .thirdparty 状态"
     ——`.thirdparty/` 可能随时被重新解压/污染。
   - **禁止反向覆盖（血的教训）**：**禁止**把 `.thirdparty/` 中的文件
     复制到 `main/patches/` 来"归档"——`.thirdparty/` 可能处于被污染
     （原始版）状态，反向复制会把完整归档覆盖成残缺版（2026-08-11
     曾因此丢失 Patch 1/2/3，恢复自 `.bf.c++\0810-2154` 备份）。
     AI 修改 `.thirdparty/` 库源码**前**，必须先核对 `main/patches/`
     对应归档仍是完整补丁版（含全部历史 patch 标记，如 Fl_Menu.cxx
     须含 Fl_Double_Window 基类 + <=999 阈值等）；确认归档完整后，
     修改 `.thirdparty/`，**再从 `.thirdparty/` 复制回归档**（此刻方向
     合法，因为两侧都是修改后的完整版）。
   - **库重编必须完整**：改了 `main/patches/` 后，先重编对应库
     （FLTK：重跑 `3_patch.bat` 同步后由 `5_build_pecia.bat` 的 `[5/7]`
     按时间戳检测补丁源比 lib 新时自动重编），再编主程序；
     `5_build_pecia.bat` 已内置该触发逻辑，无需手工。
   - **恢复演练**：若怀疑 `.thirdparty/` 被污染（patch 丢失），恢复 =
     从 `main/patches/` 重新复制覆盖 + 重编库。若连 `main/patches/`
     都残缺，从最近的 `.bf.c++` 备份恢复（先确认备份内文件含完整
     patch 标记再使用）。
   - **禁止只改 .thirdparty 而不归档**——main/ 内的代码与补丁文件必须
     能独立重建整个项目（含自写的 FFI 壳等无法从网上下载的代码）。

## 发布前核查清单（上传 Git 仓库前必做）

每次准备合并/发布（push 到 Git 仓库）前，按顺序完成以下核查，全部通过后方可提交/打包。
这是可交付的硬性门槛，不可跳过。

1. **测试门禁 + 文档一致性**（ctest 14/14，含 test_docs）
   - 跑 `5_build_pecia.bat`（或完整 `full.bat`），确认 `100% tests passed, 0 failed out of 14`，
     其中第 7 项 `test_docs` 须全绿：DOCS_MANIFEST / 目录结构说明 / README / lua_api /
     en-zh-CN 键集一致、文档提到的文件存在。
2. **静态异味扫描**（grep 源码）
   - 硬编码绝对路径（`[A-Z]:\\`）：本运行时代码**零硬编码**（允许 `test/` 下测试桩路径）。
   - 调试残留：`printf` / `fprintf(stderr)` / `OutputDebugString` 等应清理。
   - `TODO` / `FIXME` / `XXX`：本项目源码不得遗留（第三方 `patches/` 内上游注释除外）。
3. **第三方声明准确性**
   - `THIRD-PARTY-NOTICES.md` 的 patch 数量 / 库版本必须与 `main/patches/` 实际文件一致
     （例：FLTK 实为 5 个 patch 文件，非 7）。
4. **版本控制卫生**（git status 审查，不主动提交）
   - 确认构建脚本（`build/*.bat`、`full.bat`、`_common.bat`、`msvc_env.bat`）、
     `lang/*.ini`、`theme/*.ini`、`.gitattributes`、`THIRD-PARTY-NOTICES.md`、
     新增核心头文件等**均已 `git add`**；
   - 确认项目根**无**误建的 `.gitignore`（`main/.gitignore` 已正确忽略构建产物）；
   - 确认无密钥 / 临时文件误入库。
5. **编译警告审计**
   - 全目标 `/W4 /permissive-` 下增量 / 全量构建，抓 `warning Cxxxx` 并清零
     （非致命但影响发布质量）。
6. **干净全量构建可重复性**
   - 跑 `full.bat` 冷启动全链路（检查环境 → 下载 → 补丁 → 编 Rust → 编 FLTK → 编 Pecia → ctest），
     确认他人 clone 后可一键构建；P6 联网首次下载路径需在外网干净机器验证。
7. **运行时冒烟 + 发布打包**
   - 跑 `pack.bat` 产出 `release/`；确认 `Pecia.exe` / `PeciaLua.exe` / `PeciaAIChat.exe`
     及 `lang/` `theme/` `script/` 齐全；实际启动冒烟确认无崩溃。

> 红线：所有改动**未经用户明确许可不得 `git commit`**；本清单只检查，提交由用户拍板。
