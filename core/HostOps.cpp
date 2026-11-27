// HostOps.cpp - registry storage, validation, permission gate, marshalling.
// See HostOps.h for the contract.
#include "core/HostOps.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>

// ---------------------------------------------------------------------------
// value helpers
// ---------------------------------------------------------------------------

static bool argTypeAccepts(const char *decl, const OpValue &v)
{
    // dispatch() accepts a null declaration type; it means the same empty
    // string as "" below (default "string"), so never construct a std::string
    // from the raw pointer.
    std::string t(decl ? decl : "");
    bool optional = !t.empty() && t.back() == '?';
    std::string base = optional ? t.substr(0, t.size() - 1) : t;
    if (optional && v.isNull()) return true;
    // An EMPTY declared type means "string" - the same default the MCP schema
    // projection uses (agent/McpServer.cpp). The two must agree: if the
    // projection advertised a type the registry then refused, the tool would
    // look callable and fail every call with bad_arg. A type that is spelled
    // but unknown ("strng") is still a registration bug and is refused.
    if (base.empty()) base = "string";
    if (base == "int")    return v.kind() == OpValue::Kind::Int;
    if (base == "string") return v.kind() == OpValue::Kind::Str;
    if (base == "bool")   return v.kind() == OpValue::Kind::Bool;
    if (base == "list")   return v.kind() == OpValue::Kind::List;
    if (base == "map")    return v.kind() == OpValue::Kind::Map;
    return false;   // unknown declaration type is a registration bug, not user input
}

// ---------------------------------------------------------------------------
// registry
// ---------------------------------------------------------------------------

HostRegistry &HostRegistry::instance()
{
    static HostRegistry r;
    return r;
}

void HostRegistry::add(OpDef def, void *owner)
{
    // Validation FIRST, in the order the rules are stated: an op without a
    // name, without a handler, or without a DECLARED permission class is a
    // registration bug and must never reach the index. The old code ran the
    // duplicate check before these tests, which meant an empty name was
    // compared against the index (and reported as a duplicate) instead of
    // being rejected as the broken registration it is.
    if (def.name.empty()) {
        std::fprintf(stderr, "HostRegistry: op with empty name ignored\n");
        return;
    }
    if (!def.handler) {
        std::fprintf(stderr, "HostRegistry: op '%s' with empty handler ignored\n",
                     def.name.c_str());
        return;
    }
    // The permission class is the ONLY enforcement point of the write gate.
    // Defaulting it to Read would fail OPEN: an op that forgets its class
    // would never be gated and could write into a read-only document. The
    // safe direction is to refuse the registration instead - the author has
    // to state what the op does.
    if (def.perm == OpPerm::Unset) {
        std::fprintf(stderr,
                     "HostRegistry: op '%s' ignored: permission class not declared "
                     "(pass OpPerm::Read / Write / Exec)\n",
                     def.name.c_str());
        return;
    }

    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::lower_bound(entries_.begin(), entries_.end(), def.name,
        [](const Entry *e, const std::string &name) { return e->def.name < name; });
    // The entries of one name form a contiguous group (entries_ is sorted by
    // name); inside the group the newest registration is LAST.
    auto groupEnd = it;
    while (groupEnd != entries_.end() && (*groupEnd)->def.name == def.name)
        ++groupEnd;
    const void *own = owner ? static_cast<const void *>(owner) : defaultOwner_;
    for (auto p = it; p != groupEnd; ++p) {
        if ((*p)->owner == own) {
            // Same owner, same name: a hard programming error. Skip and shout.
            // Letting the last add win would leave Lua and MCP projecting
            // different handlers than the first registrant intended.
            std::fprintf(stderr, "HostRegistry: duplicate op '%s' ignored\n",
                         def.name.c_str());
            return;
        }
    }
    // A different owner may register the same name: several windows can be
    // alive at once, and the newest live owner serves the name (see
    // HostOps.h). The new entry goes LAST in the name group so "newest" is
    // simply "last within the group".
    // The entry is owned by the arena for the registry's whole life: readers
    // may still hold a pointer to it after it was retired (see removeAll).
    std::unique_ptr<Entry> owned(new Entry());
    owned->def = std::move(def);
    owned->owner = own;
    Entry *e = owned.get();
    arena_.push_back(std::move(owned));
    entries_.insert(groupEnd, e);
}

void HostRegistry::setDefaultOwner(void *owner)
{
    std::lock_guard<std::mutex> lk(mu_);
    defaultOwner_ = owner;
}

const void *HostRegistry::defaultOwner() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return defaultOwner_;
}

void HostRegistry::removeOwner(void *owner)
{
    std::lock_guard<std::mutex> lk(mu_);
    const void *own = owner;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [own](const Entry *e) { return e->owner == own; }),
                   entries_.end());
    // The retired entries stay in the arena: a reader may still hold a
    // pointer to them (find()/snapshot() hand out raw pointers).
}

void HostRegistry::removeAll()
{
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();   // retire, do not free: see arena_ in HostOps.h
}

std::vector<const OpDef *> HostRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<const OpDef *> out;
    out.reserve(entries_.size());
    // ONE entry per name: the newest live owner's. entries_ is sorted by
    // name and the newest registration of a name is the last one in its
    // group, so this keeps the first name and takes the last of each group.
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry *e = entries_[i];
        const bool lastOfName = (i + 1 == entries_.size()) ||
                                (entries_[i + 1]->def.name != e->def.name);
        if (lastOfName) out.push_back(&e->def);
    }
    return out;   // already sorted by name (insertion keeps the order)
}

const OpDef *HostRegistry::findLocked(const std::string &name) const
{
    auto it = std::lower_bound(entries_.begin(), entries_.end(), name,
        [](const Entry *e, const std::string &n) { return e->def.name < n; });
    if (it == entries_.end() || (*it)->def.name != name) return nullptr;
    // The newest live owner of this name serves it (the last entry of the
    // name group); when it is retired the previous owner serves again.
    const Entry *winner = *it;
    for (auto p = it; p != entries_.end() && (*p)->def.name == name; ++p)
        winner = *p;
    return &winner->def;
}

const OpDef *HostRegistry::find(const std::string &name) const
{
    std::lock_guard<std::mutex> lk(mu_);
    return findLocked(name);
}

void HostRegistry::setUiExecutor(UiExecutor exec)
{
    std::lock_guard<std::mutex> lk(mu_);
    executor_ = std::move(exec);
}

void HostRegistry::setWriteGate(std::function<bool()> gate)
{
    std::lock_guard<std::mutex> lk(mu_);
    writeGate_ = std::move(gate);
}

// Evaluate the write gate. UI-THREAD ONLY (called from applyPacket): the
// closure reads UI state that the UI thread mutates without a lock. No gate
// installed => false: an un-armed host must fail safe, not fail open.
bool HostRegistry::evalWriteGateUi()
{
    std::function<bool()> g;
    {
        std::lock_guard<std::mutex> lk(mu_);
        g = writeGate_;
    }
    return g ? g() : false;
}

HostRegistry::Stats HostRegistry::stats(const std::string &name) const
{
    std::lock_guard<std::mutex> lk(mu_);
    for (const Entry *e : entries_)
        if (e->def.name == name) return e->stats;
    return Stats{};
}

// The hand-off between a calling thread and the UI thread. It is shared, not
// borrowed: the dispatcher gives up after the timeout below and its frame
// dies, while the UI thread may still run the request minutes later (a modal
// dialog, a long script). A stack packet would then be written into dead
// memory - the gate test_late_tick_after_timeout segfaults on that shape.
struct RunPacket {
    HostRegistry *reg = nullptr;        // re-resolves the op on the UI thread
    std::string name;
    std::map<std::string, OpValue> args;    // copied: the caller may be gone
    bool needsGate = false;             // perm != Read: gate runs on the UI thread
    OpOutcome outcome{OpOutcome::fail("internal: not run")};
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
};

using RunPacketRef = std::shared_ptr<RunPacket>;

// Runs the request and publishes the verdict. Never throws out.
//
// LOCK ORDER: the registry lock (mu_) is NEVER taken while pk.m is held,
// and pk.m is never taken while mu_ is held - the two mutexes are never
// nested, in either direction, so there is no ABBA cycle between the
// dispatcher's thread and the UI thread.
static void applyPacket(RunPacket &pk)
{
    // Look the handler up NOW, on the UI thread: the window that registered it
    // may have closed in the meantime, and every handler closure captures it.
    // The pointer stays valid even if the op was retired meanwhile (entries
    // are retired, never freed - see HostOps.h).
    const OpDef *def = pk.reg ? pk.reg->find(pk.name) : nullptr;
    std::unique_lock<std::mutex> lk(pk.m);
    if (!def) {
        pk.outcome = OpOutcome::fail("not_found: host unregistered before the call ran");
    } else {
        lk.unlock();
        try {
            // M4 (m026 audit): the write gate reads UI state (m_tabsList,
            // doc->isEditable()), so it is evaluated HERE on the UI thread -
            // never on the dispatcher's thread, where that state would be
            // read without any lock while the UI thread mutates it.
            // Re-derive the gate from the handler we are ABOUT TO RUN, not the
            // perm snapshotted at dispatch: the same name can be re-registered
            // with a different perm between dispatch and UI execution. Using
            // the stale snapshot could run a Write handler with a Read op's
            // no-gate pass. Fail-safe: if anything disagrees, gate it.
            bool needsGateHere = pk.needsGate || (def->perm != OpPerm::Read);
            if (needsGateHere && !pk.reg->evalWriteGateUi()) {
                pk.outcome = OpOutcome::fail(
                    "perm_denied: document is read-only or preview-locked");
            } else {
                pk.outcome = def->handler(pk.args);
            }
        } catch (const std::exception &e) {
            pk.outcome = OpOutcome::fail(std::string("internal: ") + e.what());
        } catch (...) {
            pk.outcome = OpOutcome::fail("internal: unknown exception");
        }
        lk.lock();
    }
    pk.done = true;
    pk.cv.notify_all();
}

static void runOnUi(void *p)
{
    // Owns the queue's reference; frees the packet even if the waiter left.
    const std::unique_ptr<RunPacketRef> held(static_cast<RunPacketRef *>(p));
    if (!held || !*held) return;
    applyPacket(**held);
}

OpOutcome HostRegistry::dispatch(const std::string &name,
                                 const std::map<std::string, OpValue> &args)
{
    // Resolve everything under ONE short lock scope, then RELEASE before
    // marshalling: the UI wait below can last seconds and holding mu_ across
    // it would block every other dispatcher (and the first version here had
    // a literal self-deadlock: a nested grab of this same non-recursive
    // mutex, caught by test_host_ops).
    //
    // Everything the marshalled call needs is COPIED HERE, inside the locked
    // scope: after mu_ is released the registry may be torn down under us
    // (removeAll() from ~MainWindow while an MCP call is in flight), so no
    // pointer into the registry may be dereferenced past this point. The old
    // code read def->perm AFTER the unlock - a use-after-free whose garbage
    // value could turn needsGate off and SKIP the write gate of a Write op.
    OpPerm perm = OpPerm::Unset;
    UiExecutor exec;
    {
        std::lock_guard<std::mutex> guard(mu_);
        const OpDef *def = findLocked(name);
        if (!def) return OpOutcome::fail("not_found: " + name);

        // argument shape (presence + declared type; unknown keys rejected so
        // the declared schema is the whole truth for Lua and MCP alike)
        for (const auto &a : def->args) {
            auto it = args.find(a.name);
            bool optional = a.type && *a.type && a.type[strlen(a.type) - 1] == '?';
            if (it == args.end()) {
                if (!optional) return OpOutcome::fail("bad_arg: missing '" + a.name + "'");
                continue;
            }
            if (!argTypeAccepts(a.type, it->second))
                // a.type may be nullptr (empty declaration => "string"); do
                // not concatenate a null char* into std::string.
                return OpOutcome::fail("bad_arg: '" + a.name + "' expects " +
                                       (a.type ? a.type : "string"));
        }
        for (const auto &kv : args) {
            bool known = false;
            for (const auto &a : def->args) if (a.name == kv.first) { known = true; break; }
            if (!known) return OpOutcome::fail("bad_arg: unknown '" + kv.first + "'");
        }

        perm = def->perm;          // copied, not referenced
        exec = executor_;
        for (Entry *e : entries_) if (e->def.name == name) { e->stats.calls++; break; }
    }   // mu_ released before the UI wait

    auto pk = std::make_shared<RunPacket>();
    pk->reg = this;
    pk->name = name;
    pk->args = args;
    // M4 (m026 audit): the gate is NOT evaluated here on the caller's thread -
    // it reads UI state the UI thread mutates without a lock. The verdict
    // happens in applyPacket, on the thread that owns that state. Same
    // fail-safe semantics: no gate installed => writes denied.
    pk->needsGate = (perm != OpPerm::Read);
    if (exec) {
        // The queue carries its own reference, so a dispatcher that gives up
        // here cannot leave the UI thread writing into freed memory.
        exec(&runOnUi, new RunPacketRef(pk));
        OpOutcome out;
        {
            std::unique_lock<std::mutex> lk(pk->m);
            if (!pk->cv.wait_for(lk, std::chrono::duration<double>(uiWaitSeconds_.load()),
                                 [&] { return pk->done; }))
                return OpOutcome::fail("internal: ui thread timeout");
            out = pk->outcome;   // copied under the packet lock
        }   // pk->m released BEFORE mu_ is taken again: the two locks are
            // never nested in the opposite order anywhere in this file.
        if (!out.ok) {
            std::lock_guard<std::mutex> guard2(mu_);
            for (Entry *e : entries_) if (e->def.name == name) { e->stats.failures++; break; }
        }
        return out;
    }

    applyPacket(*pk);   // same-thread mode (unit tests, UI-thread callers)
    OpOutcome outSame;
    {
        std::unique_lock<std::mutex> lk(pk->m);
        outSame = pk->outcome;
    }
    if (!outSame.ok) {
        std::lock_guard<std::mutex> guard2(mu_);
        for (Entry *e : entries_) if (e->def.name == name) { e->stats.failures++; break; }
    }
    return outSame;
}
