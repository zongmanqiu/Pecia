// test_host_ops.cpp - HostOps registry contract (m020).
// Pure logic: no FLTK widgets, no MainWindow. Guards the single-source
// rules the Lua/MCP projections depend on: deterministic snapshot order,
// duplicate-registration refusal, argument shape validation, permission
// gate fail-safe, executor marshalling, per-op stats.
#include "core/HostOps.h"

#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static int g_fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { ++g_fails; \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static OpOutcome echoHandler(const std::map<std::string, OpValue> &a)
{
    auto it = a.find("text");
    if (it == a.end()) return OpOutcome::fail("bad_arg: missing text");
    return OpOutcome::success(it->second);
}

static void test_snapshot_order_and_find()
{
    HostRegistry r;
    r.add({"zeta", "", {{"text", "string", "t"}}, OpPerm::Read, echoHandler});
    r.add({"alpha", "", {}, OpPerm::Read, echoHandler});
    r.add({"mid.b", "", {}, OpPerm::Read, echoHandler});
    auto snap = r.snapshot();
    CHECK(snap.size() == 3);
    CHECK(snap[0]->name == "alpha");
    CHECK(snap[1]->name == "mid.b");
    CHECK(snap[2]->name == "zeta");
    CHECK(r.find("mid.b") != nullptr);
    CHECK(r.find("nope") == nullptr);
}

static void test_duplicate_add_is_skipped()
{
    HostRegistry r;
    r.add({"dup", "first", {}, OpPerm::Read, echoHandler});
    r.add({"dup", "second", {}, OpPerm::Read, echoHandler});
    CHECK(r.snapshot().size() == 1);
    CHECK(r.find("dup")->description == "first");   // last must NOT win
}

static void test_arg_shape_validation()
{
    HostRegistry r;
    r.add({"needs", "", {{"text", "string", "t"}, {"n", "int?", "num"}},
           OpPerm::Read, echoHandler});
    CHECK(r.dispatch("nope", {}).err.rfind("not_found:", 0) == 0);   // sanity
    {
        auto o = r.dispatch("needs", {});
        CHECK(!o.ok);
        CHECK(o.err.rfind("bad_arg: missing", 0) == 0);
    }
    {
        std::map<std::string, OpValue> a;
        a["text"] = OpValue::makeStr("x");
        a["n"] = OpValue::makeStr("not-an-int");
        auto o = r.dispatch("needs", a);
        CHECK(!o.ok);
        CHECK(o.err.rfind("bad_arg:", 0) == 0);
    }
    {
        std::map<std::string, OpValue> a;
        a["text"] = OpValue::makeStr("x");
        a["surprise"] = OpValue::makeInt(1);   // unknown key must be rejected
        auto o = r.dispatch("needs", a);
        std::fprintf(stderr, "unknown-key err=[%s]\n", o.err.c_str());
        CHECK(!o.ok);
        CHECK(o.err.find("unknown") != std::string::npos);
    }
    {
        std::map<std::string, OpValue> a;
        a["text"] = OpValue::makeStr("ok");
        auto o = r.dispatch("needs", a);
        CHECK(o.ok);
        CHECK(o.value.getString() == "ok");
    }
}

// dispatch() is documented to accept a null/zero-length declaration type:
// it defaults to "string", exactly like the MCP tools/list projection. Both
// the acceptance path and the rejection message used to dereference that
// null pointer (std::string(nullptr) / operator+(nullptr)).
static void test_null_arg_type_is_safe()
{
    HostRegistry r;
    OpDef def;
    def.name = "nulltype";
    def.perm = OpPerm::Read;
    def.args.push_back({"value", nullptr, "no declared type -> string"});
    def.handler = [](const std::map<std::string, OpValue> &a) {
        auto it = a.find("value");
        return OpOutcome::success(it == a.end() ? OpValue::makeBool(false)
                                                : it->second);
    };
    r.add(std::move(def));

    std::map<std::string, OpValue> good;
    good["value"] = OpValue::makeStr("ok");
    CHECK(r.dispatch("nulltype", good).ok);     // null decl defaults to string

    std::map<std::string, OpValue> bad;
    bad["value"] = OpValue::makeInt(7);
    const OpOutcome o = r.dispatch("nulltype", bad);
    CHECK(!o.ok);
    CHECK(o.err == "bad_arg: 'value' expects string");   // no nullptr concat
}

static void test_perm_gate_fail_safe()
{
    HostRegistry r;
    r.add({"w", "", {{"text", "string?", "t"}}, OpPerm::Write, echoHandler});
    // no gate installed => writes DENY (fail-safe, not fail-open)
    auto o = r.dispatch("w", {});
    CHECK(!o.ok);
    CHECK(o.err.rfind("perm_denied:", 0) == 0);

    r.setWriteGate([] { return true; });
    {
        std::map<std::string, OpValue> a;
        a["text"] = OpValue::makeStr("through");
        auto o2 = r.dispatch("w", a);
        CHECK(o2.ok);
    }

    // handler must NOT have run on the denied call: prove via run flag
    static bool ran = false;
    ran = false;
    r.add({"e", "", {}, OpPerm::Exec, [](const std::map<std::string, OpValue> &) {
        ran = true; return OpOutcome::success(OpValue::makeBool(true));
    }});
    r.setWriteGate([] { return false; });
    auto o3 = r.dispatch("e", {});
    CHECK(!o3.ok);
    CHECK(!ran);
}

static void test_executor_and_stats()
{
    HostRegistry r;
    r.add({"s", "", {{"text", "string", "t"}}, OpPerm::Read, echoHandler});
    r.add({"bad", "", {{"text", "string", "t"}}, OpPerm::Read,
           [](const std::map<std::string, OpValue> &) -> OpOutcome {
        throw std::runtime_error("boom");
    }});

    int marshalled = 0;
    r.setUiExecutor([&marshalled](void (*fn)(void *), void *arg) {
        ++marshalled;
        fn(arg);   // synchronous stand-in for awake+cv
    });

    std::map<std::string, OpValue> a;
    a["text"] = OpValue::makeStr("v");
    CHECK(r.dispatch("s", a).ok);
    CHECK(marshalled == 1);

    auto o = r.dispatch("bad", a);
    CHECK(!o.ok);
    CHECK(o.err.rfind("internal:", 0) == 0);   // exception swallowed at seam

    auto st = r.stats("s");
    CHECK(st.calls == 1 && st.failures == 0);
    auto st2 = r.stats("bad");
    CHECK(st2.calls == 1 && st2.failures == 1);
}

static void test_values_roundtrip()
{
    OpValue m = OpValue::makeMap();
    m.set("a", OpValue::makeInt(1));
    OpValue lst = OpValue::makeList();
    lst.push(OpValue::makeBool(true));
    lst.push(OpValue::makeStr("s"));
    m.set("l", lst);
    CHECK(m.find("a")->getInt() == 1);
    const OpValue *l = m.find("l");
    CHECK(l && l->size() == 2 && l->list()[1].getString() == "s");
    // missing keys and wrong-kind finds never crash
    CHECK(m.find("zz") == nullptr);
    OpValue scal = OpValue::makeInt(5);
    CHECK(scal.find("a") == nullptr);
    CHECK(scal.size() == 0);
}

// ---------------------------------------------------------------------------
// The late tick. A marshalled call that times out returns while the request is
// still in the queue; when the UI thread finally runs it the caller is gone -
// here a worker thread whose stack is unmapped at join(), taking the argument
// map with it. So the packet must OWN its arguments and be reference-counted
// across the hand-off, not point at the caller's frame.
// ---------------------------------------------------------------------------
static std::string gLateSaw;

static void test_late_tick_after_timeout()
{
    HostRegistry r;
    r.add({"late", "", {{"text", "string", "t"}}, OpPerm::Read,
           [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        auto it = a.find("text");
        gLateSaw = (it != a.end()) ? it->second.getString() : std::string("<missing>");
        return OpOutcome::success(OpValue::makeBool(true));
    }});

    std::vector<std::pair<void (*)(void *), void *>> queued;
    r.setUiWaitSeconds(0.05);
    r.setUiExecutor([&queued](void (*fn)(void *), void *arg) {
        queued.push_back(std::make_pair(fn, arg));   // deferred, like Fl::awake
    });

    std::string err = "unset";
    {
        std::thread worker([&r, &err] {
            std::map<std::string, OpValue> a;
            a["text"] = OpValue::makeStr("copied-me");
            const OpOutcome o = r.dispatch("late", a);
            err = o.err;
        });
        worker.join();     // `a` and the packet's old stack home die here
    }

    CHECK(err.rfind("internal: ui thread timeout", 0) == 0);
    CHECK(queued.size() == 1);
    if (queued.empty()) { r.setUiExecutor(nullptr); return; }

    gLateSaw.clear();
    for (auto &p : queued) p.first(p.second);        // the UI thread arriving late
    CHECK(gLateSaw == "copied-me");                  // read from the packet's copy
    CHECK(r.stats("late").calls == 1);
    r.setUiExecutor(nullptr);
}

// M4 red-proof (m026 audit): the write gate used to be evaluated on the
// DISPATCHER's thread (e.g. the MCP stdio thread) while the closure it calls
// walks m_tabsList / doc->isEditable() - UI state that the UI thread mutates
// without any lock: an unlocked data race on every write dispatch. The gate
// must be evaluated where that state lives, on the executor (UI) thread.
// Deterministic: with an executor that hands the packet to a worker thread,
// the gate's thread id must be the worker's, never the dispatcher's.
static void test_write_gate_evaluated_on_ui_thread()
{
    HostRegistry r;
    r.add({"w", "", {{"text", "string?", "t"}}, OpPerm::Write, echoHandler});

    std::thread::id gateTid, dispatchTid, workerTid;
    r.setWriteGate([&] { gateTid = std::this_thread::get_id(); return true; });

    // Executor that runs the packet on its own (stand-in UI) thread.
    r.setUiExecutor([&workerTid](void (*fn)(void *), void *arg) {
        std::thread worker([fn, arg, &workerTid]() {
            workerTid = std::this_thread::get_id();
            fn(arg);
        });
        worker.join();   // synchronous stand-in for awake+cv
    });

    std::map<std::string, OpValue> a;
    a["text"] = OpValue::makeStr("gate");
    dispatchTid = std::this_thread::get_id();
    auto o = r.dispatch("w", a);
    std::fprintf(stderr, "dispatch err=[%s] gateTidSet=%d workerTidSet=%d\n",
                 o.err.c_str(), gateTid != std::thread::id{}, workerTid != std::thread::id{});
    CHECK(o.ok);
    CHECK(gateTid != std::thread::id{});        // the gate actually ran
    CHECK(gateTid == workerTid);                // ... on the executor thread
    CHECK(gateTid != dispatchTid);              // ... never on the caller's
    r.setUiExecutor(nullptr);
    r.setWriteGate(nullptr);
}

int main()
{
    test_snapshot_order_and_find();
    test_duplicate_add_is_skipped();
    test_arg_shape_validation();
    test_null_arg_type_is_safe();
    test_perm_gate_fail_safe();
    test_executor_and_stats();
    test_write_gate_evaluated_on_ui_thread();
    test_late_tick_after_timeout();
    test_values_roundtrip();
    std::fprintf(stderr, "%d failures\n", g_fails);
    return g_fails ? 1 : 0;
}
