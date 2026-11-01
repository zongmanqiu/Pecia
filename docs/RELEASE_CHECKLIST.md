# Pecia 发行流程清单（RELEASE_CHECKLIST.md）

> 每次发布新版本（如 1.0.0 → 1.0.1）时**按顺序逐项执行**。
> 本文档归并了散落在 `build/build_readme.md` 第六节、`core/Updater.cpp`
> 中的发行相关要求。

---

## 一、版本号修改（单一数据源原则）

| 位置 | 改什么 | 说明 |
|---|---|---|
| `main/CMakeLists.txt` | `project(Pecia VERSION x.y.z ...)` | **唯一权威来源**。`PECIA_VERSION` 宏由此注入（见同文件 `target_compile_definitions`），关于页与更新检查自动跟随 |
| `main/README.md` + `main/docs/README/README.<lang>.md` ×15 | 下载链接与版本表述：`releases/download/<ver>/Pecia_x64_<ver>.zip`（Gitee + GitHub 各一条） | 16 份文件**必须同步**（根与 docs/README 内各 2 处下载行） |
| `main/example/ex1.markdown/ex1.md` | 版本表 `Pecia \| x.y.z` | 示例文档中的版本引用 |
| `main/HISTORY.md` | 在文件**顶部**追加新版本小节（日期 + 新增/修复/变更） | **增量原则：只追加，旧内容一律保持原样**，不修改不删除 |

> **Windows 文件属性（VERSIONINFO）自动跟随，不算手工同步项**：三份 exe 的
> 「属性 → 详细信息」版本由 `main/build/version_info.rc.in` 在 CMake 配置阶段
> 经 `configure_file` 注入 `PROJECT_VERSION` 生成（见 CMakeLists 的
> 「Windows VERSIONINFO resources」段）。改完 CMakeLists 里的版本号重新构建即可，
> **不要手工改任何 `.rc`**。

改完用 `grep -rn "旧版本号" main/` 复核零残留（注意排除 `.thirdparty/`）。

## 二、发行前质量门禁（全部通过才可发行）

1. **全量构建**：双击 `build/full.bat`（或 `5_build_pecia.bat`）；
2. **26 个 ctest 全绿**（构建链内置，红灯即中止）；
3. **文档门禁**：`build/test_docs.exe` 输出 `168 checks, 0 failures`
   （改过任何文档/API/目录后必跑）；
4. **零 C 警告**（`/W4 /WX` 级别，构建日志无 `warning C`）；
5. **压力测试**：按 `docs/STRESS_TEST_PLAN.md` 跑完 A–H 用例并填写记录表，
   重点确认大文档（`example/ex3.LargeMarkdown/` 3 档）与多标签场景；
6. **文件属性校验**：`build/Pecia.exe`、`PeciaLua.exe`、`PeciaAIChat.exe`
   右键 → 属性 → 详细信息，`文件版本 / 产品版本` 均等于 `CMakeLists.txt`
   的版本号（由 `build/version_info.rc.in` 自动生成，三份 exe 应一致）。

## 三、打包（手动）

本仓库**不提供打包脚本**，打包由发布者手动完成：

- 把项目根 `build/` 里的产物复制到 `release/<版本>/`，再压成
  **`Pecia_x64_<版本>.zip`**；
- zip 内容：`exe 三件套（Pecia / PeciaLua / PeciaAIChat）+ lang/ + theme/ +
  script/ + LICENSE + THIRD-PARTY-NOTICES.md + licenses/`。
  `build/` 里还有 `test_*.exe`，那是构建门禁产物，**不要放进发布包**；
- ⚠️ **zip 文件名不可改**：清单里的下载地址由 `gen_update_json.py` 按
  `releases/download/<tag>/Pecia_x64_<tag>.zip` 拼出（程序只读清单，不自己拼名），
  名字对不上清单就指向 404。

## 四、gitee 发行（自动更新的生效点）

1. 在 gitee 与 GitHub 两个仓库创建 release，**tag 名 = 版本号**（如 `1.0.1`），
   上传第三节的 `Pecia_x64_<版本>.zip` 作为附件（附件名不可改，下载地址与 §三 同源）；
2. 先在 `docs/history/en.md` 顶部补上这个版本的英文小节（标题写成
   `## <版本> (<日期>)`）——更新页的英文说明就是从这里取的，缺小节生成器会报错，
   不会悄悄退回中文；
3. 生成更新清单：`python build/gen_update_json.py` → 写 `docs/update.json`。
   版本取 CMakeLists、中文说明取 HISTORY.md、英文说明取 `docs/history/en.md`、
   下载地址按 §三 的命名规则拼出；**清单里没有哈希**，所以不需要把 zip 交给生成器，
   手动打的包也能直接写清单；
4. **顺序要求**：先把两个 release 的附件传完，**再**推 `docs/update.json`。
   反过来推，用户在清单生效、附件还没上传的窗口里点"检查更新"就是 404；
5. 推完跑 `python build/gen_update_json.py --verify-remote`：两个镜像各下一遍，
   既确认清单里的两个地址都能取到这个包，也逐字节比对两仓内容。清单里没有哈希，
   这一步就是唯一的互校；不一致说明两仓传的不是同一个文件，而客户端不会替你发现；
6. 清单文件**不要进发布包**（包里是 exe 三件套 + lang/theme/script + 许可，见 §三）：
   程序运行时的缓存放 exe 同级的 `update.cache.json`，名字刻意不同，
   robocopy 覆盖安装目录时不会把出厂清单盖回用户机器上；
7. **自动更新验证要用旧版本 exe 点"检查更新"**——远端产物会覆盖被测 exe（本机实测教训）。

## 五、发行后验证

1. 下载链接抽查：README（根 + 至少一个语言版）里的链接能下到新 zip；
2. 自动更新链路：旧版本启动 → 帮助 > 检查更新 打开更新页 → 显示新版本与说明 →
   下载 → 进度走到 100% 并解压（清单不含哈希，包完整性只剩 zip 自身结构 + bat 里
   「解压后 `Pecia.exe` 必须存在」这一道兜底）→ 覆盖（`robocopy /e /is /it`：同名覆盖、
   多余文件保留）→ 重启后关于页版本号正确；
   失败路径也要看一眼：把 `update_url_gitee` 改成一个不存在的路径，页面状态行应报
   「联网检查失败」，而不是谎报「已是最新版本」。原因（HTTP 404 / 超时）留在
   `UpdateCheckResult::error` 里，更新页既不显示也不落日志——短文案是刻意的，代价就是
   发布侧排错要自己加打印；
3. settings.ini 兼容性：新版本首启后，旧配置键保留、新增键补默认值、
   用户数据（`ai_api_key`、最近文件等）无损。

## 六、职责边界

- `git commit / push` 未经用户下令不执行；
- release 的创建与附件上传需用户登录 gitee 操作（或明示授权后进行）。

---

*本文档由 2026-09-17 版本 1.0.0 → 1.0.1 升级实践整理而成。*
