# Pecia 发行流程清单（RELEASE_CHECKLIST.md）

> 每次发布新版本（如 1.0.0 → 1.0.1）时**按顺序逐项执行**。
> 本文档归并了散落在 `build/build_readme.md` 第六节、`build/pack.bat`、
> `core/Updater.cpp` 中的发行相关要求。

---

## 一、版本号修改（单一数据源原则）

| 位置 | 改什么 | 说明 |
|---|---|---|
| `main/CMakeLists.txt` 第 2 行 | `project(Pecia VERSION x.y.z ...)` | **唯一权威来源**。`PECIA_VERSION` 宏由此注入（见同文件 `target_compile_definitions`），关于页与更新检查自动跟随 |
| `main/build/pack.bat` 第 44 行 | `set "VERSION=x.y.z"` | 仅是 CMake 解析失败时的**回退值**，必须与上面同步 |
| `main/README.md` + `main/docs/README.<lang>.md` ×15 | 下载链接与版本表述：`releases/download/<ver>/Pecia_x64_<ver>.zip` | 16 份文件**必须同步**（根与 docs 内各 1 处下载行） |
| `main/example/ex1.markdown/ex1.md` | 版本表 `Pecia \| x.y.z` | 示例文档中的版本引用 |
| `main/HISTORY.md` | 在文件**顶部**追加新版本小节（日期 + 新增/修复/变更） | **增量原则：只追加，旧内容一律保持原样**，不修改不删除 |

改完用 `grep -rn "旧版本号" main/` 复核零残留（注意排除 `.thirdparty/`）。

## 二、发行前质量门禁（全部通过才可发行）

1. **全量构建**：双击 `build/full.bat`（或 `5_build_pecia.bat`）；
2. **14 个 ctest 全绿**（构建链内置，红灯即中止）；
3. **文档门禁**：`build/test_docs.exe` 输出 `28 checks, 0 failures`
   （改过任何文档/API/目录后必跑）；
4. **零 C 警告**（`/W4` 级别，构建日志无 `warning C`）。

## 三、打包

- 运行 `main/build/pack.bat` → 产出 `release/<版本>/` 与
  **`Pecia_x64_<版本>.zip`**；
- zip 内容：`exe 三件套 + lang/ + theme/ + script/ + LICENSE +
  THIRD-PARTY-NOTICES.md + licenses/`；
- ⚠️ **zip 文件名不可改**：`Updater.cpp` 用 tag 拼出确定性下载地址
  `releases/download/<tag>/Pecia_x64_<tag>.zip`，名字对不上自动更新就 404。

## 四、gitee 发行（自动更新的生效点）

1. 在 gitee 仓库创建 release，**tag 名 = 版本号**（如 `1.0.1`）——
   `Updater.cpp` 通过 `api/v5/.../releases/latest` 取最新 tag 与本地版本
   三段比较，决定是否提示更新；
2. 上传第三节的 `Pecia_x64_<版本>.zip` 作为附件；
3. **顺序要求**：先发 release 再测自动更新——用旧版本 exe 点"检查更新"
   时，远端产物会覆盖被测 exe（本机实测教训）。

## 五、发行后验证

1. 下载链接抽查：README（根 + 至少一个语言版）里的链接能下到新 zip；
2. 自动更新链路：旧版本启动 → 检测到新版本 → 下载 → 覆盖（`robocopy
   /e /is /it`：同名覆盖、多余文件保留）→ 重启后关于页版本号正确；
3. settings.ini 兼容性：新版本首启后，旧配置键保留、新增键补默认值、
   用户数据（`ai_api_key`、最近文件等）无损。

## 六、职责边界

- `main/更新.txt`（更新日志）由**用户主控**，AI 不主动改；
- `git commit / push` 未经用户下令不执行；
- release 的创建与附件上传需用户登录 gitee 操作（或明示授权后进行）。

---

*本文档由 2026-09-17 版本 1.0.0 → 1.0.1 升级实践整理而成。*
