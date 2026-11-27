// HostOps.h - the single source of truth for host capabilities.
//
// Every operation the host exposes (to Lua scripts today, to the MCP
// endpoint in m021, to future tooling) is REGISTERED HERE ONCE: name,
// description, permission class, and handler. Lua bindings and the MCP
// tools/list projection are generated from this registry - a second list
// of the same facts is forbidden (m020 design rule).
//
// Threading: handlers run on the UI thread only. Core code and worker
// threads call HostRegistry::dispatch(), which marshals through the
// executor installed by the UI layer (the in-process host-op queue's
// awake+cv pattern) and enforces the permission gate centrally: Write
// operations fail with PERM_DENIED while the active document is
// edit-locked / read-only, before any handler runs.
//
// No FLTK includes here (core-layer rule).
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Self-describing scalar/list/map value. Integers only (MCP tools in
// v1 need no floats); strings are UTF-8. Deliberately NOT called Json*:
// m021 builds its JSON layer on top of this type.
class OpValue {
public:
    enum class Kind { Null, Bool, Int, Str, List, Map };

    OpValue() : kind_(Kind::Null) {}
    static OpValue makeBool(bool b)            { OpValue v; v.kind_ = Kind::Bool; v.b_ = b; return v; }
    static OpValue makeInt(long long i)        { OpValue v; v.kind_ = Kind::Int;  v.i_ = i; return v; }
    static OpValue makeStr(std::string s)      { OpValue v; v.kind_ = Kind::Str;  v.s_ = std::move(s); return v; }
    static OpValue makeList()                  { OpValue v; v.kind_ = Kind::List; return v; }
    static OpValue makeMap()                   { OpValue v; v.kind_ = Kind::Map;  return v; }

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool getBool(bool def = false) const { return kind_ == Kind::Bool ? b_ : def; }
    long long getInt(long long def = 0) const { return kind_ == Kind::Int ? i_ : def; }
    const std::string &getString() const { static const std::string e; return kind_ == Kind::Str ? s_ : e; }

    // List access (no-op on other kinds)
    void push(OpValue v) { if (kind_ == Kind::List) l_.push_back(std::move(v)); }
    bool isList() const { return kind_ == Kind::List; }
    bool isMap() const { return kind_ == Kind::Map; }
    size_t size() const { return kind_ == Kind::List ? l_.size() : (kind_ == Kind::Map ? m_.size() : 0); }
    const std::vector<OpValue> &list() const { return l_; }

    // Map access (no-op on other kinds). Keys are exact, case-sensitive.
    void set(const std::string &key, OpValue v) { if (kind_ == Kind::Map) m_[key] = std::move(v); }
    const OpValue *find(const std::string &key) const
    {
        if (kind_ != Kind::Map) return nullptr;
        auto it = m_.find(key);
        return it == m_.end() ? nullptr : &it->second;
    }
    const std::map<std::string, OpValue> &map() const { return m_; }

private:
    Kind kind_;
    bool b_ = false;
    long long i_ = 0;
    std::string s_;
    std::vector<OpValue> l_;
    std::map<std::string, OpValue> m_;
};

// Permission class. Read needs nothing; Write is gated on the active
// document being editable (isEditable(): read-only flag OR preview lock);
// Exec runs scripts and is always gated the same way as Write.
//
// Unset is NOT a permission class: it marks "the registrant did not declare
// one". The write gate is the ONLY enforcement point of the whole host
// capability set, so an op whose class was forgotten must never be treated
// as Read - that is the fail-OPEN direction. add() rejects Unset ops
// instead of registering them (see HostOps.cpp).
enum class OpPerm { Read, Write, Exec, Unset };

struct OpOutcome {
    bool ok = false;
    OpValue value;
    // Machine-readable reason first, optional detail after ':'.
    // Known prefixes: bad_arg: perm_denied: not_found: internal:
    std::string err;
    static OpOutcome success(OpValue v) { OpOutcome o; o.ok = true; o.value = std::move(v); return o; }
    static OpOutcome fail(const std::string &e) { OpOutcome o; o.err = e; return o; }
};

struct OpArg {
    std::string name;          // value (was const-ref-to-temporary: dangled)
    const char *type;        // "int" | "string" | "bool" | "string?" ... ('?' = optional)
    const char *description; // becomes lua_api.txt / MCP schema text verbatim
};

struct OpDef {
    std::string name;          // dotted, e.g. "doc.get_text"
    std::string description;
    std::vector<OpArg> args;   // references must outlive the registry entry:
                               // use string literals / static storage
    OpPerm perm = OpPerm::Unset;   // MUST be declared (Read / Write / Exec).
                                   // The default is Unset, not Read: an op
                                   // that forgets its class can never slip
                                   // past the write gate.
    std::function<OpOutcome(const std::map<std::string, OpValue> &)> handler;
};

using UiExecutor = std::function<void(void (*)(void *), void *)>;

class HostRegistry {
public:
    // Register once at startup.
    //
    // Duplicate names: the SAME owner registering a name twice is a hard
    // programming error - reported to stderr and the entry is SKIPPED (last
    // registration does not win - silently shadowing an op would desync the
    // Lua/MCP views).
    //
    // A DIFFERENT owner registering the same name is allowed: several
    // windows can be alive in one process (the product is single-window, but
    // the gates construct one window after another), and the newest live
    // owner of a name is the one that serves it. When that owner goes away
    // its entries are retired and the previous owner's entries serve again.
    // find() and snapshot() therefore expose ONE entry per name - the newest
    // live owner's - so the projections stay a single source of truth.
    //
    // An op is rejected (never inserted) when it has no name, no handler,
    // or no declared permission class.
    //
    // owner: optional registration owner - the object whose lifetime the
    // handlers belong to (e.g. the MainWindow). Ops with an owner are
    // withdrawn by removeOwner(owner) only, so one window's teardown stops
    // wiping another window's ops. nullptr => use the current default
    // owner (see setDefaultOwner).
    void add(OpDef def, void *owner = nullptr);

    // Owner applied to add() calls that do not pass one explicitly. The UI
    // layer sets it to `this` before registering its ops, so the whole set
    // can be withdrawn with removeOwner(this) at teardown.
    void setDefaultOwner(void *owner);

    // The owner add() would use right now (nullptr when none is set). Lets a
    // teardown clear the default owner only if it is still its own - a
    // window that goes away must not clear the default owner of the window
    // that replaced it.
    const void *defaultOwner() const;

    // Remove every op registered for this owner (see add). Ops of other
    // owners stay live.
    void removeOwner(void *owner);

    // Remove ALL ops, every owner. Used by MainWindow teardown so a second
    // window/test never sees handlers pointing at a destroyed object.
    void removeAll();

    // Deterministic projection order (name ascending, byte-compare).
    // m021's tools/list equality gate relies on this stability.
    //
    // LIFETIME: the returned pointers stay valid for the lifetime of the
    // registry. Entries are RETIRED (un-published) by removeAll() /
    // removeOwner(), never freed, so a reader that still holds a pointer
    // cannot dereference freed memory. OpDef contents are immutable after
    // add().
    std::vector<const OpDef *> snapshot() const;

    // Same lifetime rule as snapshot(): the pointer stays valid until the
    // registry dies, even after the op was retired.
    const OpDef *find(const std::string &name) const;
    const OpDef *findLocked(const std::string &name) const;  // caller holds mu_

    // Install how work reaches the UI thread. Passing an empty executor
    // restores direct (same-thread) call mode.
    void setUiExecutor(UiExecutor exec);
    void setWriteGate(std::function<bool()> gate);  // true => writes allowed

    // Evaluate the write gate. UI-THREAD ONLY: the closure reads UI state
    // (active tab, document editability) that the UI thread mutates without
    // a lock, so calling it from any other thread is a data race. dispatch
    // routes every Write/Exec op through this on the executor thread instead.
    // No gate installed => false: an un-armed host must fail safe, not fail
    // open.
    bool evalWriteGateUi();

    // How long a marshalled call waits for the UI thread before answering
    // "internal: ui thread timeout". Default 30 s (production); the gate in
    // test_host_ops shortens it so the late-tick path is reachable in
    // milliseconds instead of half a minute.
    // Atomic: the test/UI side may retune the timeout while dispatch() on a
    // worker thread is reading it; a plain double would be a data race.
    void setUiWaitSeconds(double seconds) { uiWaitSeconds_.store(seconds > 0 ? seconds : 30.0); }

    // Lookup, argument presence/shape validation, permission gate, then
    // execute via the UI executor. Never throws.
    // Marshalled calls wait up to 30 s; on timeout the answer is
    // "internal: ui thread timeout" and the request **may still run later**
    // (the queue cannot be cancelled). The packet is shared between both
    // sides, so a late tick cannot touch freed memory - callers must not
    // assume a timeout means the operation never happened. The registry must
    // outlive a queued call: true for the process singleton, and a local
    // registry in a test has to drain its queue before it goes away.
    //
    // THREADING: everything the marshalled call needs is resolved and
    // COPIED under mu_ before the lock is released - no pointer into the
    // registry is dereferenced after that point (the registry may be torn
    // down by a concurrent removeAll() while the call is in flight).
    OpOutcome dispatch(const std::string &name, const std::map<std::string, OpValue> &args);

    // Lightweight per-op counters (call count, failures) for diagnostics.
    struct Stats { unsigned long calls = 0; unsigned long failures = 0; };
    Stats stats(const std::string &name) const;

    static HostRegistry &instance();

private:
    struct Entry {
        OpDef def;
        Stats stats;
        const void *owner = nullptr;
    };
    mutable std::mutex mu_;
    // arena_ OWNS every Entry ever created and releases none before the
    // registry dies: find()/snapshot() hand out raw pointers into it, and a
    // reader may still hold one after the entry was retired. entries_ is the
    // LIVE index (sorted by name); it says which entries are published.
    std::vector<std::unique_ptr<Entry>> arena_;
    std::vector<Entry *> entries_;            // live index, sorted by name
    const void *defaultOwner_ = nullptr;
    UiExecutor executor_;
    std::function<bool()> writeGate_;
    std::atomic<double> uiWaitSeconds_{30.0};
};
