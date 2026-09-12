# 第三方组件与许可证说明（Third-Party Notices）

本文件列出 Pecia 引用的全部第三方组件、各自许可证、是否被本项目修改
及修改记录位置。**随源码与发布包一起分发**（`pack.bat` 会将其拷入发布目录，
与 `LICENSE` 并列）。

> 本项目自身采用 **GNU AGPL-3.0**，见 [LICENSE](LICENSE)。
> 本说明仅作许可证合规提示，不构成法律意见。

---

## 一、直接引用的第三方库（`build/deps.txt` 下载，位于 `.thirdparty/`）

| 组件 | 版本 | 来源 | 许可证 | 本项目修改 |
|------|------|------|--------|-----------|
| FLTK | 1.4.5 | https://github.com/fltk/fltk | FLTK License（LGPL-2.0 + 静态链接例外） | ✅ 5 个文件（见『三、本项目对第三方库源码的修改』） |
| Lua | 5.5.1 | https://github.com/lua/lua | MIT | 否 |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 | BSD-3-Clause | 否 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c | MIT | 否 |
| litehtml | 0.10 | https://github.com/litehtml/litehtml | BSD-3-Clause（内嵌 gumbo：MIT） | ✅ 1 个 patch（见『三、本项目对第三方库源码的修改』） |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb | MIT（dual：公有领域 / MIT） | 否 |

> 各库完整的版权与许可文本均保留在 `.thirdparty/<库名>/` 对应目录
> （如 `.thirdparty/fltk-1.4.5/COPYING`、`.thirdparty/md4c-2026-09-11/LICENSE.md`、
> `.thirdparty/pcre2-10.47/LICENCE.md`），随源码分发。被修改的文件内
> **保留上游版权头**（如 FLTK 源码头 `Copyright 1998-2024 by Bill Spitzak
> and others`），新增改动处均留有 `PATCHED by Pecia` 注释标记。

## 二、Rust 依赖树（crates.io 拉取）

代码在 `main/patches/mermaid-rs-renderer-0.3.1/mmdr-ffi/` 与
`main/patches/RaTeX-0.1.14/ratex-ffi/`（**本项目自写的 FFI 壳，MIT**），
依赖由 cargo 从 crates.io 自动拉取，版本锁定于各 crate 的 Cargo.lock。
关键组件：

| 组件 | 版本 | 来源 | 许可证 |
|------|------|------|--------|
| mermaid-rs-renderer（mmdr，Mermaid→SVG 渲染） | 0.3.1 | crates.io | MIT |
| RaTeX 系列（ratex-parser / layout / svg / types 等，LaTeX 公式） | 0.1.14 | crates.io | MIT |
| usvg / resvg（SVG→位图） | 0.47 | crates.io | Apache-2.0 OR MIT |
| fontdb（字体查询） | 0.23 | crates.io | MIT |

两个 Cargo.lock 合计 **181 个唯一 crate**，许可证分布全部为
MIT / Apache-2.0 / BSD-2/3-Clause / Zlib / Unlicense（公有领域）等
**宽松许可，不含 GPL/AGPL/MPL 等强 copyleft**（2026-08 逐 crate 审计
结论；个别 crate 含 Unicode-3.0 数据许可，同为宽松）。每个 crate 的
完整许可证文本保存在 cargo registry 缓存（`~/.cargo/registry/src/`）
与 `.crate` 归档中，随 Rust 工具链一并分发。

## 三、本项目对第三方库源码的修改（全部归档，可重放）

| 库 | 内容 | 记录文档 | 归档位置 |
|----|------|----------|----------|
| FLTK | 菜单勾选框填充色分离（check_color）；禁用内部 fakemenu 冗余窗口；菜单高亮切换强制双缓冲；文本撤销事务分组（begin/end_undo_transaction）；超链接下划线 1px 细化 | `build/FLTK_PATCHES.md` | `main/patches/fltk-1.4.5/` |
| litehtml | 超长非 CJK 词（长 URL / 长英文）按 8 字符拆词断行 | `build/LITEHTML_PATCHES.md` | `main/patches/litehtml-0.10/` |

`main/patches/` 存放的是**修改后完整文件**，构建时由 `3_patch.bat` 自动覆盖到 `.thirdparty/`；将来升级库版本时按两张
`*_PATCHES.md` 重新应用。

## 四、许可证义务摘要

1. **AGPL-3.0（本项目自身）**：发布二进制需附带 [LICENSE](LICENSE) 全文。
2. **FLTK License**（LGPL-2.0 + 例外）：允许静态链接且不要求开源；
   分发时须保留版权声明与 COPYING 文本（本项目发布补丁版，已在
   NOTICES 与归档文件中标明修改）。
3. **MIT / BSD / Apache-2.0**：分发源码或二进制时须保留各版权与许可
   声明；本项目以各自上游许可使用，未做任何超出许可范围的改动。
4. 修改第三方库不改变其上游许可证；本项目补丁以同一许可证继续分发
   （FLTK 修改版仍按 FLTK License，litehtml 修改版仍按 BSD-3-Clause）。

## 五、发布包（项目根 `release/`）内容

`build/pack.bat` 生成的发布目录 / zip **输出到项目根 `release/`**
（`main/` 上一级，与 `.thirdparty/`、`build/` 同级；`main/` 内只放源码，
不存放任何发布产物）。发布包中与许可证相关的文件：

> **`build/` 输出目录同样可分发**：每次构建成功后会自动把
> 下列 LICENSE、THIRD-PARTY-NOTICES.md 与 `licenses/` 同步进 `build/`
> （由 `main/CMakeLists.txt` 的 `pecia_copy_licenses` 目标完成，
> 源文件取自 `main/` 与 `.thirdparty/`），
> 因此 `build/` 与 `release/` 的许可证内容保持一致，任取其一分发均可。

| 文件 | 说明 |
|------|------|
| `LICENSE` | AGPL-3.0 全文（本项目，自 `main/LICENSE` 拷贝） |
| `THIRD-PARTY-NOTICES.md` | 本文件（组件清单、修改说明、义务摘要、Lua 许可全文附录） |
| `licenses/` | 各直接链接 C/C++ 库的许可证原文（自 `.thirdparty/` 拷贝）：`FLTK-COPYING.txt`、`litehtml-LICENSE.txt`、`lua-LICENSE.html`、`md4c-LICENSE.md`、`pcre2-LICENCE.md`、`pcre2-COPYING.txt`、`stb-LICENSE.txt` |

> Lua 上游 tar 包根目录未随附独立 LICENSE 文件，其 MIT 声明位于
> `doc/readme.html`（已按上表原样拷贝），本文件附录中也复现了一份
> （权威文本以源码 `lua.h` 末尾声明为准）。
> Rust crate 的完整许可文本保存在 cargo registry 缓存中（见『二、Rust 依赖树（crates.io 拉取）』说明），
> 关键组件的许可以本文件清单为准。

## 附录：Lua 许可全文（MIT）

以下为 `lua.h` 文件末尾的官方版权与许可声明（权威文本以源码为准）：

```
Copyright (C) 1994-2026 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```