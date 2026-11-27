# Pecia changelog (English)

> Translated version history. The Chinese source of truth stays `main/HISTORY.md`
> and is **not** duplicated here; this folder carries translations only, one
> section per release, newest first.
>
> `build/gen_update_json.py` reads the section whose heading matches the version
> being published and puts it into the `notes_en` field of `docs/update.json`, so
> the update page can show release notes in the user's language. A missing section
> is a hard error, not a silent fallback to Chinese.
>
> Add a file per language you are willing to maintain (`<code>.md`, same codes as
> `lang/`). Languages without a file fall back to the Chinese `notes`.

---

## 1.2.0 (2026-11-26)

**Added**

- **MCP can now write and verify Lua, not just edit text**: new host operations
  `lua.validate` (compile only — returns the line number and Lua's own words),
  `lua.run` (executes against a snapshot of the document, so a failed run changes
  nothing), `script.run` (runs an installed script by name, with `--!param` values
  passed in — a missing required parameter answers `bad_arg` instead of opening a
  dialog), `console.get` / `console.set` (read and write the Lua console draft),
  `script.save` (turn the draft into `script/<group>/<name>.lua` and refresh the
  script bar, paths confined to the script root) and `ai.send` / `ai.get`.
  The full "write → validate → run → read the error → fix → re-check" loop is now
  reachable from MCP; previously `initialize.instructions` promised a `run_lua`
  tool that did not exist.
- **Script UI is measurable**: `ui.windows` / `ui.inspect` / `ui.pixel` /
  `ui.send` / `ui.wait`. FLTK draws its own controls, so an external tree walk
  sees a single HWND — the widget tree is published by the host itself
  (`DialogBase::enableInspection`: whoever builds UI registers it), covering the
  parameter dialog and script dialogs. `ui.pixel` captures off-screen via
  `PrintWindow`, so the window need not be in front.
- **Two ready-made script UI components**: `win:input(prompt, default)` and
  `win:select(prompt, {items}, pre)` — Pecia-themed dialogs rather than Win32
  boxes, which is what makes them visible to the tools above.
- **Execution guardrails**: instruction budget (`timeout_ms`, default 3000,
  capped at 5000, enforced with `lua_sethook`), separate caps for `print` output
  and the return value (32 KB default, 64 KB max, with a `truncated` flag), and
  nested-run rejection when a script re-enters the engine.
- **Explicit selection for `lua.run` / `script.run`** (`sel_start` / `sel_end`):
  most built-in scripts only act on the selected text, and an agent has no mouse —
  until now such scripts could not be driven over MCP at all.

**Changed**

- **The Lua console window is deleted (`LuaTool/`).** Writing, validating and running
  scripts already closes over MCP, so the window only displayed the same data twice. The
  draft is now a string on the main window, still persisted in `script.last.txt` next to
  the exe (same format, so an existing draft survives the upgrade); `console.get/set`
  read and write it, and `console.show` no longer opens a window - it opens the draft as
  an ordinary editor tab. Gone with it: the Tools > Lua Script... menu entry, the console
  buttons and their shortcut bindings (`win.button.toai`, `win.button.saveas`), the
  `lua_win_w` / `lua_win_h` size keys, and 22 language keys only the console used (all 16
  files updated). The script bar is now the only human entry point for running scripts.
- **One executable ships now.** `PeciaLua.exe` and `PeciaAIChat.exe` became
  in-process windows of `Pecia.exe`; the cross-process pipe layer
  (`PipeProtocol` / `PipeClient` / `PipeSecurity` / `LuaPipeServer`), the tool
  launcher, the two single-instance mutexes, their resource scripts and icons and
  the updater branches handling them are gone. The reason is visibility: an agent
  can only test what lives in the same process, so the console buffer and script
  dialogs could only become inspectable once they ran inside Pecia.exe.
- **The AI chat moved from its own window into a column of the main window.**
  `AIChat/AIChatWindow.{h,cpp}` became `AIChat/AIChatPane.{h,cpp}` (an `Fl_Group`
  child), and the `DialogBase` shell went with it: title bar, pin, minimize and the
  `aichat_win_w` / `aichat_win_h` size keys. That column is **independent of the
  Markdown preview**: `editor | preview | AI chat` can all be up at once, opening one
  never closes the other, and each seam has its own draggable bar. One function
  (`columnWidths()`) decides widths: the chat keeps at least 320px because its button
  row cannot re-flow, the preview at least 160px, and when both floors do not fit the
  editor yields first - down to its own 20% floor. The request timer reads in the
  **status line** (`AI 7s`) instead of a title the pane no longer has. `Tools > AI Chat`
  toggles this column; `ai.send` became **idempotent open** - it used to call the same
  toggle, so a second message closed the pane. The pane only claims its shortcuts while
  it holds the keyboard, so Ctrl+Enter in the editor is not stolen. The wheel belongs
  to the column under the pointer: FLTK asks every child in turn, so without a guard a
  wheel over the chat scrolled the preview too (the gate measured that).
- **Settings became a vertical page switcher, and the API fields moved into
  Options.** `AIChat/AIChatSettingsDialog.{h,cpp}` is deleted: `ai_endpoint`,
  `ai_model` and `ai_api_key` had two writers (the chat window had its own dialog),
  and Options had none. Options is now four pages - `Editing / Save & Exit /
  Interface & System / AI` - with a self-drawn vertical switcher
  (`ui/SettingsNav.{h,cpp}`) on the left, using the main window's tab vocabulary:
  flush to the window edge and full height (no white band under the title bar),
  the menu bar's own `background3`, a 4px air gap between rows instead of a drawn
  line, the active cell filled edge to edge with highlight1, and `text1` for every
  label - inactive rows in the hint tone would read as disabled. The whole-window
  scrollbar is gone and the height comes from the tallest page (6 rows), not from
  the 480 callers used to pass. Controls are built once and a page switch only
  shows/hides them, so `loadFrom`/`saveTo` did not change and no value can be lost
  on a switch. Widening the dialog exposed two paths that had never been taken, and
  both crashed when measured: (1) `minW` now exceeds the requested width, so the
  `size()` call that sits *before* `initShell()` hit the window shadow that does not
  exist yet - `DialogBase::resize` is meant to run after initShell, which is what
  ParamDialog already does; (2) `fl_width()` called before `fl_font()` makes FLTK
  look up font size 0, `fl_find_font` answers -1 and `fc_face` reads `fonts[-1]`.
- **Three fixes on the AI page (m041).** (1) The Options fields are square now:
  the project's own convention is `FL_BORDER_BOX` (the main window uses
  `ThemedInput`, the extensions dialog spelled it by hand), and only the four
  Options fields were bare `BlinkInput` with FLTK's rounded box. The rule moved
  out of `ThemedInput` into `applyInputTone(Fl_Input*, tc)` so the integer field
  reads the same one rule. (2) The `GLM api` button and its `ai.glmapi` key are
  deleted - a page whose title and fields name no vendor should not link to one.
  (3) The field is labelled `API URL`, not a bare `URL` (16 language files, value
  only; the machine key `ai.endpoint` is unchanged). (4) The factory endpoint and
  model stay preset: deleting them would make a new user type a 55-character URL
  by hand, which this project has already ruled out. What was wrong is that the
  same literal lived in ten places (Config.h default arguments, Config.cpp
  fallbacks, setters, seeding, and the chat pane's call site); it is now
  `Config::kDefaultAiEndpoint` / `kDefaultAiModel`. (5) With the URL cleared the
  chat pane no longer says "generation failed - check the key/network" but the
  new `ai.notconfigured` status. Measured along the way: `setAiEndpoint("")`
  really stores an empty string (the `url ? url : default` guard only catches
  nullptr), so the belief that clearing re-fills the default was wrong - and it
  was not changed into re-filling either, because a field that grows back is
  worse than an honest status line.
- **One run path**: the console's Run button and the bottom script bar call the
  same execution function with the same parameter dialog and the same single-undo
  semantics (previously two engines ran two paths).
- **Theme / language / shortcut updates are direct same-process calls** — the
  main window refreshes the docked pane immediately, replacing the cross-process
  settings.ini watcher.
- **Denials name the right reason**: with no document open, write/exec tools now
  answer `not_found` instead of `perm_denied` (there is nothing to lock).
- **The Tools menu dissolves; the View menu is reorganized by meaning.** Tools
  held only the AI Chat entry, so the whole menu is gone and AI Chat moves into
  View as a pane toggle. The View toggles are now two semantic groups - **region**
  (menu bar / script bar / status bar / side panel / AI chat, ordered to mirror
  their on-screen position: top bars top-down, panes left-right) and
  **presentation** (line numbers / highlight current line / space symbols /
  word wrap), separated by dividers.
- **The AI chat button row gets a backdrop** in the same color parameter the
  status bar uses (`background2`); before, the row's only backdrop was the pane
  color, which read as "no background".

**Changed**

- **Script runs now report to the human.** The script bar and the menu used to take
  `out` and `err` from the engine and throw both away, so the only clue was whether the
  document happened to change. The result now lands in the status line as a short verb
  plus the **wall-clock cost of the run** (`Script done (12 ms)`, `Script error (3 ms)`),
  failures put the full text in an info window, and switching tabs clears it so one
  document never shows another's result.
- **The user guide ships in two languages, picked by UI language.**
  `docs/使用说明.html` splits into `使用说明.zh-CN.html` and `使用说明.en.html`
  (the English edition is a full translation, including the copyable AI prompt).
  Help > User Guide opens the edition matching the interface language - Chinese
  UIs read the zh-CN edition, everything else English; a missing localized file
  falls back to English, and "file missing" vs "no .html association on this
  system" are reported separately.
- **AI chat context depth is configurable** (Settings > Options > AI): how much
  conversation history rides along with each message was hard-wired at 25 turns,
  now 1-100 with the oldest dropped beyond the limit. The setter and the getter
  both clamp, so even a hand-edited settings.ini cannot push an unbounded message
  list into the request builder; the default is unchanged.

**Fixed**

- The Shortcuts dialog no longer offers actions with nothing behind them:
  `win.button.todoc` and `win.button.settings` (the chat buttons removed this
  round) and `win.button.help` (the console's help button died in the previous
  round, the table row stayed) are gone from the action table, together with
  their labels and any leftover bindings in settings.ini.

- `initialize.instructions` advertised a non-existent `run_lua` tool, so any
  client that trusted it would build a failing call.
- The updater's process cleanup still listed the two tool executables that no
  longer exist.
- **The preview panel can no longer outlive its window.** `PreviewPanel`'s view
  is a *child widget* of the main window while the panel arms its own 30 ms pump
  timer: destroying a window that still had the preview open let FLTK free the
  widget but leave the panel armed, so the next tick called `damage()` on freed
  memory and the fault surfaced in whichever event loop ran later. With one
  window per process this is nearly invisible; the same-process test gate hit it
  in about a third of runs (`0xC0000005` and `0xC0000409`). The destructor now
  uses the same `shutdownPreview()` as the deferred close (detach, then delete),
  and `test_preview_panel_dies_with_window` guards it — removing the fix makes
  that gate crash 5/5.
- **A `settings.ini` change from another process can no longer reach a freed
  window.** The watcher marshals its callback with `Fl::awake(handler, config)`,
  and FLTK 1.4.5 has no `Fl::remove_awake`, so an event already queued when the
  Config died was still delivered. Each message now carries a shared alive token
  that `~Config` drops, making the stale event a no-op (the same guard
  `UpdateDialog` uses).
- **`doc.api` no longer answers `not_found` for everything.** The handler looked
  only next to the executable, while `lua_api.txt` ships under `script/`
  (`docs/RELEASE_CHECKLIST.md` §三) - and the MCP instructions make reading it
  the very first step of the loop. The install layout (`script/`, then `docs/`,
  then the exe directory) is searched now; the bare-name and traversal guards
  are unchanged.
- **`doc.get_text` returns the buffer byte-for-byte.** The old loop split at
  `'\n'` and never appended it, so LF documents came back as one run-on line and
  CRLF documents kept only bare `'\r'`. Because `doc.apply_edit` matches its
  anchor by bytes, any anchor built from what the agent read was guaranteed to
  miss. Three assertions that had pinned the old behaviour as expected are
  corrected, and a CRLF fixture was added - it turns the gate red five times
  against the old code.
- **`serverInfo.version` reports the build's own version again.** It was the
  literal `1.1.3`; it now comes from `PECIA_VERSION`, the same macro the About
  page and the updater read, and the two ignored `appName`/`appVersion`
  parameters of `runStdioLoop` are gone. A selftest nail asserts the handshake
  version equals `PECIA_VERSION` (restoring the literal makes it fail).
- **A marshalled call can no longer write into a dead stack.** `dispatch` kept
  its `RunPacket` on its own frame and passed a pointer to the caller's argument
  map; after the 30 s timeout returned, a late UI-thread tick wrote that frame -
  and in the worker-thread case the stack was already unmapped. The packet is
  now heap-allocated with a reference on each side, owns a copy of its
  arguments, and re-resolves the operation on the UI thread, so a host that
  unregistered in the meantime answers `not_found` instead of running a closure
  over a destroyed window. `test_late_tick_after_timeout` segfaults on the old
  shape.
- **The MCP thread entry now catches** (project rule 6): an escaping exception
  used to mean fastfail `0xC0000409` with no dump. Per-request failures are
  logged and answered with `-32603` so the connection survives; the outer catch
  only logs. There is currently no reachable throw, so this item ships **without
  a red proof** - it is the mandated boundary, not a measured fix.
- **`win:message` / `win:question` use `MessageBoxW`.** The non-headless branch
  handed UTF-8 text to the ANSI API, so Chinese script dialogs were guaranteed
  mojibake outside the GBK code page; every other native box in the project
  already goes through `widen` + `MessageBoxW`. A new `test_docs` lint forbids
  `MessageBoxA(` calls in first-party `.cpp` files (explaining the rule in a
  comment is not a violation).
- **A deferred menu script run can no longer fire on a destroyed window.**
  `DeferredRunScript.self` was a raw `MainWindow*`, and `~MainWindow` itself
  noted that `remove_timeout` cannot revoke a timer whose data is a heap struct.
  The window now owns a liveness token, the queued struct copies it, and the
  callback checks it before executing (plus the rule-6 catch). The gate has two
  halves: one proves a live window's deferred run really happens, the other is
  the crash guard - measured **3/10 segfaults** with the token check removed
  (1/10 before the heap-reuse step), 0/10 with the fix.
- **`WidgetRegistry` stopped claiming protection it does not have.** Its header
  said a vanished window is caught by "the alive check in snapshot()", but the
  code only filtered null entries while `find`/`findByTitle` dereferenced
  blindly. FLTK 1.4.5 exposes no window-chain walk, so a real liveness test is
  not implementable: the comment now states the actual invariant, and a new
  `test_docs` lint enforces it by allowing `WidgetRegistry::instance().add(`
  only in `ui/DialogBase.cpp`, the one place that also unregisters.
- **Paste-media skip reasons land on the status line.** The feature shipped in
  1.1.2 with zero test coverage and silent skip paths - an unsaved document, a
  read-only document or a failed write made Ctrl+V do nothing, which users could
  only read as broken. A real-clipboard probe proved the core logic was fine
  (files and bitmaps land a copy plus a link, text is handed back), so the bug
  was the UX. Skip reasons now travel through `Editor::setPasteNotice` to the
  status line ("Paste media: save the document first / document is read-only /
  write failed", 16 languages); handing text back and an empty clipboard stay
  silent. New `test_paste_media` (the 36th ctest target) drives the REAL
  clipboard end to end: file/bitmap land a copy plus a link, text wins with no
  notice, and unsaved/locked documents answer with the right key.
- **The lost shipped-docs copy rule is back, and guarded.** The m021 delivery -
  copy the user guide and the MCP onboarding doc into `build/docs` at build time -
  was silently dropped when a CMakeLists rebuild lost part of the file, and every
  later gate stayed green because no gate watched that promise: Help > User Guide
  would have opened nothing. The `pecia_copy_docs` ALL target is restored (it runs
  even without a relink), and a `test_docs` lint pins the rule's existence, both
  files on disk, and the removal of the old single-file handbook.
- **Two new fences against runaway scripts.** The `lua_sethook` instruction
  budget never fires inside a C function (PCRE2 backtracking), so the documented
  `timeout_ms` cap (5000 ms) was hollowed out by instruction-based conversion.
  The hook now also enforces a wall-clock deadline (5000 ms), and PCRE2 matches
  get an explicit match limit (10M default down to 1M - a single pathological
  match caps at roughly 100-200 ms). The known boundary is stated honestly: Lua
  has no preemption, so one non-regex C call cannot be interrupted. Red proof:
  pathological regex x20 measured 3089 ms on the old shape (threshold 1600, red)
  and 292 ms after the fix.
- **The write gate is evaluated on the UI thread.** The gate closure walks the
  tab list and reads document editability - state the UI thread mutates without
  a lock - but used to run on the MCP stdio caller's thread, an unlocked data
  race (the old comment claimed the caller thread prevents queue races, which
  was backwards). Evaluation moved into `applyPacket`; the fail-safe semantics
  are unchanged. A gate asserts the verdict happens on the executor thread -
  the old implementation fails it deterministically, twice.
- **`doc.open` no longer spawns a process.** Opening a file in single-tab mode
  while the tab was occupied went through `spawnWindowWithFile()`, i.e.
  `CreateProcessW(current exe)`. In the product that means "the MCP call
  reports success while the file never opens in the connected window"; in a
  test binary the "current exe" is the test itself, so the process **copied
  itself** (12 consecutive runs all timed out past 25 s, and one self-spawned
  process was left behind). `doc.open` / `doc.new` / `doc.save` now always open
  in the current window, **never spawn a process and never open a modal**; a
  document with unsaved changes is refused explicitly (`unsaved_changes`)
  instead of being dropped silently or opened elsewhere. `spawnWindowWithFile`
  also got a guard: only an executable whose base name starts with "Pecia" may
  spawn a copy, so no non-product binary can multiply itself again. Control
  experiment: the old 1.1.3 tree passes the same gate 12/12, the new one 0/12
  before the fix.
- **The script dialog's selection list was invisible and unclickable.** The
  item buttons of `win:select` were placed in **group-relative** coordinates,
  while FLTK draws and hit-tests children by their **window** coordinates - so
  of 12 items, 0/12 sat fully inside the viewport (every one clipped 16 px on
  the left, the first three entirely above it). Window-absolute coordinates
  put 7/12 fully visible with the first row in view; the scrolled content
  height is now computed from the item count instead of a hard-coded
  `listH * 4`, which also made the last row reachable.
- **`rex.gsub` copied the wrong byte on a zero-width match and read past the
  heap.** The zero-width branch appended the *next* character instead of the
  one under the match (`gsub('ab','(?=b)','-')` produced `a-a-b`, not `a-b`),
  and the tail was off by one, carrying a malloc block header out with it (100
  `Z`s measured 203 bytes, tail `\x00\x01`). The branch now appends the byte at
  the match position, scans forward by one and clamps the tail to the real
  remaining length - byte-identical to native `string.gsub`.
- **`split('')` and `gmatch('')` used to loop forever.** On an empty subject
  the old guard let the scan stand still, so both APIs hung (killed after
  5-6 s). The scan now always advances and an empty subject yields an empty
  table.
- **`debug.sethook()` can no longer disarm the run budget.** The hook carries
  both the instruction budget and the wall-clock deadline, so one
  `debug.sethook()` took both defences away (`while true` hung). The `debug`
  global and `package.loaded["debug"]` are now removed, so `require("debug")`
  cannot bring it back either.
- **`timeout_ms` is now also a wall-clock deadline.** Only the instruction
  conversion existed, so the script bar and the MCP side got budgets 12x apart
  for the same span of time. One deadline now derives the instruction budget,
  so both paths are equal (measured: both cut off at 13 ms).
- **A dangling read of `def->perm` in the registry.** `dispatch` read
  `def->perm` after releasing the registry lock, while the entry may already
  have been freed by `removeAll()` - the garbage value could clear
  `needsGate` and let a Write operation **skip the write gate**. The
  permission is now copied inside the lock and nothing owned by the registry
  is dereferenced afterwards; entries are retired rather than freed, so a
  pointer already handed out stays valid.
- **An operation that forgets its permission class is no longer treated as
  Read.** `OpDef::perm` defaulted to Read - the fail-open direction - so an op
  that forgot to declare one was registered and **bypassed the write gate**.
  The default is now `Unset` and `add()` rejects it; the probe shows the old
  shape letting an undeclared write through, the new one refusing to register.
- **The registry is scoped by owner; a name is no longer globally unique.** A
  second window registering the same op names used to be rejected as a
  duplicate, so **the new window owned no host operations at all** and every
  dispatch went back to the first window (measured in `mainwindow_smoke`: 16
  assertions red - the document read, the file opened and the tab list all
  belonged to the other window). The same name from a **different owner** is
  now allowed to coexist, the **newest live owner serves it**, and when it
  goes away the previous one takes over again; `find()` / `snapshot()` still
  expose **one entry per name** (the newest live owner's), so the projections
  keep their shape. The write gate now carries a liveness token: a dead
  window's gate fails safe (denies writes) instead of dereferencing freed
  memory.
- **`console.show` opens a new tab.** The draft lands in a new tab and the
  current document is neither replaced nor asked about - "show me a script"
  is not a licence to throw away unsaved work, and a modal box would block
  the whole UI thread (on the agent path the MCP call simply times out).
- **`rex.find`'s `to` is now exclusive of the end point.** `lua_api.txt` has
  always documented the exclusive semantics - the implementation had not
  caught up with the docs. Implementation and docs now agree (this is a
  **silent change of a public return value**, so scripts relying on the old
  inclusive behaviour must check themselves). The 92 built-in scripts are
  unaffected (only `12_regex_replace.lua` uses `rex.gsub`).
- **`WidgetRegistry` has a real liveness check now.** The registry handed out
  raw `Fl_Window*` with no liveness check, so a window that never
  deregistered left a dangling pointer behind (while the header claimed an
  alive check existed). Registration now parks a FLTK death sentinel in the
  window's `user_data` slot (`AUTO_DELETE_USER_DATA`, so window destruction
  always fires it) and every read path drops the dead entries first - measured:
  deleting a window without deregistering makes `find()` report it gone
  immediately, with no dangling pointer left in the table.
- **`ui.wait` no longer re-enters other calls.** It pumps the event loop with
  `Fl::wait()` inside a dispatch handler, so queued packets were executed
  **nested** inside it (a second handler could re-enter the script engine or
  start another `ui.wait`). UI delivery now goes through one queue and one
  drain, and the drain refuses to start while a handler is running - a nested
  event pump can therefore never start a second packet.
- **Document read/write boundaries.** `doc.get_text` could overflow
  `start + length` and slip past the range clamp (huge allocation plus a
  leak, reported as `internal:` instead of `bad_arg:`); `doc.apply_edit`
  accepted a negative `start`, which wrapped around as `size_t` and threw
  `out_of_range`; `doc.replace_all` rewrote the buffer with `buf->text()`,
  and FLTK **clears the undo stack in `text()`** - the user's entire undo
  history was wiped while the comment called it "one grouped transaction".
  The three now clamp without overflow, reject a negative offset with
  `bad_arg`, and use `select()` + `replace_selection()` (undoable).
- **`ui.inspect` coordinates are consistent now.** It subtracted the dialog's
  **screen** position from the widget's window position, so x/y were garbage
  (measured -340). It now uses the widget's own `x()`/`y()`.
- **Clearing the OK shortcut in the settings no longer leaves the old binding
  alive.** `shortcutCore_resetDialogOkCache()` cleared the "loaded" flag but
  not the cached value, so the default `Ctrl+Enter` survived.
- **Pending configuration changes are no longer dropped silently.** When the
  direct-write fallback failed as well, `saveAll()` still ran
  `m_dirtyKeys.clear()`, so the next save overwrote the pending change with
  the values on disk.
- **A failed paste now says so.** The `paste.fail` language key existed but
  nothing referenced it: all four write-failure paths (directory creation,
  PNG write, copy, generated file name) failed silently.
- **Crashes no longer show two dialogs.** Besides the host's own crash
  handler, Windows raised an "Unknown Hard Error" box (the WER fallback) that
  covered the crash report the user should actually see. Both
  `CrashReport::install` and the crashing thread now set `SetErrorMode`, and
  the test binaries do the same - that popup during testing was exactly it.
- **The 16 language files are aligned again**: the `lua.cancelled` key was
  missing from all of them, and `chat.title` / `filedlg.lua` were orphan keys
  nothing referenced. All three are fixed and the 16 key sets match again.
- **The status line is no longer hardcoded and no longer stuck in one language.**
  `Script done 0` had two defects: the `0` was the **line count of the script's
  output** (neither a duration nor a state - nobody could guess what it meant),
  and the whole notice was **rendered once, in whatever language was active at
  that moment**, so switching the language left the old wording on screen. Now:
  (1) every *word* on the bar comes from the language file - the three
  hardcoded English bits (`Read-only`, `Size`, `AI`) are localized too (16
  files, new keys `status.readonly` / `status.size` / `status.ai_elapsed`);
  (2) the script notice keeps its i18n **key** instead of rendered text, so
  `updateStatusBar()` re-renders it in the current language on every repaint
  and a language switch takes effect at once (the paste-media skip reason
  travels the same channel and follows along); (3) the number is now the
  **wall-clock cost of the run**, measured inside `LuaEngine::run` (the script
  bar and the agent path report the same number), and the new keys
  `lua.ok_time` / `lua.fail_time` keep the old short forms as their **prefix**,
  so the meaning and the assertions are unchanged; (4) the bar is assembled as
  a string rather than through one fixed-size `snprintf` buffer, which used to
  silently cut the tail in languages that need more room - and the script
  notice lives in that tail.

---

## 1.1.3 (2026-11-02)

**Added**

- **Dedicated update page** (Help > Check for Updates): a source dropdown
  (Gitee / GitHub), a network check, a scrollable release-notes area and an
  inline progress bar replace the former two windows (decision dialog + separate
  progress window). Opening the page no longer goes online by itself - the first
  frame only reads `update.cache.json` next to the exe; only "Check online"
  triggers a fetch.
- **Manifest-driven update**: `core/Updater` now reads `docs/update.json`
  (version / published / url_gitee / url_github / notes / notes_en) instead of
  calling the Gitee Releases API. If the selected source fails the other one is
  tried, an empty URL is skipped, and only when both fail does it fall back to
  the local cache.
- **`build/gen_update_json.py`** generates `docs/update.json`: version from
  CMakeLists, Chinese notes from `HISTORY.md`, English notes from
  `docs/history/en.md`, download URLs built from the release-asset naming rule.
  New `docs/history/` holds translations only; a missing section is a hard error.
- **Configurable update source**: `settings.ini` gains `update_source` /
  `update_url_gitee` / `update_url_github`; the URLs can be edited or cleared
  (cleared = that source is disabled).
- **Markdown preview selection P2**: drag-select inside one preview block,
  line-by-line highlight, `Ctrl+C` / context-menu copy of the rendered text
  (read-only copy, no reverse mapping back to the source document).
- **New application icon**: `pecia.ico` now carries native 18-px frames; the
  title-bar icon went from 16 to 18 and follows `screen_scale`.

**Fixed**

- **The update script always deleted the package**: it ran `del` regardless of
  whether `robocopy` succeeded, leaving no way to retry; it now deletes only on
  success and keeps the package on failure.
- **HTTP 4xx stored as a package**: error pages used to be written to disk as a
  zip; the HTTP status is now reported directly.
- **Background results never repainted**: `Fl::awake` cannot wake a blocked
  `Fl::wait`; `Fl::lock()` was added to the three process entry points. Update
  checks/downloads, remote image fetches and preview rendering all shared that
  latent "result ready but UI stale" behaviour.
- **Title-bar icon too small / off-centre above 100% DPI**: it was drawn in
  fixed device pixels without `screen_scale`, and its size was hard-coded in
  three places (draw, tab draw, hit test); all three now share one constant.
- **Icon swap ignored by incremental builds**: replacing only the `.ico`
  without touching the `.rc` never reran `rc.exe`; the three `.rc` files now
  register their icon/manifest/QR resources as `OBJECT_DEPENDS`.
- **Enter in the find box jumped to "Replace with"**: the Enter passed through
  by `PassThruInput` fell into `Fl_Group` navigation; the find bar now catches
  it and runs "next". Go To (Ctrl+G) listened only for `FL_KEYDOWN`, which never
  arrives on Windows, and now handles `FL_KEYBOARD` too.
- **Find box flickered when the bar opened**: `activate()` showed/hid the
  placeholder based on "has initial text", contradicting the focus state; the
  placeholder is now owned solely by `updateHintVisibility()`.
- **Update page used the wrong download source / could not retry**: a check may
  have been answered by the fallback mirror while the download still used the
  selected (failing) URL; the picker now follows the source that answered, and
  a failed download leaves the Update button usable for a retry.
- **Update page exception guard**: failing to start the worker thread no longer
  throws into the FLTK event loop; the missing `update.page.exception` string
  was added to all 16 languages.
- **Glued paragraphs when copying from the preview**: preprocess zeroes the
  spacing between paragraphs and list items, so the old "gap > line height"
  block test never fired and copied text ran the paragraphs together. Block
  boundaries now come from block ids, and `<pre>` code keeps its indentation
  and runs of spaces.
- **Dangling timer when closing the preview**: destroying the preview view now
  cancels the pending debounced re-render, so a callback can no longer touch a
  freed view after closing the preview or switching tabs.
- **Two preview-only details**: the first entry into preview-only moves focus to
  the preview, and changing the base font size refreshes the preview scale.
- **Scroll-marker line cache**: the cache key now includes the word-wrap mode,
  so the marker no longer uses stale line numbers after toggling wrap.
- **A truncated download was reported as success**: the read loop ended
  silently when `WinHttpReadData` failed, so a half-downloaded zip went on to
  the updater. The reader now tells a normal EOF from a broken connection and
  checks the received size against `Content-Length`, deleting the partial file
  and reporting a failure on a mismatch.
- **The updater killed unrelated Pecia instances**: the batch used
  `taskkill /f /im Pecia.exe`, which hits every instance on the machine
  (including another install), with an unbounded retry loop. It now closes only
  `Pecia.exe` / `PeciaLua.exe` / `PeciaAIChat.exe` whose `ExecutablePath` is the
  current install directory, with at most 10 retries.

**Changed**

- The update status line is down to five short messages (checking / check ok /
  check failed / local manifest / update failed); details live in the "latest
  version" field and the button state.
- The update manifest drops `sha256` and package hashing: the manifest and the
  package come from the same source, so a hash cannot stop someone who can
  replace the package, yet it added a manual packaging step. Integrity is
  covered by the zip structure plus the "`Pecia.exe` must exist" check in the
  update batch. The generator also drops `--verify-remote`: GitHub is an
  automatic mirror of Gitee, so comparing two necessarily identical copies
  only wastes a download.
- Editor and preview context menus are generated by one shared routine; the
  preview side only greys out actions that would modify the source.
- The About page now shows "Project repository: Gitee | GitHub" instead of a
  single project link; each repository is its own clickable target.
- README download lines shortened to "Download x.y.z (Gitee)" across all 16 files.

**Gates**

- Tests 32 -> 33: new `preview_selection` (drag range, line highlight, copy of
  rendered text).

---

## 1.1.2 (2026-10-31)

**Added**

- **The AI chat window (PeciaAIChat) is now "file as state"**: the conversation no
  longer depends on an inter-process push pipe - `ai_chat.txt` next to the exe is
  the single persistence layer. "Edit > Send to AI" appends the selection as one
  `user` record, the chat window polls the file every 0.15 s, reloads on change
  and issues the request itself; pushes that arrive while the window is closed are
  replayed on the next load. `LuaTool/AiPushServer.{h,cpp}` removed, and the AI
  process may now only write the `ai_*` settings keys.
- **Markdown preview-only mode** (Markdown > Preview Only, `Ctrl+Alt+P`): hides the
  editor and keeps only the preview. Empty documents cannot enter the mode - the
  emptiness test lives in `ui/PreviewMode.h` (BOM, whitespace and full-width spaces
  all count as empty) - and an empty preview now reads "(empty document)" instead
  of showing a blank panel.
- **Preview policy when opening .md** (Settings > Interface & System): nothing /
  split preview / preview only, applied only to freshly opened files.
- **Outline folding**: the sidebar outline folds level by level, with new
  "expand all / collapse all" and "refresh" icon buttons.
- **Pasting an image or file inserts a path** (Settings > Editing, off by default):
  when the clipboard carries no text, a bitmap is encoded to PNG and a copied file
  is duplicated into `<doc dir>\<doc name>\`, then inserted as a Markdown link.
  Directory and file names are sanitised (spaces and brackets truncate a bare
  link) and CJK is preserved.
- **Chat history file renamed to `ai_chat.txt`** (was `ai_chat.log.txt`), migrated
  on first run; if the old file is locked the session falls back to the old name
  rather than splitting history between the two executables.

**Fixed**

- Rounded window corners lost after maximize/restore: the user preference and the
  effective state are now separate, so a window that starts maximized no longer
  writes "square" back into the preference.
- The edge-resize hit zone was too narrow on the scrollbar side; it now uses its
  own constant, shared by the main window and the tool windows.
- An empty preview panel gave no feedback at all and looked like a frozen window.

**Changed**

- The status bar shows position as `12:5` instead of `Ln 12 / Col 5`, dropping the
  hardcoded English labels and freeing width for the other fields.

**Gates**

- Tests 27 -> 32: `ai_chat_log`, `image_attach`, `outline_collapse`,
  `preview_mode`, `rounded_corners`.

---
