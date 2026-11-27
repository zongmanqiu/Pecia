// MainWindow_ops.cpp - the host-capability layer (m020).
//
// Implements the core/HostOps registry entries against this MainWindow.
// This file is the ONLY place window capabilities are described; Lua (and
// later the MCP endpoint) project from the registry instead of holding
// their own lists (single-source rule, .plan/m020 + m024).
//
// Threading contract: every handler below runs ON THE UI THREAD (the
// dispatcher marshals through Fl::awake). They may therefore touch widgets
// directly and must never block on dialogs longer than the 30 s dispatch
// timeout allows.
#include "ui/MainWindow.h"

#include "core/HostOps.h"
#include "editor/Document.h"
#include "editor/Editor.h"
#include "core/PathUtils.h"
#include "mdview/preprocess.h"        // extractOutlineHeadings / md_to_html
#include "script/ScriptManager.h"     // scriptManagerScan
#include "script/LuaEngine.h"         // m025 execution surface
#include "script/LuaParamParser.h"    // --!param declarations
#include "AIChat/AiChatLog.h"         // built-in chat log (ai.get)
#include "core/I18n.h"
#include "core/Config.h"              // m_cfg->getLang (m025 execution ops)
#include "ui/WidgetRegistry.h"        // m025 UI measurement surface

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_utf8.h>   // fl_fopen
#include <FL/x.H>          // fl_xid (ui.pixel PrintWindow)

#include <typeinfo>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   // GetCurrentThreadId
#endif

#include <string>
#include <cctype>   // toupper / isalpha (hostPathAllowed)
#include <deque>
#include <mutex>

// ---------------------------------------------------------------------------
// UI dispatch queue
//
// The registry hands UI-thread work over through the executor installed in
// registerHostOps(). The first version woke the loop with Fl::awake(fn, arg),
// i.e. it ran the packet AS an awake callback - inside whatever event pump
// happened to be running. A handler that pumps the event loop itself
// (ui.wait does: Fl::wait(0.05) in a loop, so a dialog can appear while it
// waits) would therefore run every queued packet NESTED inside itself: a
// second packet's handler can re-enter the script engine, start another
// ui.wait inside a running one, or outlive the window that owns it.
//
// So the work goes through ONE queue instead. The executor only enqueues and
// wakes a single drain; the drain refuses to start while a host handler is
// running, and the runner that called the handler picks the queue up when it
// returns. A nested event pump can therefore never start a second packet.
//
// The same-thread fast path stays INLINE (see registerHostOps): a script bar
// run must see the answer before the call returns, and deferring it to the
// next event loop iteration would deadlock (the UI thread would be waiting
// for the answer it is supposed to deliver itself).
// ---------------------------------------------------------------------------
namespace {

struct UiTask {
    void (*fn)(void *);
    void *arg;
};

std::mutex g_uiQueueMu;
std::deque<UiTask> g_uiQueue;
bool g_awakeScheduled = false;   // a wake for the drain is posted
int g_inHostHandler = 0;        // UI thread only: >0 => inside a host handler

// ui.send 的 click 分支要驱动真实 handle() 链，必须临时改写 FLTK 的全局事件
// 状态。这些变量是全局的：忘了恢复就会污染后续真实事件（按钮一直按下、
// 坐标跳到某个子控件）。用 RAII 在进入前保存、离开时（含提前返回/异常）恢复。
struct FltkEventStateGuard {
    int x, y, state;
    FltkEventStateGuard() : x(Fl::e_x), y(Fl::e_y), state(Fl::e_state) {}
    ~FltkEventStateGuard() {
        Fl::e_x = x;
        Fl::e_y = y;
        Fl::e_state = state;
    }
    FltkEventStateGuard(const FltkEventStateGuard &) = delete;
    FltkEventStateGuard &operator=(const FltkEventStateGuard &) = delete;
};

// Runs the queued packets, one at a time, on the UI thread.
void drainUiQueue(void *)
{
    {
        std::lock_guard<std::mutex> lk(g_uiQueueMu);
        g_awakeScheduled = false;   // this wake is consumed
    }
    // A host handler is running: a nested event pump brought us here. Running
    // the queued packet NOW would nest it inside that handler. The runner
    // that called the handler takes the queue over when the handler returns,
    // so nothing is lost - the packet only runs a moment later.
    if (g_inHostHandler > 0) return;

    for (;;) {
        UiTask task{};
        {
            std::lock_guard<std::mutex> lk(g_uiQueueMu);
            if (g_uiQueue.empty()) return;
            task = g_uiQueue.front();
            g_uiQueue.pop_front();
        }
        g_inHostHandler++;
        task.fn(task.arg);
        g_inHostHandler--;
    }
}

// Makes sure the event loop will drain the queue (at most one wake pending).
void wakeForPendingQueue()
{
    {
        std::lock_guard<std::mutex> lk(g_uiQueueMu);
        if (g_uiQueue.empty() || g_awakeScheduled) return;
        g_awakeScheduled = true;
    }
    Fl::awake(drainUiQueue, nullptr);
}

// Puts a packet on the UI queue and wakes the loop.
void enqueueUiTask(void (*fn)(void *), void *arg)
{
    {
        std::lock_guard<std::mutex> lk(g_uiQueueMu);
        g_uiQueue.push_back(UiTask{fn, arg});
    }
    wakeForPendingQueue();
}

// RAII marker for "a host handler is running" (UI thread only).
struct HostHandlerScope {
    HostHandlerScope() { ++g_inHostHandler; }
    ~HostHandlerScope() { --g_inHostHandler; }
    HostHandlerScope(const HostHandlerScope &) = delete;
    HostHandlerScope &operator=(const HostHandlerScope &) = delete;
};

} // namespace

// ---------------------------------------------------------------------------
// Path gate for the host ops (m020 §3: "doc.open/save 路径校验").
//
// The agent may only open a real, absolute file path: not a Windows reserved
// device name (CON, NUL, COM1 ... - those resolve to a device, not a file) and
// not a relative path, which would resolve against whatever working directory
// the process happens to have.
// ---------------------------------------------------------------------------
static bool hostPathAllowed(const std::string &path, std::string *why) {
    if (path.empty()) {
        if (why) *why = "bad_arg: empty path";
        return false;
    }
    // Reserved device names, with or without an extension ("NUL.txt" too).
    std::string base = path;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const size_t dot = base.find('.');
    const std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
    std::string up;
    up.reserve(stem.size());
    for (char c : stem) up.push_back((char)toupper((unsigned char)c));
    static const char *const kDevices[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    for (const char *d : kDevices) {
        if (up == d) {
            if (why) *why = "bad_arg: reserved device name";
            return false;
        }
    }
    // Absolute path required: a drive letter ("C:\...") or a UNC path
    // ("\\server\share\...").
    const bool unc = path.size() > 1 && (path[0] == '\\' || path[0] == '/') &&
                     (path[1] == '\\' || path[1] == '/');
    const bool drive = path.size() > 2 && isalpha((unsigned char)path[0]) &&
                       path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    if (!unc && !drive) {
        if (why) *why = "bad_arg: path must be absolute";
        return false;
    }
    return true;
}
#include <vector>
#include <cctype>

// Line number out of a Lua error string ("[string \"...\"]:12: syntax error
// near 'x'"), 0 when absent. Cheap scan, no regex dependency.
static long long luaErrorLine(const std::string &err){
    size_t p = err.find("]:");
    if (p == std::string::npos) return 0;
    size_t i = p + 2;
    long long v = 0;
    bool any = false;
    while (i < err.size() && isdigit((unsigned char)err[i])) {
        v = v * 10 + (err[i] - '0');
        any = true;
        ++i;
    }
    return any ? v : 0;
}

// RTTI class name for ui.inspect: FLTK has no class_name(), and the RTTI
// string on MSVC is "class Fl_Input" - strip the storage-class prefix so an
// agent sees the name a human would type in the FLTK docs.
static std::string widgetClassName(const Fl_Widget *w)
{
    std::string n = typeid(*w).name();
    static const char *cls = "class ";
    if (n.compare(0, strlen(cls), cls) == 0) n.erase(0, strlen(cls));
    return n;
}

// ---------------------------------------------------------------------------
// marshalling + gate
// ---------------------------------------------------------------------------

void MainWindow::registerHostOps()
{
    HostRegistry &reg = HostRegistry::instance();

    // Every op registered below captures THIS window, so they all belong to
    // it: set the default owner before the first add() so the whole set can
    // be withdrawn together at teardown (see unregisterHostOps). Must stay
    // ABOVE the first reg.add() - an add() before this line would be owned by
    // nobody and would outlive the window.
    reg.setDefaultOwner(this);

    // Remember the UI thread: measured in FLTK 1.4.5, Fl::awake(void*) on
    // Win32 is a bare PostThreadMessage (Fl_WinAPI_System_Driver.cxx:1127)
    // - if the UI thread itself dispatched and then blocked on the
    // completion wait, nothing would ever drain the queue: deadlock.
    // Script-bar runs come from FL callbacks, so the same-thread fast
    // path is mandatory, not an optimisation.
    const DWORD uiTid = GetCurrentThreadId();
    reg.setUiExecutor([uiTid](void (*fn)(void *), void *arg) {
        if (GetCurrentThreadId() == uiTid) {
            // Same UI thread: run it inline. The answer has to be there
            // before the caller returns (the script bar waits for it).
            HostHandlerScope scope;
            fn(arg);
            // A packet may have arrived from another thread while this
            // handler ran. Let the EVENT LOOP drain it - draining it here
            // would nest that packet inside the script that just called us.
            if (g_inHostHandler == 0) wakeForPendingQueue();
            return;
        }
        // Another thread: queue it. The queue is drained by the UI thread
        // outside of any running host handler, so a handler that pumps the
        // event loop (ui.wait) cannot run a second packet nested.
        enqueueUiTask(fn, arg);
    });

    // Write/Exec gate: evaluated on the UI THREAD inside applyPacket (M4,
    // m026 audit) - the closure below walks m_tabsList / doc->isEditable(),
    // state the UI thread mutates without a lock, so it must never run on
    // the dispatcher's thread (e.g. the MCP stdio endpoint). An earlier
    // version evaluated it at dispatch time for queue-race reasons; that
    // reasoning was backwards and traded one race for a worse one.
    // With NO document open there is nothing to protect: every Write/Exec
    // handler already fails with not_found in that state, and refusing the
    // console/chat ops (which touch no document) with perm_denied would
    // tell the agent the wrong reason (m025).
    //
    // The liveness token is captured BY VALUE: the registry keeps the gate
    // until another window installs its own, and a window that dies while
    // another one is still alive would otherwise leave its (dangling) gate
    // behind. The token outlives the window and is cleared by ~MainWindow as
    // its first act, so a stale gate fails SAFE (no writes) instead of
    // dereferencing freed memory.
    std::shared_ptr<std::atomic<bool>> alive = m_alive;
    reg.setWriteGate([this, alive]() {
        if (!alive->load()) return false;   // the owning window is gone
        Tab *t = activeTab();
        if (!t || !t->doc) return true;
        return t->doc->isEditable();
    });

    // ---- helpers shared by handlers ------------------------------------
    auto activeBuf = [this]() -> Fl_Text_Buffer * {
        Tab *t = activeTab();
        return (t && t->editor) ? t->editor->buffer() : nullptr;
    };
    auto activeDoc = [this]() -> Document * {
        Tab *t = activeTab();
        return t ? t->doc : nullptr;
    };

    // ---- Read ops -------------------------------------------------------

    reg.add({"doc.get_text",
             "Active document text. range: [start,length] byte window; omitted = whole document. Returns str.",
             {{"start", "int?", "byte offset (default 0)"},
              {"length", "int?", "byte count from start (default: to end)"}},
             OpPerm::Read,
             [activeBuf](const std::map<std::string, OpValue> &a) -> OpOutcome {
        Fl_Text_Buffer *buf = activeBuf();
        if (!buf) return OpOutcome::fail("not_found: no active document");
        const int len = buf->length();
        long long start = a.count("start") ? a.at("start").getInt() : 0;
        long long take  = a.count("length") ? a.at("length").getInt() : len;
        if (start < 0 || start > len) return OpOutcome::fail("bad_arg: start out of range");
        if (take < 0) return OpOutcome::fail("bad_arg: length must be >= 0");
        // Clamp WITHOUT overflowing: start + take could wrap (start=5,
        // take=INT64_MAX) and defeat the range check, which then handed a
        // garbage end offset to text_range() and leaked the FLTK buffer when
        // the string constructor threw.
        if (take > (long long)len - start) take = (long long)len - start;
        char *txt = buf->text_range((int)start, (int)(start + take));
        if (!txt) return OpOutcome::fail("internal: text_range");
        // Byte-for-byte the buffer: this is the same text doc.apply_edit's
        // anchor is matched against, so line ends must survive the round trip
        // (an earlier version dropped '\n', which left CRLF documents with bare
        // '\r' and made every anchor built from a read fail).
        std::string out;
        try {
            out.assign(txt, (size_t)take);
        } catch (...) {
            free(txt);
            return OpOutcome::fail("internal: out of memory building the result");
        }
        free(txt);
        return OpOutcome::success(OpValue::makeStr(std::move(out)));
    }});

    reg.add({"doc.info",
             "Active document state: path, dirty, length(bytes), encoding(int), read_only, edit_locked.",
             {},
             OpPerm::Read,
             [activeDoc](const std::map<std::string, OpValue> &) -> OpOutcome {
        Document *d = activeDoc();
        if (!d) return OpOutcome::fail("not_found: no active document");
        Fl_Text_Buffer *buf = d->buffer();
        OpValue m = OpValue::makeMap();
        m.set("path", OpValue::makeStr(d->filePath() ? d->filePath() : ""));
        m.set("dirty", OpValue::makeBool(d->isDirty()));
        m.set("length", OpValue::makeInt(buf ? buf->length() : 0));
        m.set("encoding", OpValue::makeInt((long long)d->encoding()));
        m.set("read_only", OpValue::makeBool(d->isReadOnly()));
        m.set("edit_locked", OpValue::makeBool(d->isEditLocked()));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"outline.list",
             "Markdown headings of the active document: list of {level, text, id, line}.",
             {},
             OpPerm::Read,
             [activeBuf](const std::map<std::string, OpValue> &) -> OpOutcome {
        Fl_Text_Buffer *buf = activeBuf();
        if (!buf) return OpOutcome::fail("not_found: no active document");
        char *t = buf->text();
        if (!t) return OpOutcome::fail("internal: text");
        std::string md = t;
        free(t);
        std::vector<PreviewHeading> hs;
        extractOutlineHeadings(md, hs);
        OpValue lst = OpValue::makeList();
        for (const auto &h : hs) {
            OpValue m = OpValue::makeMap();
            m.set("level", OpValue::makeInt(h.level));
            m.set("text", OpValue::makeStr(h.text));
            m.set("id", OpValue::makeStr(h.id));
            m.set("line", OpValue::makeInt(h.line));
            lst.push(std::move(m));
        }
        return OpOutcome::success(std::move(lst));
    }});

    reg.add({"find.all",
             "Plain-text (no regex) case-sensitive search in the active document: list of byte offsets.",
             {{"needle", "string", "text to find"}},
             OpPerm::Read,
             [activeBuf](const std::map<std::string, OpValue> &a) -> OpOutcome {
        Fl_Text_Buffer *buf = activeBuf();
        if (!buf) return OpOutcome::fail("not_found: no active document");
        const std::string &needle = a.at("needle").getString();
        if (needle.empty()) return OpOutcome::fail("bad_arg: needle is empty");
        char *t = buf->text();
        if (!t) return OpOutcome::fail("internal: text");
        std::string all = t;
        free(t);
        OpValue lst = OpValue::makeList();
        size_t pos = 0;
        while ((pos = all.find(needle, pos)) != std::string::npos) {
            lst.push(OpValue::makeInt((long long)pos));
            pos += needle.size();
        }
        return OpOutcome::success(std::move(lst));
    }});

    reg.add({"script.list",
             "Script bar catalog: {root:[names], groups:[{name, scripts:[names]}]} (paths relative to script/).",
             {},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        ScriptCatalog cat = scriptManagerScan();
        OpValue m = OpValue::makeMap();
        OpValue root = OpValue::makeList();
        for (const auto &s : cat.rootScripts) root.push(OpValue::makeStr(s));
        m.set("root", std::move(root));
        OpValue groups = OpValue::makeList();
        for (const auto &g : cat.groups) {
            OpValue gm = OpValue::makeMap();
            gm.set("name", OpValue::makeStr(g.name));
            OpValue sl = OpValue::makeList();
            for (const auto &s : g.scripts) sl.push(OpValue::makeStr(s));
            gm.set("scripts", std::move(sl));
            groups.push(std::move(gm));
        }
        m.set("groups", std::move(groups));
        m.set("root_path", OpValue::makeStr(cat.rootPath));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"doc.api",
             "Read a shipped documentation file by bare name: lua_api.txt (under script/), "
             "or a docs/ file such as MCP-usage once the updater has fetched it.",
             {{"name", "string", "document file name"}},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        const std::string name = a.at("name").getString();
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos
            || name.find("..") != std::string::npos || name.empty() || name.front() == '.')
            return OpOutcome::fail("bad_arg: bare file name required");
        // Install layout (docs/RELEASE_CHECKLIST.md §三): the API text ships as
        // script/lua_api.txt, handbook pages land in docs/ when the updater
        // fetches them, and root files sit next to the exe. Looking only in the
        // exe directory made this answer not_found for lua_api.txt - the first
        // call the instructions tell every agent to make.
        static const char *const subdirs[] = { "script", "docs", "" };
        const std::string base = pathutil::exeDir() + "\\";
        std::string path;
        FILE *f = nullptr;
        for (const char *sub : subdirs) {
            path = *sub ? base + sub + "\\" + name : base + name;
            f = fl_fopen(path.c_str(), "rb");
            if (f) break;
        }
        if (!f) return OpOutcome::fail("not_found: " + name);
        std::string data;
        char buf[8192];
        size_t n;
        unsigned long total = 0;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            data.append(buf, n);
            total += (unsigned long)n;
            if (total > (8u << 20)) { fclose(f); return OpOutcome::fail("internal: file exceeds 8 MB"); }
        }
        fclose(f);
        return OpOutcome::success(OpValue::makeStr(std::move(data)));
    }});

    // ---- Write ops ------------------------------------------------------

    reg.add({"doc.apply_edit",
             "Replace old_text at anchor (first exact occurrence) or at byte start. Exactly one location must match; no-op unless old_text is found verbatim. Returns {pos}.",
             {{"old_text", "string", "exact text to replace"},
              {"new_text", "string", "replacement text"},
              {"anchor", "string?", "context string; edit applies at its first occurrence"},
              {"start", "int?", "byte offset (used when anchor omitted)"}},
             OpPerm::Write,
             [activeBuf](const std::map<std::string, OpValue> &a) -> OpOutcome {
        Fl_Text_Buffer *buf = activeBuf();
        if (!buf) return OpOutcome::fail("not_found: no active document");
        const std::string &oldT = a.at("old_text").getString();
        const std::string &newT = a.at("new_text").getString();
        if (oldT.empty()) return OpOutcome::fail("bad_arg: old_text is empty");
        char *t = buf->text();
        if (!t) return OpOutcome::fail("internal: text");
        std::string all = t;
        free(t);
        size_t at;
        if (a.count("anchor")) {
            const std::string &anc = a.at("anchor").getString();
            if (anc.empty()) return OpOutcome::fail("bad_arg: anchor is empty");
            size_t p = all.find(anc);
            if (p == std::string::npos) return OpOutcome::fail("not_found: anchor");
            if (all.find(anc, p + 1) != std::string::npos)
                return OpOutcome::fail("bad_arg: anchor not unique - extend it");
            at = p;
        } else {
            if (!a.count("start")) return OpOutcome::fail("bad_arg: anchor or start required");
            const long long startAt = a.at("start").getInt();
            if (startAt < 0) return OpOutcome::fail("bad_arg: start must be >= 0");
            at = (size_t)startAt;
        }
        // The bounds check must not overflow: at + oldT.size() wraps for a
        // huge `start`, which turned a bad_arg into an out_of_range throw.
        if (at > all.size() || oldT.size() > all.size() - at ||
            all.compare(at, oldT.size(), oldT) != 0)
            return OpOutcome::fail("not_found: old_text at target position");
        buf->undo_begin();          // one undo step for the whole edit
        buf->remove((int)at, (int)(at + oldT.size()));
        buf->insert((int)at, newT.c_str());
        buf->undo_end();
        OpValue m = OpValue::makeMap();
        m.set("pos", OpValue::makeInt((long long)at));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"doc.replace_all",
             "Plain-text case-sensitive replace-all in the active document. Returns count.",
             {{"search", "string", "text to find"},
              {"replace", "string", "replacement text"}},
             OpPerm::Write,
             [activeBuf](const std::map<std::string, OpValue> &a) -> OpOutcome {
        Fl_Text_Buffer *buf = activeBuf();
        if (!buf) return OpOutcome::fail("not_found: no active document");
        const std::string &needle = a.at("search").getString();
        const std::string &repl = a.at("replace").getString();
        if (needle.empty()) return OpOutcome::fail("bad_arg: search is empty");
        char *t = buf->text();
        if (!t) return OpOutcome::fail("internal: text");
        std::string all = t;
        free(t);
        int count = 0;
        size_t pos = 0;
        while ((pos = all.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
        if (!count) return OpOutcome::success(OpValue::makeInt(0));
        // rebuild in one pass (per-hit replace would churn the gap many times)
        std::string out;
        size_t last = 0, p = 0;
        while ((p = all.find(needle, last)) != std::string::npos) {
            out.append(all, last, p - last);
            out += repl;
            last = p + needle.size();
        }
        out.append(all, last, std::string::npos);
        buf->undo_begin();
        // Replace the buffer's CONTENT without destroying the user's undo
        // history. Fl_Text_Buffer::text() clears mUndo/mUndoList (see
        // src/Fl_Text_Buffer.cxx:309-311), so the whole-buffer write wiped
        // every edit the user could still undo - and the replace itself was
        // not undoable either. select()+replace_selection() goes through the
        // normal BufReplace path, which is undoable.
        buf->select(0, buf->length());
        buf->replace_selection(out.c_str());
        buf->undo_end();
        return OpOutcome::success(OpValue::makeInt(count));
    }});

    reg.add({"doc.save",
             "Save the active document. Returns saved(bool). Refuses for an untitled "
             "document (no path) and for an encoding that would lose data - it never "
             "opens a Save-As dialog.",
             {},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &) -> OpOutcome {
        int i = activeTabIndex();
        if (i < 0) return OpOutcome::fail("not_found: no active document");
        std::string err;
        const bool saved = doSaveForHost(i, &err);
        if (!saved && !err.empty()) return OpOutcome::fail(err);
        OpValue m = OpValue::makeMap();
        m.set("saved", OpValue::makeBool(saved));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"doc.open",
             "Open a file path in THIS window (reuses an already-open tab). Never opens "
             "a second window and never prompts: refuses when the active document has "
             "unsaved changes. Returns true.",
             {{"path", "string", "absolute file path"}},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        const std::string &path = a.at("path").getString();
        std::string why;
        if (!hostPathAllowed(path, &why)) return OpOutcome::fail(why);
        std::string err;
        if (!openFileForHost(path.c_str(), &err))
            return OpOutcome::fail(err.empty() ? std::string("io: cannot open the file") : err);
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    reg.add({"doc.new",
             "Create a fresh empty tab and make it active. Refuses when the active "
             "document has unsaved changes (never prompts). Returns true.",
             {},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &) -> OpOutcome {
        std::string err;
        if (!newFileForHost(&err))
            return OpOutcome::fail(err.empty() ? std::string("internal: cannot create a new document") : err);
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    reg.add({"tab.switch",
             "Activate tab by 0-based index. May prompt to save if state requires.",
             {{"index", "int", "tab index"}},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        const int idx = (int)a.at("index").getInt();
        if (idx < 0 || idx >= (int)m_tabsList.size())
            return OpOutcome::fail("bad_arg: index out of range");
        switchToTab(idx);
        OpValue m = OpValue::makeMap();
        m.set("active", OpValue::makeInt(activeTabIndex()));
        m.set("count", OpValue::makeInt((long long)m_tabsList.size()));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"tab.list",
             "Open tabs: list of {index, path, dirty, active}.",
             {},
             OpPerm::Read,
             [this](const std::map<std::string, OpValue> &) -> OpOutcome {
        const int cur = activeTabIndex();
        OpValue lst = OpValue::makeList();
        for (int i = 0; i < (int)m_tabsList.size(); ++i) {
            OpValue m = OpValue::makeMap();
            m.set("index", OpValue::makeInt(i));
            m.set("path", OpValue::makeStr(m_tabsList[i].doc && m_tabsList[i].doc->filePath()
                                               ? m_tabsList[i].doc->filePath() : ""));
            m.set("dirty", OpValue::makeBool(m_tabsList[i].doc && m_tabsList[i].doc->isDirty()));
            m.set("active", OpValue::makeBool(i == cur));
            lst.push(std::move(m));
        }
        return OpOutcome::success(std::move(lst));
    }});

    reg.add({"doc.reveal",
             "Scroll/reveal the given byte position in the active editor.",
             {{"pos", "int", "byte offset"}},
             OpPerm::Read,   // view-only: changes no text, gate not required
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        Tab *t = activeTab();
        if (!t || !t->editor || !t->doc) return OpOutcome::fail("not_found: no active document");
        Fl_Text_Buffer *buf = t->editor->buffer();
        if (!buf) return OpOutcome::fail("internal: no buffer");
        const int len = buf->length();
        long long pos = a.at("pos").getInt();
        if (pos < 0 || pos > len) return OpOutcome::fail("bad_arg: pos out of range");
        t->editor->insert_position((int)pos);
        t->editor->show_insert_position();
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    // -----------------------------------------------------------------
    // m025: the Lua execution surface. Before this round the MCP client
    // could read and edit the document but could NOT write/run a script,
    // so the "write -> validate -> run -> read error -> fix" loop the
    // instructions promise was unreachable (m024 4 listed run_lua as the
    // meta-tool; this is its landing).
    // -----------------------------------------------------------------

    // Wall-clock wish -> the deadline of one run. kMaxRunWallMs (5 s) is the
    // longest the UI thread may be stalled by a single script; the engine
    // derives the instruction budget from this SAME deadline, so the script
    // bar and the agent path get the same treatment (they used to differ by
    // 12x: the agent path asked for 60M instructions for the same 3 seconds).
    auto timeoutFor = [](long long ms) -> int {
        if (ms <= 0) ms = 3000;
        if (ms > 5000) ms = 5000;          // UI thread must not stall longer
        return (int)ms;
    };
    auto capFor = [](long long c) -> int {
        if (c <= 0) return 32768;
        if (c > 65536) return 65536;
        return (int)c;
    };

    // Shared tail: push the script's final cursor/selection back into the
    // editor (same contract as the script bar path).
    auto syncEditorAfterRun = [this](LuaEditorHost &host) {
        Tab *t = activeTab();
        if (!t || !t->editor || !t->doc) return;
        Fl_Text_Buffer *buf = t->doc->buffer();
        if (!buf) return;
        const int len = buf->length();
        if (host.cursorPos < 0) host.cursorPos = 0;
        if (host.cursorPos > len) host.cursorPos = len;
        t->editor->insert_position(host.cursorPos);
        t->editor->show_insert_position();
        if (host.selStart < 0) host.selStart = 0;
        if (host.selEnd > len) host.selEnd = len;
        if (host.hasSelection && host.selEnd > host.selStart)
            buf->select(host.selStart, host.selEnd);
        else
            buf->unselect();
        t->editor->notifyCursorMoved();
        updateStatusBar();
    };

    reg.add({"lua.validate",
             "Compile a Lua script WITHOUT executing it (no document change, no dialog, no side effect): {ok, error, line}. Always run this before lua.run.",
             {{"code", "string", "script text (UTF-8, same shape as a script/ file)"}},
             OpPerm::Exec,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        if (!m_luaEngine) m_luaEngine = new LuaEngine();
        std::string err;
        const bool ok = m_luaEngine->validate(a.at("code").getString(), &err);
        OpValue m = OpValue::makeMap();
        m.set("ok", OpValue::makeBool(ok));
        m.set("error", OpValue::makeStr(ok ? std::string() : err));
        m.set("line", OpValue::makeInt(luaErrorLine(err)));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"lua.run",
             "Run a Lua script against the ACTIVE document. Executes on a scratch copy: a failed run changes nothing. Returns {verdict: pass|error, output, result, error, truncated, applied}. headless (default true) captures win:message/question/input/select as text so no modal can stall the UI thread.",
             {{"code", "string", "script text"},
              {"sel_start", "int?", "byte offset of the selection start"},
              {"sel_end", "int?", "byte offset of the selection end (exclusive)"},
              {"timeout_ms", "int?", "wall-clock budget in ms (default 3000, max 5000); the instruction budget is derived from it"},
              {"out_cap", "int?", "print() cap bytes (default 32768, max 65536)"},
              {"headless", "bool?", "capture dialogs as text instead of showing them"},
              {"answer", "bool?", "win:question answer while headless"}},
             OpPerm::Exec,
             [this, timeoutFor, capFor, syncEditorAfterRun](
                 const std::map<std::string, OpValue> &a) -> OpOutcome {
        Tab *t = activeTab();
        if (!t || !t->editor || !t->doc)
            return OpOutcome::fail("not_found: no active document");
        Fl_Text_Buffer *buf = t->doc->buffer();
        if (!buf) return OpOutcome::fail("internal: no buffer");
        if (!m_luaEngine) m_luaEngine = new LuaEngine();

        LuaEditorHost host;
        host.buffer = buf;
        host.cursorPos = t->editor->insert_position();
        host.hasSelection = buf->selection_position(&host.selStart, &host.selEnd);
        // The selection can be stated explicitly: most built-in scripts act
        // ONLY on a selection, and an agent has no mouse. [sel_start, sel_end)
        // are byte offsets into the document.
        if (a.count("sel_start") && a.count("sel_end")) {
            const long long s0 = a.at("sel_start").getInt();
            const long long s1 = a.at("sel_end").getInt();
            if (s0 < 0 || s1 < s0 || s1 > buf->length())
                return OpOutcome::fail("bad_arg: selection range out of bounds");
            host.selStart = (int)s0;
            host.selEnd = (int)s1;
            host.hasSelection = s1 > s0;
        }

        LuaRunOptions opts;
        opts.headless = true;
        if (a.count("headless")) opts.headless = a.at("headless").getBool(true);
        opts.answer = a.count("answer") ? a.at("answer").getBool(false) : false;
        opts.timeoutMs =
            timeoutFor(a.count("timeout_ms") ? a.at("timeout_ms").getInt() : 0);
        // maxInstructions stays 0: the engine derives the instruction budget
        // from the deadline above, so both call paths get the same budget.
        opts.outputCap = capFor(a.count("out_cap") ? a.at("out_cap").getInt() : 0);
        opts.resultCap = opts.outputCap;

        LuaRunResult res;
        std::string err;
        char langCode[16] = "en";
        m_cfg->getLang(langCode, sizeof(langCode), "en");
        const bool ok = m_luaEngine->run(a.at("code").getString(), host,
                                         nullptr, &err, nullptr, langCode,
                                         &res, &opts);
        syncEditorAfterRun(host);
        // The user sees their document change; give them the one-line
        // reason. No info window here - it is modal and would stall the
        // single-in-flight endpoint (m037).
        m_lastScriptMs = res.elapsedMs;
        reportScriptOutcome(ok, res.output, err, false);

        OpValue m = OpValue::makeMap();
        m.set("verdict", OpValue::makeStr(ok ? "pass" : "error"));
        m.set("output", OpValue::makeStr(res.output));
        m.set("result", OpValue::makeStr(res.result));
        m.set("error", OpValue::makeStr(ok ? std::string() : err));
        m.set("line", OpValue::makeInt(ok ? 0 : luaErrorLine(err)));
        m.set("truncated", OpValue::makeBool(res.truncated || res.resultTruncated));
        m.set("applied", OpValue::makeBool(res.applied));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"script.run",
             "Run an INSTALLED script (name as reported by script.list, e.g. \"pair\" or \"Row/dedupe\"). params is a map of the script's --!param names; a declared param with no default and no entry fails with bad_arg instead of opening a dialog. Same result shape as lua.run plus applied.",
             {{"name", "string", "script path relative to script/, no extension"},
              {"params", "map?", "--!param values by name"},
              {"sel_start", "int?", "byte offset of the selection start"},
              {"sel_end", "int?", "byte offset of the selection end (exclusive)"},
              {"timeout_ms", "int?", "wall-clock budget in ms (default 3000, max 5000)"},
              {"out_cap", "int?", "print() cap bytes"},
              {"headless", "bool?", "capture dialogs as text (default true)"}},
             OpPerm::Exec,
             [this, timeoutFor, capFor, syncEditorAfterRun](
                 const std::map<std::string, OpValue> &a) -> OpOutcome {
        Tab *t = activeTab();
        if (!t || !t->editor || !t->doc)
            return OpOutcome::fail("not_found: no active document");
        Fl_Text_Buffer *buf = t->doc->buffer();
        if (!buf) return OpOutcome::fail("internal: no buffer");
        const std::string &name = a.at("name").getString();
        if (name.empty()) return OpOutcome::fail("bad_arg: name is empty");

        const std::string path = scriptManagerPath(name);
        if (path.empty()) return OpOutcome::fail("not_found: no script \"" + name + "\"");

        FILE *fp = fl_fopen(path.c_str(), "rb");
        if (!fp) return OpOutcome::fail("not_found: cannot open script");
        std::string script;
        {
            char chunk[8192];
            size_t got = 0;
            while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0)
                script.append(chunk, got);
            fclose(fp);
        }
        if (script.empty()) return OpOutcome::fail("internal: empty script file");

        // --!param values: caller map first, declared default second, else
        // a structured bad_arg (NEVER a modal dialog on this path).
        char langCode[16] = "en";
        m_cfg->getLang(langCode, sizeof(langCode), "en");
        std::vector<std::string> knownLangs;
        for (const auto &li : I18n::scanLanguages()) knownLangs.push_back(li.code);
        const LuaParamSet decls = luaParseParams(script, langCode, knownLangs);
        const OpValue *pmap = a.count("params") ? &a.at("params") : nullptr;
        std::vector<std::pair<std::string, std::string>> paramValues;
        for (const LuaParam &p : decls.params) {
            std::string v;
            bool given = false;
            if (pmap && pmap->kind() == OpValue::Kind::Map) {
                if (const OpValue *pv = pmap->find(p.name)) {
                    v = pv->getString();
                    given = true;
                }
            }
            if (!given) {
                if (!p.defValue.empty() || !p.choices.empty()) {
                    v = p.defValue;             // dropdowns default to first
                    if (v.empty() && !p.choices.empty()) v = p.choices.front();
                } else {
                    return OpOutcome::fail("bad_arg: param missing: " + p.name);
                }
            }
            paramValues.push_back({p.name, v});
        }

        if (!m_luaEngine) m_luaEngine = new LuaEngine();
        LuaEditorHost host;
        host.buffer = buf;
        host.cursorPos = t->editor->insert_position();
        host.hasSelection = buf->selection_position(&host.selStart, &host.selEnd);
        // The selection can be stated explicitly: most built-in scripts act
        // ONLY on a selection, and an agent has no mouse. [sel_start, sel_end)
        // are byte offsets into the document.
        if (a.count("sel_start") && a.count("sel_end")) {
            const long long s0 = a.at("sel_start").getInt();
            const long long s1 = a.at("sel_end").getInt();
            if (s0 < 0 || s1 < s0 || s1 > buf->length())
                return OpOutcome::fail("bad_arg: selection range out of bounds");
            host.selStart = (int)s0;
            host.selEnd = (int)s1;
            host.hasSelection = s1 > s0;
        }

        LuaRunOptions opts;
        opts.headless = true;
        if (a.count("headless")) opts.headless = a.at("headless").getBool(true);
        opts.timeoutMs =
            timeoutFor(a.count("timeout_ms") ? a.at("timeout_ms").getInt() : 0);
        // maxInstructions stays 0: the engine derives the instruction budget
        // from the deadline above, so both call paths get the same budget.
        opts.outputCap = capFor(a.count("out_cap") ? a.at("out_cap").getInt() : 0);
        opts.resultCap = opts.outputCap;

        LuaRunResult res;
        std::string err;
        const bool ok = m_luaEngine->run(script, host, nullptr, &err,
                                         paramValues.empty() ? nullptr : &paramValues,
                                         langCode, &res, &opts);
        syncEditorAfterRun(host);
        // The user sees their document change; give them the one-line
        // reason. No info window here - it is modal and would stall the
        // single-in-flight endpoint (m037).
        m_lastScriptMs = res.elapsedMs;
        reportScriptOutcome(ok, res.output, err, false);

        OpValue m = OpValue::makeMap();
        m.set("verdict", OpValue::makeStr(ok ? "pass" : "error"));
        m.set("output", OpValue::makeStr(res.output));
        m.set("result", OpValue::makeStr(res.result));
        m.set("error", OpValue::makeStr(ok ? std::string() : err));
        m.set("line", OpValue::makeInt(ok ? 0 : luaErrorLine(err)));
        m.set("truncated", OpValue::makeBool(res.truncated || res.resultTruncated));
        m.set("applied", OpValue::makeBool(res.applied));
        return OpOutcome::success(std::move(m));
    }});

    reg.add({"console.get",
             "The Lua draft (the script an agent or the user is preparing). Returns str.",
             {},
             OpPerm::Read,
             [this](const std::map<std::string, OpValue> &) -> OpOutcome {
        return OpOutcome::success(OpValue::makeStr(luaDraftText()));
    }});

    reg.add({"console.set",
             "Replace the Lua draft and persist it. Does NOT run the script and does NOT save it to script/ - use lua.run / script.save. Nothing pops up: use console.show when the user should see it.",
             {{"code", "string", "full draft text"}},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        setLuaDraft(a.at("code").getString());
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    reg.add({"console.show",
             "Open the Lua draft in a new editor tab so the user can read and edit it (there is no separate console window). Returns true.",
             {},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &) -> OpOutcome {
        std::string err;
        if (!showLuaDraftAsTab(&err)) return OpOutcome::fail(err);
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    reg.add({"script.save",
             "Save the Lua draft into script/<group>/<name>.lua so it appears in the script bar (group = existing subfolder or \"\" for the root). Anchored write only: the script must not already exist unless overwrite=true.",
             {{"name", "string", "file name without extension"},
              {"group", "string?", "existing script subfolder"},
              {"overwrite", "bool?", "replace an existing file (default false)"}},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        const std::string &name = a.at("name").getString();
        const std::string group = a.count("group") ? a.at("group").getString() : "";
        const bool overwrite = a.count("overwrite") ? a.at("overwrite").getBool(false) : false;
        auto badName = [](const std::string &n) {
            if (n.empty() || n.size() > 128) return true;
            if (n.find("..") != std::string::npos) return true;
            if (n.find('/') != std::string::npos || n.find('\\') != std::string::npos) return true;
            if (n.find(':') != std::string::npos) return true;
            for (char c : n)
                if ((unsigned char)c < 0x20) return true;
            return false;
        };
        if (badName(name)) return OpOutcome::fail("bad_arg: name must be a plain file name");
        if (!group.empty() && badName(group))
            return OpOutcome::fail("bad_arg: group must be a plain folder name");

        ScriptCatalog cat = scriptManagerScan();
        if (cat.rootPath.empty()) return OpOutcome::fail("internal: script root missing");

        std::string target = cat.rootPath + "/" + (group.empty() ? name : group + "/" + name) + ".lua";
        FILE *probe = fl_fopen(target.c_str(), "rb");
        if (probe) { fclose(probe);
            if (!overwrite) return OpOutcome::fail("bad_arg: exists (pass overwrite=true)");
        }
        const std::string text = luaDraftText();
        FILE *fp = fl_fopen(target.c_str(), "wb");
        if (!fp) return OpOutcome::fail("internal: cannot write " + target);
        fwrite("\xEF\xBB\xBF", 1, 3, fp);          // BOM: matches the script bar loader
        fwrite(text.data(), 1, text.size(), fp);
        fclose(fp);
        reloadScriptBar();
        return OpOutcome::success(OpValue::makeStr(target));
    }});

    reg.add({"ai.send",
             "Append text to the built-in AI chat as a user message (opens the AI column, independent of the preview column). Independent of MCP: this feeds Pecia's OWN configured endpoint, not the caller.",
             {{"text", "string", "user message"}},
             OpPerm::Write,
             [this](const std::map<std::string, OpValue> &a) -> OpOutcome {
        std::string err;
        if (!sendToAiChat(a.at("text").getString(), &err))
            return OpOutcome::fail("internal: " + err);
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    reg.add({"ai.get",
             "Last assistant reply in the built-in AI chat log (\"\" when none). Returns str.",
             {},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        FILE *fp = _wfopen(ai_chat_log::defaultLogPath().c_str(), L"rb");
        if (!fp) return OpOutcome::success(OpValue::makeStr(std::string()));
        std::string raw;
        {
            char chunk[8192];
            size_t got = 0;
            while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0)
                raw.append(chunk, got);
            fclose(fp);
        }
        std::string last;
        for (const auto &e : ai_chat_log::parseChatLog(raw))
            if (e.role == "assistant") last = e.content;
        return OpOutcome::success(OpValue::makeStr(std::move(last)));
    }});

    // -----------------------------------------------------------------
    // m025: UI measurement of the dialogs a script opens. FLTK draws its own
    // controls, so an external tree walk sees one HWND - the truthful tree is
    // the host's, published by DialogBase::enableInspection into
    // WidgetRegistry, and read back here.
    // -----------------------------------------------------------------

    reg.add({"ui.windows",
             "Dialogs the host currently shows for a script: [{id, kind, title, visible, modal}]. Empty when no script dialog is up.",
             {},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        OpValue lst = OpValue::makeList();
        for (const auto &e : WidgetRegistry::instance().snapshot()) {
            OpValue m = OpValue::makeMap();
            m.set("id", OpValue::makeInt(e.id));
            m.set("kind", OpValue::makeStr(e.kind));
            m.set("title", OpValue::makeStr(e.win->label() ? e.win->label() : ""));
            m.set("visible", OpValue::makeBool(e.win->visible() != 0));
            m.set("modal", OpValue::makeBool(e.win->modal() != 0));
            m.set("w", OpValue::makeInt(e.win->w()));
            m.set("h", OpValue::makeInt(e.win->h()));
            lst.push(std::move(m));
        }
        return OpOutcome::success(std::move(lst));
    }});

    reg.add({"ui.inspect",
             "Widget tree of one script dialog: [{path,class,label,x,y,w,h,visible,sensitive,value}] (x/y relative to the dialog, path = child indices for ui.send). id from ui.windows. The walk stops after 512 nodes (a dialog tree is far smaller in practice).",
             {{"id", "int", "dialog id"}},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        WidgetRegistry::Entry *e = WidgetRegistry::instance().find(
            (int)a.at("id").getInt());
        if (!e) return OpOutcome::fail("not_found: no such dialog id");
        OpValue lst = OpValue::makeList();
        std::vector<int> path;
        // recursive walk over an explicit stack, node-capped: a dialog tree
        // is small by nature, and the cap bounds the work a malformed tree
        // could ask for. The cap is stated in the op description (the
        // answer shape is a list, so there is no room for a flag).
        std::vector<Fl_Widget *> stack { e->win };
        std::vector<std::vector<int>> paths { path };
        int guard = 0;
        while (!stack.empty() && guard++ < 512) {
            Fl_Widget *wd = stack.back(); stack.pop_back();
            std::vector<int> p = paths.back(); paths.pop_back();
            if (wd != e->win) {
                OpValue m = OpValue::makeMap();
                OpValue vp = OpValue::makeList();
                for (int idx : p) vp.push(OpValue::makeInt(idx));
                m.set("path", std::move(vp));
                m.set("class", OpValue::makeStr(widgetClassName(wd)));
                m.set("label", OpValue::makeStr(wd->label() ? wd->label() : ""));
                // Fl_Widget::x()/y() are already relative to the widget's
                // WINDOW - and the dialog IS that window. Subtracting the
                // dialog's ON-SCREEN position mixed two coordinate spaces
                // and reported nonsense (a button at dialog x=300 in a
                // dialog centred at screen x=640 came out as -340).
                m.set("x", OpValue::makeInt(wd->x()));
                m.set("y", OpValue::makeInt(wd->y()));
                m.set("w", OpValue::makeInt(wd->w()));
                m.set("h", OpValue::makeInt(wd->h()));
                // Fl_Widget::visible() only reflects the widget's own hidden
                // flag - a hidden DIALOG must dominate its children, else an
                // agent reads a closed window as still on screen.
                m.set("visible", OpValue::makeBool(
                    (wd->visible() && e->win->visible()) != 0));
                m.set("sensitive", OpValue::makeBool(wd->active() != 0 && wd->output() == 0));
                std::string val;
                if (auto *in = dynamic_cast<Fl_Input *>(wd)) val = in->value() ? in->value() : "";
                else if (auto *cb = dynamic_cast<Fl_Check_Button *>(wd)) val = cb->value() ? "1" : "0";
                else if (auto *ch = dynamic_cast<Fl_Choice *>(wd))
                    val = std::to_string(ch->value());
                m.set("value", OpValue::makeStr(val));
                lst.push(std::move(m));
            }
            if (auto *g = dynamic_cast<Fl_Group *>(wd)) {
                for (int i = g->children() - 1; i >= 0; --i) {
                    auto q = p;
                    q.push_back(i);
                    stack.push_back(g->child(i));
                    paths.push_back(std::move(q));
                }
            }
        }
        return OpOutcome::success(std::move(lst));
    }});

    reg.add({"ui.pixel",
             "Colour of one client-area pixel of a script dialog, captured off-screen (PrintWindow): returns \"#rrggbb\". Works whether or not the window is in front.",
             {{"id", "int", "dialog id"},
              {"x", "int", "client x"},
              {"y", "int", "client y"}},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        WidgetRegistry::Entry *e = WidgetRegistry::instance().find(
            (int)a.at("id").getInt());
        if (!e) return OpOutcome::fail("not_found: no such dialog id");
        HWND hwnd = fl_xid(e->win);
        const int x = (int)a.at("x").getInt();
        const int y = (int)a.at("y").getInt();
        if (x < 0 || y < 0 || x >= e->win->w() || y >= e->win->h())
            return OpOutcome::fail("bad_arg: pixel outside the dialog");

        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, e->win->w(), e->win->h());
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        // PW_RENDERFULLCONTENT (0x2) is what makes a DirectComposition/GL
        // surface actually paint into the DC on Win8.1+.
        const BOOL okPaint = PrintWindow(hwnd, mem, 2);
        std::string out;
        if (okPaint) {
            const COLORREF c = GetPixel(mem, x, y);
            char hex[10];
            snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                     (int)GetRValue(c), (int)GetGValue(c), (int)GetBValue(c));
            out = hex;
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        if (out.empty()) return OpOutcome::fail("internal: PrintWindow failed");
        return OpOutcome::success(OpValue::makeStr(std::move(out)));
    }});

    reg.add({"ui.send",
             "Synthesise one interaction with a script dialog: event=click|type on the widget at path (from ui.inspect). click drives PUSH/RELEASE through the real handle() chain; type appends text to an Fl_Input.",
             {{"id", "int", "dialog id"},
              {"path", "list", "child indices, e.g. [1,0]"},
              {"event", "string", "click | type"},
              {"text", "string?", "for event=type"}},
             OpPerm::Exec,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        WidgetRegistry::Entry *e = WidgetRegistry::instance().find(
            (int)a.at("id").getInt());
        if (!e) return OpOutcome::fail("not_found: no such dialog id");
        const OpValue &p = a.at("path");
        if (p.kind() != OpValue::Kind::List || p.list().empty())
            return OpOutcome::fail("bad_arg: path must be a non-empty list");
        Fl_Widget *wd = e->win;
        for (const OpValue &step : p.list()) {
            Fl_Group *g = dynamic_cast<Fl_Group *>(wd);
            if (!g) return OpOutcome::fail("bad_arg: path leaves the widget tree");
            const int idx = (int)step.getInt();
            if (idx < 0 || idx >= g->children())
                return OpOutcome::fail("bad_arg: path index out of range");
            wd = g->child(idx);
        }
        const std::string &ev = a.at("event").getString();
        if (ev == "click") {
            const FltkEventStateGuard saved; // 进入前保存，退出前（含异常）恢复
            (void)saved;                     // /W4：只用析构函数，显式标记已用
            const int cx = wd->x() + wd->w() / 2;
            const int cy = wd->y() + wd->h() / 2;
            Fl::e_x = cx; Fl::e_y = cy;
            Fl::e_state = FL_BUTTON1;
            wd->handle(FL_PUSH);
            wd->handle(FL_RELEASE);
            return OpOutcome::success(OpValue::makeBool(true));
        }
        if (ev == "type") {
            Fl_Input *in = dynamic_cast<Fl_Input *>(wd);
            if (!in) return OpOutcome::fail("bad_arg: type needs an input widget");
            const std::string &txt = a.count("text") ? a.at("text").getString() : "";
            std::string cur = in->value() ? in->value() : "";
            cur += txt;
            in->value(cur.c_str());
            return OpOutcome::success(OpValue::makeBool(true));
        }
        return OpOutcome::fail("bad_arg: event must be click or type");
    }});

    reg.add({"ui.wait",
             "Wait for a script dialog to appear (id=0 + title) or disappear (id>0). Returns {seen:true|false}. Caps at timeout_ms (default 2000, max 5000).",
             {{"title", "string?", "dialog title to wait for"},
              {"id", "int?", "existing dialog id that should vanish"},
              {"timeout_ms", "int?", "wait budget"}},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        // The wait pumps the event loop (Fl::wait below) so a dialog can
        // actually APPEAR while we wait. Packets queued by another thread
        // are NOT executed during that pump - the UI dispatch queue above
        // only drains outside a running handler - so the pump cannot run a
        // second op nested inside this one.
        const std::string title = a.count("title") ? a.at("title").getString() : "";
        const long long id = a.count("id") ? a.at("id").getInt() : 0;
        long long ms = a.count("timeout_ms") ? a.at("timeout_ms").getInt() : 2000;
        if (ms <= 0) ms = 2000;
        if (ms > 5000) ms = 5000;
        if (title.empty() && id <= 0) return OpOutcome::fail("bad_arg: title or id required");
        const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)ms;
        bool seen = false;
        for (;;) {
            if (!title.empty()) {
                seen = WidgetRegistry::instance().findByTitle(title.c_str()) != nullptr;
            } else {
                // "wait until gone": gone = unregistered OR hidden
                WidgetRegistry::Entry *t =
                    WidgetRegistry::instance().find((int)id);
                seen = !t || !t->win->visible();
            }
            if (seen) break;
            if (GetTickCount64() >= deadline) break;
            Fl::wait(0.05);
        }
        OpValue m = OpValue::makeMap();
        m.set("seen", OpValue::makeBool(seen));
        return OpOutcome::success(std::move(m));
    }});
}

void MainWindow::unregisterHostOps()
{
    // The registry is a process singleton; exactly one MainWindow owns the
    // product ops at a time. Withdraw THIS window's ops (the owner token set
    // by registerHostOps) so a later window/test can never dispatch into
    // handlers that captured a destroyed MainWindow. Ops belonging to another
    // owner stay live - removeAll() would wipe those too.
    HostRegistry::instance().removeOwner(this);
    // The default owner pointed at the window that is going away - but only
    // clear it if it is still OURS: a window that replaced us may already
    // have set the default owner to itself, and clearing that would leave
    // its later registrations without an owner.
    if (HostRegistry::instance().defaultOwner() == this)
        HostRegistry::instance().setDefaultOwner(nullptr);
    // The executor and the gate belong to the WINDOW that installed them.
    // Drop them only when no window is left: another window may still be
    // alive, and a null executor would run its handlers on the caller's
    // thread (the MCP thread) - touching UI state without the UI thread.
    if (HostRegistry::instance().snapshot().empty()) {
        HostRegistry::instance().setUiExecutor(nullptr);
        HostRegistry::instance().setWriteGate(nullptr);
    }
    // Answer the packets that are still queued: their op is gone, so they
    // fail with "not_found" right away instead of waiting out the 30 s
    // dispatch timeout. (Draining here is safe - the ops are already
    // retired, so the handlers never touch this window.)
    drainUiQueue(nullptr);
}
