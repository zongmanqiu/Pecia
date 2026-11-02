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
