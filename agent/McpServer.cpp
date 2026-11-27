// McpServer.cpp - see McpServer.h.
#include "McpServer.h"
#include "McpJson.h"

#include "core/HostOps.h"
#include "core/OpLog.h"   // callback-boundary catch-all logs here (red line 6)

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <io.h>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>   // CommandLineToArgvW
#endif

namespace mcp {
namespace {

const char *const kSupportedVersions[] = {
    "2025-03-26", "2025-06-18", "2025-11-25"
};
const char *const kLatestVersion = "2025-11-25";

bool isSupportedVersion(const std::string &v)
{
    for (const char *s : kSupportedVersions) if (v == s) return true;
    return false;
}

// ---------------------------------------------------------------------------
// framing
// ---------------------------------------------------------------------------

// Binary-safe stdout write: the CRT is forced to O_BINARY at loop entry,
// and we still write via WriteFile so no layer may translate \n -> \r\n
// inside a protocol frame (measured requirement, g5 checklist item 5).
// The payload is written in chunks: a size_t narrowed to a DWORD would
// truncate a frame larger than 4 GiB mid-JSON.
void writeFrame(const std::string &payload)
{
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        const char *p = payload.data();
        size_t done = 0;
        while (done < payload.size()) {
            size_t want = payload.size() - done;
            if (want > (size_t)(1u << 20)) want = (size_t)(1u << 20);   // 1 MiB per write
            DWORD written = 0;
            if (!WriteFile(h, p + done, (DWORD)want, &written, nullptr) || written == 0) break;
            done += (size_t)written;
        }
        return;
    }
#endif
    fwrite(payload.data(), 1, payload.size(), stdout);
    fflush(stdout);
}

// Chunk source for the line reader: reads up to want bytes into dst and
// returns how many it got (0 = EOF or error).
using ChunkSource = std::function<size_t(char *, size_t)>;

// Read one chunk from stdin. ReadFile on the stdin handle returns as soon
// as ANY byte is available (a pipe delivers partial reads), so the reader
// stays responsive; the CRT is only used when there is no usable handle.
size_t readStdinChunk(char *dst, size_t want)
{
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (!ReadFile(h, dst, (DWORD)want, &got, nullptr)) return 0;
        return (size_t)got;
    }
#endif
    return std::fread(dst, 1, want, stdin);
}

// Buffered line reader: one read per chunk, scan for '\n', keep the rest in
// our own buffer. The old loop read ONE BYTE PER SYSCALL (~1.3 MB/s
// measured: a 1 MiB frame cost ~0.8 s of pure read time) and could not tell
// "line too long" from EOF, so a hostile frame closed the whole endpoint.
class LineReader {
public:
    enum class Status { Line, Oversized, Eof };

    explicit LineReader(ChunkSource src) : src_(std::move(src)) {}

    // Diagnostic for the selftest: retained buffer size, must stay bounded
    // even while discarding an endless newline-free stream.
    size_t bufferedBytes() const { return buf_.size(); }

    Status next(std::string &line)
    {
        line.clear();
        for (;;) {
            const size_t nl = buf_.find('\n', pos_);
            if (nl != std::string::npos) {
                // The line ends inside the buffer. Its length is what we
                // have collected PLUS the tail up to the newline - check it
                // before accepting it, or a frame that crosses the limit in
                // the very chunk that carries its newline would slip through.
                if (line.size() + (nl - pos_) > kMaxLineLen) {
                    pos_ = nl + 1;
                    compact();
                    return Status::Oversized;
                }
                line.append(buf_, pos_, nl - pos_);
                pos_ = nl + 1;
                compact();
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;   // skip blank lines
                return Status::Line;
            }
            // No newline in the buffer yet: take it all, then refill.
            line.append(buf_, pos_, buf_.size() - pos_);
            pos_ = buf_.size();
            if (line.size() > kMaxLineLen) {
                // Too long: swallow the rest of the line so its tail is not
                // read as the next request. The endpoint stays OPEN - an
                // oversized frame is a parse error (-32700), not a hangup.
                discardToNewline();
                return Status::Oversized;
            }
            if (!refill()) {
                // EOF: a last line without a newline is still a line.
                if (line.empty()) return Status::Eof;
                if (line.back() == '\r') line.pop_back();
                return Status::Line;
            }
        }
    }

private:
    ChunkSource src_;
    std::string buf_;
    size_t pos_ = 0;
    bool eof_ = false;

    bool refill()
    {
        if (eof_) return false;
        // Reclaim the consumed prefix BEFORE appending. discardToNewline()
        // advances pos_ to the end on every pass with no newline; without
        // this compact the buffer grows 64 KiB per read forever, so a peer
        // streaming endless newline-free bytes could OOM the endpoint even
        // though the line was already declared oversized.
        compact();
        char chunk[64 * 1024];
        const size_t got = src_(chunk, sizeof(chunk));
        if (got == 0) { eof_ = true; return false; }
        buf_.append(chunk, got);
        return true;
    }

    void compact()
    {
        if (pos_ == 0) return;
        buf_.erase(0, pos_);
        pos_ = 0;
    }

    // Drop bytes until the next newline (or EOF).
    void discardToNewline()
    {
        for (;;) {
            const size_t nl = buf_.find('\n', pos_);
            if (nl != std::string::npos) { pos_ = nl + 1; compact(); return; }
            pos_ = buf_.size();
            if (!refill()) return;
        }
    }
};

// ---------------------------------------------------------------------------
// message handling
// ---------------------------------------------------------------------------

std::string resultObj(const std::string &idJson, const std::string &resultJson)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + idJson + ",\"result\":" + resultJson + "}";
}

std::string errorObj(const std::string &idJson, int code, const std::string &msg)
{
    OpValue m = OpValue::makeMap();
    m.set("code", OpValue::makeInt(code));
    m.set("message", OpValue::makeStr(sanitizeText(msg)));
    return "{\"jsonrpc\":\"2.0\",\"id\":" + idJson + ",\"error\":" + jsonEncode(m) + "}";
}

// id text for echoing back: numbers print bare, strings quoted, absent -> null
std::string idJsonOf(const OpValue &msg)
{
    const OpValue *id = msg.find("id");
    if (!id) return "null";
    if (id->kind() == OpValue::Kind::Int) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%lld", id->getInt());
        return buf;
    }
    if (id->kind() == OpValue::Kind::Str) return jsonEncode(*id);
    return "null";   // floats/objects/arrays as ids are invalid (see idKindOf)
}

// Request ids. JSON-RPC 2.0 would also allow NULL, but MCP does not: the
// project's own checklist (g5 A2#9, item 4) says "请求 id MUST NOT 为 null;
// id 为 null 或非字符串/数字 → 按无效请求处理". So only a string or an
// integer id is valid here; a bool / null / map / list id makes the whole
// message an Invalid Request (-32600).
enum class IdKind { Absent, Valid, Invalid };

IdKind idKindOf(const OpValue &msg)
{
    const OpValue *id = msg.find("id");
    if (!id) return IdKind::Absent;
    if (id->kind() == OpValue::Kind::Int || id->kind() == OpValue::Kind::Str)
        return IdKind::Valid;
    return IdKind::Invalid;
}

// The registry marks its internal failures with an "internal:" prefix
// (core/HostOps.h). Those are NOT tool-level failures: the dispatch could
// not complete (UI thread timeout) or the handler blew up.
bool isInternalFailure(const std::string &err)
{
    return err.compare(0, 9, "internal:") == 0;
}

// tools/list projection: registry snapshot -> ordered tool descriptors.
// inputSchema types come from the declared OpArg.type strings only; the
// schema emitted here IS the registry (m020 single-source rule).
std::string buildToolsList()
{
    OpValue arr = OpValue::makeList();
    for (const OpDef *def : HostRegistry::instance().snapshot()) {
        OpValue props = OpValue::makeMap();
        OpValue required = OpValue::makeList();
        for (const OpArg &a : def->args) {
            std::string t(a.type ? a.type : "");
            bool optional = !t.empty() && t.back() == '?';
            // An arg with no declared type is a registration slip, but the
            // registry still REQUIRES it (core/HostOps.cpp validates every
            // declared arg). Dropping it from the schema would leave the
            // tool uncallable - the client would not send it and the call
            // would die with bad_arg. Declaring it as a string keeps the
            // schema and the registry in agreement.
            std::string base = optional ? t.substr(0, t.size() - 1) : t;
            const char *jt = "string";
            if (base == "int") jt = "integer";
            else if (base == "bool") jt = "boolean";
            else if (base == "list") jt = "array";
            else if (base == "map") jt = "object";

            OpValue ps = OpValue::makeMap();
            ps.set("type", OpValue::makeStr(jt));
            ps.set("description", OpValue::makeStr(a.description ? a.description : ""));
            props.set(a.name, std::move(ps));
            if (!optional) required.push(OpValue::makeStr(a.name));
        }
        OpValue schema = OpValue::makeMap();
        schema.set("type", OpValue::makeStr("object"));
        schema.set("properties", std::move(props));
        // always an ARRAY, even empty: some clients choke on {} or null
        schema.set("required", std::move(required));

        OpValue tool = OpValue::makeMap();
        tool.set("name", OpValue::makeStr(def->name));
        tool.set("description", OpValue::makeStr(def->description));
        tool.set("inputSchema", std::move(schema));
        // annotations from perm bits (g5/unreal-mcp pattern): read ops are
        // safe to advertise as readOnlyHint
        if (def->perm == OpPerm::Read) {
            OpValue ann = OpValue::makeMap();
            ann.set("readOnlyHint", OpValue::makeBool(true));
            tool.set("annotations", std::move(ann));
        }
        arr.push(std::move(tool));
    }
    OpValue out = OpValue::makeMap();
    out.set("tools", std::move(arr));
    return jsonEncode(out);
}

// JSON-RPC-level call: args map already validated by caller of dispatch.
std::string handleToolsCall(const OpValue &req, const std::string &idJson)
{
    const OpValue *params = req.find("params");
    const OpValue *nameV = params ? params->find("name") : nullptr;
    if (!nameV || nameV->kind() != OpValue::Kind::Str)
        return errorObj(idJson, -32602, "params.name missing");
    const std::string tool = nameV->getString();

    const OpDef *def = HostRegistry::instance().find(tool);
    if (!def)
        return errorObj(idJson, -32602, "unknown tool: " + tool);

    // arguments: object (or absent) -> OpValue map; wrong shape = -32602
    std::map<std::string, OpValue> args;
    if (const OpValue *a = params->find("arguments")) {
        if (!a->isMap())
            return errorObj(idJson, -32602, "arguments must be an object");
        for (const auto &kv : a->map()) {
            const OpValue &v = kv.second;
            switch (v.kind()) {
            case OpValue::Kind::Null:
                break;   // null == not provided: registry defaults apply
            case OpValue::Kind::Int:
            case OpValue::Kind::Bool:
            case OpValue::Kind::Str:
            case OpValue::Kind::List:
            case OpValue::Kind::Map:
                args[kv.first] = v;
                break;
            default:
                return errorObj(idJson, -32602, "argument has unsupported JSON type");
            }
        }
    }

    OpOutcome o = HostRegistry::instance().dispatch(tool, args);

    if (!o.ok && isInternalFailure(o.err)) {
        // An internal failure is NOT a tool-level failure: the dispatch
        // could not complete (UI thread timeout) or the handler blew up.
        // The registry cannot take back what already ran - a timed-out
        // packet is still queued and DOES land later - so the honest answer
        // is a JSON-RPC internal error. An isError result would promise
        // "nothing was applied" (docs/MCP接入.md) and invite a retry that
        // applies the edit twice.
        return errorObj(idJson, -32603,
                        o.err + " - the operation may still be applied later; "
                                "do not retry blindly, read the document state first");
    }

    OpValue res = OpValue::makeMap();
    OpValue content = OpValue::makeList();
    if (o.ok) {
        std::string text;
        if (o.value.kind() == OpValue::Kind::Str) text = o.value.getString();
        else text = jsonEncode(o.value);
        OpValue item = OpValue::makeMap();
        item.set("type", OpValue::makeStr("text"));
        item.set("text", OpValue::makeStr(std::move(text)));
        content.push(std::move(item));
        res.set("content", std::move(content));
        res.set("isError", OpValue::makeBool(false));
    } else {
        // SEP-1303: tool-level failures are results, not JSON-RPC errors.
        // These really changed nothing: fix the arguments and call again.
        OpValue item = OpValue::makeMap();
        item.set("type", OpValue::makeStr("text"));
        item.set("text", OpValue::makeStr(sanitizeText(o.err)));
        content.push(std::move(item));
        res.set("content", std::move(content));
        res.set("isError", OpValue::makeBool(true));
    }
    return resultObj(idJson, jsonEncode(res));
}

// returns a frame to send, or "" for silence (notifications / drops).
// idOut (optional) receives the id to echo on an internal error frame: an
// exception escaping here must still answer the request it came from.
std::string handleLine(const std::string &line, std::string *idOut = nullptr)
{
    if (idOut) *idOut = "null";
    OpValue msg;
    std::string perr;
    if (!jsonParse(line, msg, perr)) {
        // -32700 with null id; connection stays open (g5 checklist)
        return errorObj("null", -32700, "parse error: " + perr);
    }
    if (!msg.isMap()) return errorObj("null", -32600, "request must be an object");

    // A frame with result/error and no method is a RESPONSE to something the
    // server asked the peer. JSON-RPC forbids answering a response, even if
    // its id has a type we would otherwise call invalid - so recognize the
    // response BEFORE the id validation below.
    if (!msg.find("method") && (msg.find("result") || msg.find("error"))) return "";

    const IdKind idk = idKindOf(msg);
    if (idk == IdKind::Invalid) {
        // An id of a type JSON-RPC does not allow makes the whole message
        // an Invalid Request. Answered with a null id - there is no id to
        // correlate, which is the shape the spec prescribes.
        return errorObj("null", -32600, "invalid request: id must be a string or an integer");
    }
    const std::string idJson = (idk == IdKind::Valid) ? idJsonOf(msg) : std::string("null");
    if (idOut) *idOut = idJson;

    // A frame WITHOUT an id is a NOTIFICATION: consumed, never answered.
    // Every method below honours that rule, so a notification can never be
    // mistaken for a dropped request.
    const bool notif = (idk == IdKind::Absent);

    // "jsonrpc" must be exactly "2.0" (JSON-RPC 2.0 request shape).
    const OpValue *jv = msg.find("jsonrpc");
    if (!jv || jv->kind() != OpValue::Kind::Str || jv->getString() != "2.0")
        return notif ? "" : errorObj(idJson, -32600, "invalid request: \"jsonrpc\" must be \"2.0\"");

    const OpValue *methodV = msg.find("method");
    // Responses were recognized before id validation and returned already.
    if (!methodV)
        return notif ? "" : errorObj(idJson, -32600, "invalid request: missing method");
    if (methodV->kind() != OpValue::Kind::Str)
        return notif ? "" : errorObj(idJson, -32600, "invalid request: method must be a string");
    const std::string method = methodV->getString();

    if (method == "initialize") {
        if (notif) return "";   // notification rule: consumed, never answered
        const OpValue *params = msg.find("params");
        const OpValue *pv = params ? params->find("protocolVersion") : nullptr;
        std::string version = (pv && pv->kind() == OpValue::Kind::Str)
                                  ? pv->getString() : std::string();
        if (!isSupportedVersion(version)) version = kLatestVersion;   // never an error

        OpValue si = OpValue::makeMap();
        si.set("name", OpValue::makeStr("pecia"));
        // One source for the version the client sees: PECIA_VERSION comes from
        // project(Pecia VERSION ...) in CMakeLists, the same macro the About
        // page and the updater use. A literal here reported 1.1.3 after the
        // tree moved to 1.2.0.
        si.set("version", OpValue::makeStr(PECIA_VERSION));
        OpValue caps = OpValue::makeMap();
        OpValue toolsCap = OpValue::makeMap();
        caps.set("tools", std::move(toolsCap));
        OpValue res = OpValue::makeMap();
        res.set("protocolVersion", OpValue::makeStr(version));
        res.set("capabilities", std::move(caps));
        res.set("serverInfo", std::move(si));
        // closed-loop instructions live in the protocol itself, not in the
        // user's prompt discipline (ahkdeck m159 lesson)
        res.set("instructions", OpValue::makeStr(
            "Pecia markdown editor host tools. Read doc.api('lua_api.txt') "
            "before writing scripts. Loop: read -> doc.apply_edit or write a "
            "script then lua.validate then lua.run/script.run -> verify with "
            "doc.get_text/doc.info. lua.run executes on a scratch copy, so an "
            "erroring run changes nothing; it returns {verdict,output,result,"
            "error,line,truncated,applied}. Runs are headless by default: win:* "
            "dialogs are captured into output instead of blocking, and a "
            "runaway loop is cut off as a timeout error. Write and exec tools "
            "fail with perm_denied while the "
            "active document is read-only or preview-locked; never retry those "
            "without changing the lock state. Tool failures come back as isError "
            "results - fix the arguments and call again; nothing was applied on "
            "isError. A JSON-RPC error (-32603) means the host itself failed: "
            "the operation may still be applied later, so read the document "
            "state before retrying."));
        return resultObj(idJson, jsonEncode(res));
    }

    if (method == "ping") return notif ? "" : resultObj(idJson, "{}");
    // These names are notification-only: silence applies to the notification
    // form. With an id they are requests with no handler and must get the
    // normal -32601 error instead of being silently swallowed.
    if (method == "notifications/initialized" || method == "notifications/cancelled") {
        if (notif) return "";
        return errorObj(idJson, -32601, "method not found: " + method);
    }
    if (method == "tools/list") return notif ? "" : resultObj(idJson, buildToolsList());
    if (method == "tools/call") {
        if (notif) {
            // ONE rule for every method: a notification is EXECUTED but
            // never answered. Dropping the call instead would leave the
            // agent believing a write happened that never did - the tool
            // really runs on the UI thread, it just has nobody to answer.
            const std::string frame = handleToolsCall(msg, idJson);
            if (!frame.empty()) opLog("mcp: notification tool call: %s", frame.c_str());
            return "";
        }
        return handleToolsCall(msg, idJson);
    }

    if (notif) return "";   // unknown notifications are silently consumed
    return errorObj(idJson, -32601, "method not found: " + method);
}

// ---------------------------------------------------------------------------
// selftest fixtures (must mirror, not re-implement, production paths)
// ---------------------------------------------------------------------------

struct Nail { std::string line; const char *mustContain; };

int runFixtureRound()
{
    // fixture registry: dispatch runs same-thread (no executor installed)
    HostRegistry &reg = HostRegistry::instance();
    reg.add({"echo", "echo back", {{"text", "string", "t"},
                                   {"times", "int?", "repeat count"}},
             OpPerm::Read,
             [](const std::map<std::string, OpValue> &a) -> OpOutcome {
        long long n = a.count("times") ? a.at("times").getInt() : 1;
        if (n < 1 || n > 10) return OpOutcome::fail("bad_arg: times out of range");
        std::string s;
        for (long long i = 0; i < n; ++i) s += a.at("text").getString();
        return OpOutcome::success(OpValue::makeStr(std::move(s)));
    }});
    reg.add({"locked", "write op", {}, OpPerm::Write,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        return OpOutcome::success(OpValue::makeBool(true));
    }});
    // an op that fails the way a timed-out dispatch fails: the registry
    // marks those with an "internal:" prefix and the packet may still land
    reg.add({"boom", "internal failure", {}, OpPerm::Read,
             [](const std::map<std::string, OpValue> &) -> OpOutcome {
        return OpOutcome::fail("internal: ui thread timeout");
    }});

    static const Nail kGolden[] = {
        // version negotiation: unknown version tolerated, newest returned
        {R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01","capabilities":{}}})",
         "\"protocolVersion\":\"2025-11-25\""},
        // notifications: silence
        {R"({"jsonrpc":"2.0","method":"notifications/initialized"})", ""},
        // ping three-line
        {R"({"jsonrpc":"2.0","id":2,"method":"ping"})", "\"result\":{}"},
        // tools/list reflects registry with deterministic order and []-arrays
        {R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})",
         "\"required\":[\"text\"]"},
        // unknown method -> -32601
        {R"({"jsonrpc":"2.0","id":4,"method":"resources/list"})", "\"code\":-32601"},
        // unknown tool -> -32602 (protocol shape), NOT isError
        {R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"nope"}})",
         "\"code\":-32602"},
        // happy call: map result text
        {R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"echo","arguments":{"text":"ab","times":2}}})",
         "\"text\":\"abab\""},
        // tool semantic failure -> isError:true result, not JSON-RPC error
        {R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"echo","arguments":{"text":"x","times":99}}})",
         "\"isError\":true"},
        // perm gate: no gate installed -> deny (fail-safe), tool-level
        {R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"locked","arguments":{}}})",
         "perm_denied"},
        // bad arguments shape -> -32602
        {R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"echo","arguments":42}})",
         "\"code\":-32602"},
        // parse error: -32700, connection alive
        {"{oops", "\"code\":-32700"},
        {std::string("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"ping\",\"x\":\"a") + char(3) + "b\"}",
         "\"code\":-32700"},
        // lone id without method (response-shaped junk with id) -> -32600
        {R"({"jsonrpc":"2.0","id":11})", "\"code\":-32600"},
        // float anywhere (incl. id position) is outside our int64 JSON
        // contract: the whole line is a parse error, connection survives
        {R"({"jsonrpc":"2.0","id":1.5,"method":"ping"})", "\"code\":-32700"},
        // every method is notification-safe: consumed, never answered
        {R"({"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2025-11-25"}})", ""},
        {R"({"jsonrpc":"2.0","method":"ping"})", ""},
        {R"({"jsonrpc":"2.0","method":"tools/list"})", ""},
        {R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"echo","arguments":{"text":"x"}}})", ""},
        // an id of a type JSON-RPC does not allow -> Invalid Request
        {R"({"jsonrpc":"2.0","id":true,"method":"ping"})", "\"code\":-32600"},
        {R"({"jsonrpc":"2.0","id":{"a":1},"method":"ping"})", "\"code\":-32600"},
        {R"({"jsonrpc":"2.0","id":[1],"method":"ping"})", "\"code\":-32600"},
        // a null id is NOT allowed either (MCP: id MUST NOT be null)
        {R"({"jsonrpc":"2.0","id":null,"method":"ping"})", "\"code\":-32600"},
        // a response-shaped message must NOT be answered
        {R"({"jsonrpc":"2.0","id":1,"result":{}})", ""},
        // the "jsonrpc" member is mandatory and must be exactly "2.0"
        {R"({"id":12,"method":"ping"})", "\"code\":-32600"},
        {R"({"jsonrpc":"1.0","id":13,"method":"ping"})", "\"code\":-32600"},
        // an internal failure is a JSON-RPC error, never isError
        {R"({"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"boom","arguments":{}}})",
         "\"code\":-32603"},
    };

    int fails = 0;
    for (const Nail &n : kGolden) {
        std::string got = handleLine(n.line);
        bool ok = n.mustContain[0] == '\0' ? got.empty()
                                           : got.find(n.mustContain) != std::string::npos;
        // pre-call ValidateJSON on our OWN outgoing frames (tsar lesson):
        if (!got.empty()) {
            OpValue check;
            std::string jerr;
            if (!jsonParse(got, check, jerr)) { ok = false; }
        }
        if (!ok) {
            ++fails;
            std::fprintf(stderr, "FAIL selftest golden: %s\n  want [%s]\n  got  [%s]\n",
                         n.line.c_str(), n.mustContain, got.c_str());
        }
    }

    // serverInfo carries the build's own identity. This used to be a literal,
    // so the handshake kept announcing 1.1.3 after the tree moved to 1.2.0 -
    // the nail below is what makes that impossible to ship again.
    {
        const std::string got = handleLine(
            R"({"jsonrpc":"2.0","id":99,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{}}})");
        const std::string want = std::string("\"version\":\"") + PECIA_VERSION + "\"";
        if (got.find(want) == std::string::npos) {
            ++fails;
            std::fprintf(stderr, "FAIL serverInfo version: want [%s]\n  got  [%s]\n",
                         want.c_str(), got.c_str());
        }
    }

    // JSON layer nails
    {
        OpValue v;
        std::string e;
        if (jsonParse(R"({"a":1.5})", v, e)) { ++fails; std::fprintf(stderr, "FAIL floats accepted\n"); }
        if (jsonParse(R"({"a":1 }})", v, e)) { ++fails; std::fprintf(stderr, "FAIL trailing accepted\n"); }
        // JSON forbids leading zeros
        if (jsonParse(R"({"a":01})", v, e)) { ++fails; std::fprintf(stderr, "FAIL leading zero accepted\n"); }
        if (jsonParse(R"({"a":-01})", v, e)) { ++fails; std::fprintf(stderr, "FAIL negative leading zero accepted\n"); }
        if (!jsonParse(R"({"a":0})", v, e)) { ++fails; std::fprintf(stderr, "FAIL plain zero rejected\n"); }
    }
    {
        OpValue v; std::string e;
        // deep nesting rejected
        std::string deep(200, '[');
        if (jsonParse(deep, v, e)) { ++fails; std::fprintf(stderr, "FAIL depth accepted\n"); }
        // depth cap: kMaxDepth levels are legal, one more is not, and the
        // legal one survives the encoder untouched (no silent null rewrite)
        std::string ok32, no33;
        for (int i = 0; i < kMaxDepth; ++i) ok32 += '[';
        for (int i = 0; i < kMaxDepth; ++i) ok32 += ']';
        for (int i = 0; i < kMaxDepth + 1; ++i) no33 += '[';
        for (int i = 0; i < kMaxDepth + 1; ++i) no33 += ']';
        OpValue d;
        if (!jsonParse(ok32, d, e)) {
            ++fails; std::fprintf(stderr, "FAIL depth %d rejected: %s\n", kMaxDepth, e.c_str());
        } else if (jsonEncode(d) != ok32) {
            ++fails; std::fprintf(stderr, "FAIL depth round trip corrupted\n");
        }
        if (jsonParse(no33, d, e)) { ++fails; std::fprintf(stderr, "FAIL depth %d accepted\n", kMaxDepth + 1); }
        // surrogate pair round trip: emoji survives as 4-byte UTF-8
        if (jsonParse("{\"s\":\"\\ud83d\\ude00\"}", v, e) && v.find("s")) {
            const std::string &s = v.find("s")->getString();
            if (s.size() != 4) { ++fails; std::fprintf(stderr, "FAIL surrogate len=%zu\n", s.size()); }
        } else { ++fails; std::fprintf(stderr, "FAIL surrogate parse\n"); }
        // encoder escapes NUL and control chars
        OpValue sv = OpValue::makeStr(std::string("a\0b\nc", 5));
        std::string enc = jsonEncode(sv);
        if (enc.find("\\u0000") == std::string::npos || enc.find("\\n") == std::string::npos) {
            ++fails; std::fprintf(stderr, "FAIL encode control escape: %s\n", enc.c_str());
        }
        // broken UTF-8 must not reach the peer: it is replaced, not passed through
        OpValue bad = OpValue::makeMap();
        bad.set("s", OpValue::makeStr(std::string("a\xFF\xFE" "b", 4)));
        const std::string badEnc = jsonEncode(bad);
        OpValue back; std::string berr;
        if (!jsonParse(badEnc, back, berr)) {
            ++fails; std::fprintf(stderr, "FAIL invalid utf-8 frame: %s\n", berr.c_str());
        } else if (back.find("s") && back.find("s")->getString().size() != 8) {
            ++fails; std::fprintf(stderr, "FAIL invalid utf-8 not replaced\n");
        }
    }

    // framing: an oversized line is reported as such and the NEXT line is
    // still served - a hostile frame must not close the endpoint.
    {
        const std::string big(kMaxLineLen + 4096, 'x');
        const std::string src = big + "\n"
                                + R"({"jsonrpc":"2.0","id":1,"method":"ping"})" + "\n";
        size_t at = 0;
        LineReader reader([&src, &at](char *dst, size_t want) -> size_t {
            const size_t left = src.size() - at;
            const size_t n = left < want ? left : want;
            std::memcpy(dst, src.data() + at, n);
            at += n;
            return n;
        });
        std::string line;
        if (reader.next(line) != LineReader::Status::Oversized) {
            ++fails; std::fprintf(stderr, "FAIL oversized line not detected\n");
        }
        if (reader.next(line) != LineReader::Status::Line ||
            line.find("\"ping\"") == std::string::npos) {
            ++fails; std::fprintf(stderr, "FAIL reader lost the next line: [%s]\n", line.c_str());
        }
    }

    // A peer streaming bytes with NO newline must not grow the reader buffer
    // without bound: discardToNewline() used to advance pos_ to the end and
    // refill() appended without compacting, so every 64 KiB chunk stuck around
    // forever. Feed 32 MiB of newline-free data (512 chunks); the retained
    // buffer must stay around one chunk, not climb past the line cap.
    {
        const size_t kChunks = 512;
        size_t served = 0;
        LineReader reader([&served, kChunks](char *dst, size_t want) -> size_t {
            if (served++ >= kChunks) return 0;   // EOF after the flood
            const size_t n = want < 64 * 1024 ? want : 64 * 1024;
            std::memset(dst, 'x', n);
            return n;
        });
        std::string line;
        if (reader.next(line) != LineReader::Status::Oversized) {
            ++fails; std::fprintf(stderr, "FAIL newline-free flood not reported Oversized\n");
        }
        // The whole 32 MiB was consumed to EOF (discardToNewline drained it).
        // The buffer must have been reclaimed each round - a hard bound well
        // below the flood size proves it, and regresses if compact() is lost.
        if (reader.bufferedBytes() > (1u << 20)) {
            ++fails;
            std::fprintf(stderr, "FAIL reader buffer grew to %zu bytes on newline-free flood\n",
                         reader.bufferedBytes());
        }
    }

    // an internal failure must be a JSON-RPC error, never isError:true:
    // the operation may still land later, so a blind retry would apply it twice.
    {
        const std::string got = handleLine(
            R"({"jsonrpc":"2.0","id":15,"method":"tools/call","params":{"name":"boom","arguments":{}}})");
        if (got.find("\"code\":-32603") == std::string::npos ||
            got.find("isError") != std::string::npos ||
            got.find("may still be applied later") == std::string::npos) {
            ++fails; std::fprintf(stderr, "FAIL internal failure shape: %s\n", got.c_str());
        }
        // and the id must be echoed so the client can correlate the answer
        if (got.find("\"id\":15") == std::string::npos) {
            ++fails; std::fprintf(stderr, "FAIL internal failure lost the id: %s\n", got.c_str());
        }
    }

    // registry projection equality: tools/list order == snapshot order
    {
        std::string tl = buildToolsList();
        auto snap = HostRegistry::instance().snapshot();
        size_t prev = 0;
        for (const OpDef *def : snap) {
            size_t at = tl.find("\"name\":\"" + def->name + "\"");
            if (at == std::string::npos || at < prev) {
                ++fails; std::fprintf(stderr, "FAIL projection order at %s\n", def->name.c_str());
            }
            prev = at;
        }
    }

    reg.removeAll();
    return fails;
}

bool g_stdioMode = false;

} // namespace

bool stdioModeRequested(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (argv[i] && std::string(argv[i]) == "--mcp-stdio") { g_stdioMode = true; return true; }
    // also honor the wide command line (GUI launches may bypass argv wiring)
#if defined(_WIN32)
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    bool found = false;
    for (int i = 1; wargv && i < wargc; ++i) {
        static const wchar_t flag[] = L"--mcp-stdio";
        if (wargv[i] && wcscmp(wargv[i], flag) == 0) { found = true; break; }
    }
    if (wargv) LocalFree(wargv);
    if (found) { g_stdioMode = true; return true; }
#else
    (void)argc; (void)argv;
#endif
    return g_stdioMode;
}

void runStdioLoop()
{
    // Red line 6: this is a thread entry. An exception escaping here is
    // fastfail 0xC0000409, which bypasses CrashReport - no dump at all. The
    // inner catch keeps one bad request from killing the endpoint.
    try {
#if defined(_WIN32)
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::string line;
        std::string idJson = "null";   // the id to echo if a request blows up
        LineReader reader(readStdinChunk);
        for (;;) {
            const LineReader::Status st = reader.next(line);
            if (st == LineReader::Status::Eof) break;
            idJson = "null";
            std::string frame;
            if (st == LineReader::Status::Oversized) {
                // A frame over the line limit is a parse failure, not a
                // reason to close the connection: answer -32700 and keep
                // serving (the rest of the line was discarded).
                frame = errorObj("null", -32700,
                                 "parse error: request line exceeds the 1 MiB limit");
            } else {
                try {
                    frame = handleLine(line, &idJson);
                } catch (const std::exception &e) {
                    opLog("mcp: request dropped: %s", e.what());
                    frame = errorObj(idJson, -32603, "internal error");
                } catch (...) {
                    opLog("mcp: request dropped: unknown exception");
                    frame = errorObj(idJson, -32603, "internal error");
                }
            }
            if (frame.empty()) continue;
            frame += '\n';
            writeFrame(frame);
        }
        std::fprintf(stderr, "mcp: stdin closed, endpoint exiting\n");
    } catch (const std::exception &e) {
        opLog("mcp: endpoint stopped: %s", e.what());
    } catch (...) {
        opLog("mcp: endpoint stopped: unknown exception");
    }
}

int runSelfTest()
{
    int fails = runFixtureRound();
    std::fprintf(stderr, "mcp_selftest: %d failures\n", fails);
    return fails ? 1 : 0;
}

} // namespace mcp
