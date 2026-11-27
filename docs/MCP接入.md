# Pecia MCP 接入（For users & AI agents）

Pecia 支持把自身作为 MCP 服务器，让外部 AI 客户端（Qoder、Claude Desktop、
WorkBuddy 等）直接驱动编辑器：读文档、改文本、跑内置 Lua 脚本（含 PCRE2 正则库
`rex.*`）、查大纲与设置。窗口照常可用——AI 改的就是你眼前这份文档。

## 1. 配置

在客户端的 MCP 配置里添加（路径换成你的 Pecia 安装目录）：

```json
{
  "mcpServers": {
    "pecia": {
      "command": "<安装目录>\\Pecia.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

- Qoder：个人设置 → MCP 服务，或项目 `.qoder/config/mcp.json`。
- Claude Desktop：`claude_desktop_config.json` 的 `mcpServers` 段。
- 只有 stdio 一种传输；协议版本支持 2025-03-26 / 2025-06-18 / 2025-11-25。

## 2. 行为边界（先读这段，能省掉 90% 的疑问）

- **拉起即开窗**：客户端会启动一个带窗口的 Pecia 实例。若你已手动开着 Pecia，
  会同时存在两个窗口（各自独立，共用同一个 settings.ini）。MCP 用哪个就只开哪个。
- **编辑锁优先**：「仅预览」模式或只读文件下，所有写入/执行类工具统一返回
  `perm_denied`——这不是故障，是保护。切回编辑模式即可。**没有打开任何文档时**
  写入类工具返回 `not_found`（无文档可保护，原因不该被说成锁）。
- **写入只作用于当前标签页**；跨标签批量操作请逐个 `tab.switch`。
- **断开不断窗**：客户端退出后 Pecia 窗口继续正常使用。
- **选区要自己给**：内置脚本大多只处理选区，而 agent 没有鼠标——`lua.run` /
  `script.run` 接受 `sel_start` / `sel_end`（文档内字节偏移）显式圈定选区，
  `input` 与 `editor:get_selection()` 就会是那段文字。
- **一次一个请求**：stdio 端点串行处理工具调用。任何模态框（脚本参数弹窗、
  `win:input` 等）开着的时候，端点读不到下一个请求，直到它被关闭——这也是
  MCP 侧运行脚本默认 headless 的第二个理由。
- **运行脚本默认 headless**：`lua.run` / `script.run` 触发时，`win:message` /
  `win:question` / `win:input` / `win:select` **不弹窗**，而是把提示写进返回的
  `output`（question 用你预设的 `answer`，input/select 回 `nil`）。这是刻意的：
  脚本同步跑在 UI 线程上，一个模态框就会把宿主和客户端一起卡死。
- **死循环有预算**：`timeout_ms`（默认 3000，上限 5000）折算成 Lua 指令数**和**一道墙钟
  截止时间，超限由 `lua_sethook` 掐断并回 `timeout` 类错误，文档不受影响。指令预算由
  这道截止时间统一推导，所以脚本栏与 MCP 侧拿到的是同一份额度。
- Lua 环境**非沙箱**（与脚本栏同一口径）：脚本可访问文件与系统，请只接入你信任的
  模型与提示词。MCP 侧**不提供**把脚本写进 `script/` 目录的通道——`script.save`
  只能保存那份草稿（`console.get/set` 读写它），且路径被限制在脚本根目录内。

## 3. 工具一览

| 组 | 工具 | 说明 |
|---|---|---|
| 读 | `doc.get_text` `doc.info` `doc.api` `outline.list` `find.all` `script.list` `tab.list` `doc.reveal` | 文本/状态/文档/大纲/检索 |
| 写 | `doc.apply_edit` `doc.replace_all` `doc.save` `doc.open` `doc.new` `tab.switch` | 锚点定位=唯一命中才动；失败即未改，可放心重试 |
| 执行 | `lua.validate` `lua.run` `script.run` | 先验后跑；跑在文档快照上，出错即什么都没改 |
| 草稿 | `console.get` `console.set` `console.show` `script.save` | 读写那份草稿（**已无控制台窗口**，m037）；`console.show` 把它开成普通编辑器标签给人看 |
| 内置对话 | `ai.send` `ai.get` | 喂 Pecia 自己的 AI 对话面板（主窗口独立一栏；用的是编辑器里配置的端点，与 MCP 无关） |
| 界面量测 | `ui.windows` `ui.inspect` `ui.pixel` `ui.send` `ui.wait` | 脚本弹出的界面（参数框、输入框、选择框）可看、可点、可取色 |

`tools/list` 的清单与 `doc.api("lua_api.txt")`、`pecia.ops()` 三者同源——宿主只在
一处定义能力，看到的即生效的。

## 4. 给 AI 的建议闭环

**改文本**

1. 先 `doc.api("lua_api.txt")` 读 API 文档，再动手。
2. 简单替换用 `doc.apply_edit`（带 `anchor` 时更安全：锚点不唯一会直接报错而不是
   改错地方）；批量/正则/循环写成脚本。
3. 每次写入后 `doc.get_text` 或 `doc.info` 复验；`isError` 的结果表示**什么都没改**，
   修正参数后重调即可。
   ⚠ 唯一例外是 JSON-RPC 层的 `-32603`（internal）：那表示**这次调用没能完成**
   （宿主 UI 线程超时等），**不代表什么都没改**——请求可能仍会在稍后生效。收到
   `-32603` 不要盲目重试，先读一次文档状态再决定。

**写脚本并验证**（这一串就是 `lua.validate → lua.run → 读错 → 改` 的闭环）

1. `lua.validate(code)`：只编译、不执行、零副作用。返回 `{ok, error, line}`——
   `error` 是 Lua 原话，`line` 是行号。**没验过就不要 run**。
2. `lua.run(code)`：对当前文档执行。返回
   `{verdict: "pass"|"error", output, result, error, line, truncated, applied}`；
   `applied=true` 说明文档真的变了（一次撤销即可回退）。
3. 报错就按 `line` 改代码重验，不要盲目重试同一份代码。
4. 已安装的脚本用 `script.run(name, params)` 跑：`name` 取 `script.list` 给出的
   相对路径（模型看不到磁盘路径），`params` 直接喂 `--!param` 的值，缺必填项会回
   `bad_arg` 而不是弹窗。
5. 需要让用户看见/复用时 `console.set` 写草稿，`console.show` 开成标签给人过目，再 `script.save` 落盘。
6. 界面量测（`ui.windows` / `ui.inspect` / `ui.send` / `ui.pixel` / `ui.wait`）
   是给**自动化门禁**用的：它们读的是宿主自己登记的控件树。
   ⚠ 实测约束：同一条 stdio 连接**一次只能处理一个请求**——只要有任何模态框
   还开着，端点就读不到第二个请求（`win:input` 弹着时 `ui.windows` 直接超时）。
   因此不要指望"AI 一边等模态框一边注入"：需要脚本按某个输入运行，就用
   `script.run` + `params`（或让 headless 下 `win:input` 回 nil 走默认分支）；
   界面本身的回归交给 Pecia 的门禁（`test_script_dialog_measurement`）。

## 5. 排错

- 客户端报 connection closed：确认 command 路径指向 Pecia.exe 本体且带 `--mcp-stdio`。
- 工具全部 `perm_denied`：菜单 Markdown > 仅预览 关掉，或文件属性去掉只读。
- 想让 AI 只读不写：给它挂这个服务器时，在会话里保持仅预览模式即可（写权限由
  编辑器实时状态决定，不由 MCP 配置决定）。

---

# English summary

Pecia embeds an MCP stdio server: add `<install dir>\Pecia.exe --mcp-stdio` to your
client's `mcpServers`. The client launches a visible Pecia window; tools read/edit the
active tab only. All write/exec tools honor the editor's live read-only/preview lock
(`perm_denied` = protection, not failure; with no document open they answer `not_found`
instead), and every failing tool result guarantees nothing was applied.

The Lua loop is first-class: `lua.validate` compiles a script without running it,
`lua.run` executes it against a scratch copy of the document (a failed run changes
nothing, one undo reverts a successful one), and `script.run` runs an installed script
with its `--!param` values passed in. Runs are headless by default — `win:*` dialogs are
captured into `output` instead of blocking the UI thread — and a runaway loop is cut off
by an instruction budget rather than hanging the editor. Drafts live as a plain
string now - the Lua console window was deleted in m037: `console.set` writes it,
`console.show` opens it as an ordinary editor tab, and `script.save` turns it
into a script file.

When a script opens a dialog, that UI is measurable: `ui.windows` / `ui.inspect` return
the real widget tree (FLTK self-draws, so an external tree walk would see nothing),
`ui.send` synthesizes clicks/typing, `ui.pixel` reads colors off-screen, and `ui.wait`
blocks until a dialog appears or vanishes. Tool list, `lua_api.txt` and `pecia.ops()` are
generated from one host registry — what you see is what runs. Lua is intentionally not
sandboxed; connect only agents you trust.
