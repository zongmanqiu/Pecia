// test_pipe_protocol.cpp - wire-protocol tests for the inter-process
// named pipe between Pecia and its tools (PeciaLua / PeciaAIChat).
//
// This locks the contract that makes "Lua console Save As -> main window
// reloads the script bar" work: LuaToolWindow.cpp sends exactly
//   pipeSend(m_peciaPipe, PIPE_TAG_RELOAD, "")
// on a successful Save As, and the main window's pipe server
// (LuaPipeServer.cpp) parses the frame, routes RLOD to
// reloadScriptBar(), and replies OK. If any side of the frame
// format changes (tag bytes, length encoding, reply shape), this test
// fails.
//
// The server side here mirrors LuaPipeServer::listenLoop's frame
// parsing (4-byte tag, u32 LE length, payload, then OK/ERR reply);
// the client side links the REAL core/PipeClient.cpp so the exact
// production send path is exercised.
#include "test_assert.h"
#include "core/PipeClient.h"
#include "core/PipeProtocol.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

bool readAll(HANDLE h, void *buf, DWORD n) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
        DWORD got = 0;
        if (!ReadFile(h, p, n, &got, nullptr) || got == 0) return false;
        p += got;
        n -= got;
    }
    return true;
}

bool writeAll(HANDLE h, const void *buf, DWORD n) {
    const char *p = static_cast<const char *>(buf);
    while (n > 0) {
        DWORD sent = 0;
        if (!WriteFile(h, p, n, &sent, nullptr) || sent == 0) return false;
        p += sent;
        n -= sent;
    }
    return true;
}

// Frame received by the server side of the pipe.
struct ReceivedFrame {
    std::string tag;      // 4 bytes, e.g. "RLOD"
    std::string payload;  // 0..N bytes
};

// One-shot pipe server: parses one frame (same code shape as
// LuaPipeServer::listenLoop), hands it to `onFrame` for handling, and
// replies OK/ERR + payload. Runs on its own thread.
struct TestPipeServer {
    std::string        name;
    std::atomic<bool>  ready{false};
    ReceivedFrame      received;
    std::string        replyTag = PIPE_TAG_OK;
    std::string        replyPayload;

    explicit TestPipeServer(const std::string &pipeName) : name(pipeName) {}

    // Start the listener thread; returns once the pipe exists.
    void start() {
        std::thread([this]() { run(); }).detach();
        while (!ready.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void run() {
        HANDLE hPipe = CreateNamedPipeA(
            name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) { ready = true; return; }
        ready = true;

        if (!ConnectNamedPipe(hPipe, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe);
            return;
        }

        char tag[4] = {0};
        if (!readAll(hPipe, tag, 4)) { CloseHandle(hPipe); return; }
        DWORD len = 0;
        if (!readAll(hPipe, &len, 4) || len > (16 << 20)) {
            CloseHandle(hPipe);
            return;
        }
        std::string payload(len, '\0');
        if (len > 0 && !readAll(hPipe, &payload[0], len)) {
            CloseHandle(hPipe);
            return;
        }

        received.tag.assign(tag, 4);
        received.payload = std::move(payload);

        writeAll(hPipe, replyTag.data(), 4);
        DWORD plen = (DWORD)replyPayload.size();
        writeAll(hPipe, &plen, 4);
        if (plen > 0) writeAll(hPipe, replyPayload.data(), plen);
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
};

std::string uniquePipeName(int seq) {
    char buf[128];
    snprintf(buf, sizeof(buf), "\\\\.\\pipe\\pecia-test-%lu-%d",
             (unsigned long)GetCurrentProcessId(), seq);
    return buf;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

// The Save As -> reload contract: LuaTool sends exactly this frame
// (LuaToolWindow.cpp:427) and the main window answers OK with an empty
// payload (LuaPipeServer.cpp:65-67). Server must receive tag "RLOD"
// and an empty payload.
static void test_reload_frame_contract() {
    TestPipeServer srv(uniquePipeName(1));
    srv.start();

    PipeReply rep = pipeSend(srv.name, PIPE_TAG_RELOAD, "");
    CHECK(rep.peerConnected);
    CHECK(rep.ok);
    CHECK(rep.payload.empty());
    CHECK(srv.received.tag == PIPE_TAG_RELOAD);
    CHECK(srv.received.payload.empty());
}

// RUN-style frame with a multi-line UTF-8 payload (a Lua script, e.g.
// "print('中文')") must arrive byte-identical; the reply payload (script
// output) must also round-trip byte-identical.
static void test_run_payload_roundtrip() {
    const std::string script = "print('hello')\nprint('\xE4\xB8\xAD\xE6\x96\x87') \r\n";
    const std::string output = "hello\n\xE4\xB8\xAD\xE6\x96\x87\n";

    TestPipeServer srv(uniquePipeName(2));
    srv.replyPayload = output;   // simulate the RUN reply carrying print() output
    srv.start();

    PipeReply rep = pipeSend(srv.name, PIPE_TAG_RUN, script);
    CHECK(rep.peerConnected);
    CHECK(rep.ok);
    CHECK(srv.received.tag == PIPE_TAG_RUN);
    CHECK(srv.received.payload == script);
    CHECK(rep.payload == output);
}

// INS (insert at cursor) with an empty reply is what the AI-chat tool
// uses; the payload must arrive intact even when it has no trailing
// newline.
static void test_ins_payload_roundtrip() {
    const std::string text = "func() -- 无换行";

    TestPipeServer srv(uniquePipeName(3));
    srv.start();

    PipeReply rep = pipeSend(srv.name, PIPE_TAG_INS, text);
    CHECK(rep.peerConnected);
    CHECK(rep.ok);
    CHECK(srv.received.tag == PIPE_TAG_INS);
    CHECK(srv.received.payload == text);
}

// ERR reply: the client must see ok=false and carry the error message
// in the payload (production: ERR <len> <message>).
static void test_err_reply() {
    TestPipeServer srv(uniquePipeName(4));
    srv.replyTag = PIPE_TAG_ERR;
    srv.replyPayload = "script error at line 3";
    srv.start();

    PipeReply rep = pipeSend(srv.name, PIPE_TAG_RUN, "bad()");
    CHECK(rep.peerConnected);
    CHECK(!rep.ok);
    CHECK(rep.payload == "script error at line 3");
}

// No peer listening (e.g. Lua tool launched while Pecia is not running):
// pipeSend must return promptly with peerConnected=false instead of
// hanging on the wait.
static void test_no_peer_no_hang() {
    auto t0 = std::chrono::steady_clock::now();
    PipeReply rep = pipeSend(uniquePipeName(99), PIPE_TAG_RELOAD, "", 4000);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    CHECK(!rep.peerConnected);
    CHECK(!rep.ok);
    CHECK(ms < 3000);   // must not block for the full waitMs
}

} // namespace

int main() {
    test_reload_frame_contract();
    test_run_payload_roundtrip();
    test_ins_payload_roundtrip();
    test_err_reply();
    test_no_peer_no_hang();
    if (test::failCount() == 0) {
        printf("test_pipe_protocol: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_pipe_protocol: %d/%d checks FAILED\n",
            test::failCount(), test::checkCount());
    return 1;
}
