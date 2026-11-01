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
