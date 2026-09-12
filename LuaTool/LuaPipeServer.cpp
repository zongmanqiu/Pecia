// LuaPipeServer.cpp - named-pipe server; requests run on the main UI thread.
#include "LuaPipeServer.h"

#include "ui/MainWindow.h"
#include "PipeProtocol.h"
#include "core/PipeSecurity.h"

#include <windows.h>
#include <FL/Fl.H>

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace {

const DWORD kFrameMax = 16 << 20;   // 16 MB frame cap (document snapshots)
const DWORD kAcceptPollMs = 200;    // overlapped connect wait timeout (ms)

bool readAll(HANDLE h, void *buf, DWORD n) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
        DWORD got = 0;
        // NOTE: the pipe is created with FILE_FLAG_OVERLAPPED (required for
        // async ConnectNamedPipe), but we use synchronous I/O after connect.
        // MS docs warn this is undefined, but it works in practice for named
        // pipes in connected state. CancelIoEx + wakeUp() handle teardown.
        if (!ReadFile(h, p, n, &got, nullptr)) return false;
        if (got == 0) return false;
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

// Request frame marshalled to the UI thread. Shared_ptr so the request stays
// alive after the listener thread's wait_for times out: the queued UI-thread
// callback holds its own copy and safely completes even if the listener has
// already dropped its reference (a stack/raw-owned request would be freed and
// the late callback would dereference a destroyed object -> UB).
struct PipeRequest {
    explicit PipeRequest(MainWindow *o) : owner(o) {}
    MainWindow      *owner;
    std::string      tag;
    std::string      payload;
    bool             ok = false;
    std::string      out;   // RUN: print() output
    std::string      err;   // error message (RUN failure / INS failure)
    std::mutex       mu;
    std::condition_variable cv;
    bool             done = false;
};

// `data` is a heap-allocated std::shared_ptr<PipeRequest>*. We take
// ownership, run the request on the UI thread, signa done, then free the
// shared_ptr wrapper. The inner request stays alive via this copy even if
// the listener already gave up its reference.
void s_runOnUiThread(void *data) {
    std::shared_ptr<PipeRequest> *wrapper =
        static_cast<std::shared_ptr<PipeRequest> *>(data);
    std::shared_ptr<PipeRequest> r = std::move(*wrapper);
    delete wrapper;
    if (r->tag == PIPE_TAG_INS) {
        r->ok = r->owner->insertAiText(r->payload, &r->err);
    } else if (r->tag == PIPE_TAG_GETSEL) {
        r->ok = r->owner->getSelectionText(&r->out, &r->err);
    } else if (r->tag == PIPE_TAG_GETDOC) {
        r->ok = r->owner->getDocumentSnapshot(&r->out, &r->err);
    } else if (r->tag == PIPE_TAG_APPLY) {
        r->ok = r->owner->applyDocumentSnapshot(r->payload, &r->err);
    } else if (r->tag == PIPE_TAG_RELOAD) {
        r->owner->reloadScriptBar();
        r->ok = true;
    } else if (r->tag == PIPE_TAG_CLRSEL) {
        r->ok = r->owner->clearSelection(&r->err);
    } else if (r->tag == PIPE_TAG_SEND) {
        r->ok = r->owner->sendToAiChat(r->payload, &r->err);
    } else {   // PIPE_TAG_RUN
        r->ok = r->owner->executeLuaScript(r->payload, &r->out, &r->err);
    }
    {
        std::lock_guard<std::mutex> lk(r->mu);
        r->done = true;
    }
    r->cv.notify_one();
}

} // namespace

LuaPipeServer::LuaPipeServer(MainWindow *owner)
    : m_owner(owner) {
    char name[128];
    snprintf(name, sizeof(name), "%s%lu", "\\\\.\\pipe\\pecia-lua-", GetCurrentProcessId());
    m_pipeName = name;
    m_ready = true;
    m_thread = std::thread([this]() { listenLoop(); });
}

LuaPipeServer::~LuaPipeServer() {
    // Signal the listener to stop, wake it if it is blocked inside a pipe
    // I/O (ReadFile/WriteFile have no timeout and a stuck client could
    // otherwise make join() hang forever - e.g. the process would not
    // exit), then wait for it to finish.
    m_ready = false;
    wakeUp();
    // Cancel any in-flight read/write on the currently accepted pipe: this
    // is the case wakeUp()'s self-connect cannot cover (the listener is
    // blocked reading that client's pipe, so our STOP frame sits unread on
    // a new instance). CancelIoEx makes the blocked ReadFile/WriteFile
    // return immediately with ERROR_OPERATION_ABORTED.
    HANDLE cur = m_curPipe.load();
    if (cur) CancelIoEx(cur, nullptr);
    if (m_thread.joinable()) m_thread.join();
}

// Self-connect and send a STOP frame. If the listener is parked inside a
// readAll()/writeAll() with no timeout, the frame arrives and lets it
// observe the teardown and exit; if the listener already stopped, the
// connection simply fails/does nothing.
void LuaPipeServer::wakeUp() {
    HANDLE hPipe = CreateFileA(m_pipeName.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    // STOP tag + zero-length payload frame.
    const char stopTag[4] = { 'S', 'T', 'O', 'P' };
    WriteFile(hPipe, stopTag, 4, &written, nullptr);
    DWORD zero = 0;
    WriteFile(hPipe, &zero, 4, &written, nullptr);
    CloseHandle(hPipe);
}

void LuaPipeServer::listenLoop() {
    // A manual-reset event is used both to interrupt the blocking connect
    // wait (so the loop can observe m_ready == false and exit) and to carry
    // a connect-completion from the overlapped I/O. The event is reset at
    // the start of each accept so a previous completion can't be mistaken
    // for a new one, and the wait has a short timeout so the loop also
    // re-checks m_ready periodically (covers the no-client case).
    HANDLE hAccept = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (hAccept == nullptr) return;

    while (m_ready) {
        // User-only DACL: without one, any process of any user/integrity
        // level could connect and run arbitrary Lua (os.execute/io/package
        // are fully open - see lua_api.txt) as the Pecia user.
        SECURITY_DESCRIPTOR sd;
        SECURITY_ATTRIBUTES sa;
        PACL acl = nullptr;
        bool secOk = pipeSec::makeUserOnlySa(sd, sa, acl);
        HANDLE hPipe = CreateNamedPipeA(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, secOk ? &sa : nullptr);
        if (acl) LocalFree(acl);
        if (hPipe == INVALID_HANDLE_VALUE) break;

        // Register the handle so the destructor can CancelIoEx an in-flight
        // blocking read/write during teardown.
        m_curPipe.store(hPipe, std::memory_order_relaxed);

        ResetEvent(hAccept);
        OVERLAPPED ov = {};
        ov.hEvent = hAccept;
        BOOL connOk = ConnectNamedPipe(hPipe, &ov);
        DWORD err = GetLastError();
        if (connOk) {
            // Connected synchronously (client was already waiting).
        } else if (err == ERROR_PIPE_CONNECTED) {
            // Client connected in the tiny gap between CreateNamedPipe and
            // ConnectNamedPipe; treat as connected.
        } else if (err == ERROR_IO_PENDING) {
            // Async connect in progress - wait on the event (bounded so the
            // loop can notice teardown even without a client, and unbounded
            // only until a client or the destructor intervenes).
            for (;;) {
                DWORD wr = WaitForSingleObject(hAccept, kAcceptPollMs);
                if (wr == WAIT_OBJECT_0) break;           // connected
                if (!m_ready) {                           // told to stop
                    CancelIoEx(hPipe, nullptr);
                    break;
                }
            }
            if (!m_ready) {
                // Teardown while waiting for a client.
                CancelIoEx(hPipe, nullptr);
                m_curPipe.store(nullptr, std::memory_order_relaxed);
                CloseHandle(hPipe);
                break;
            }
        } else {
            // Unexpected connect failure - give up the pipe server.
            m_curPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
            break;
        }

        char tag[4] = {0};
        if (!readAll(hPipe, tag, 4) ||
            memcmp(tag, PIPE_TAG_STOP, 4) == 0) {   // teardown wake-up frame
            m_curPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
            if (memcmp(tag, PIPE_TAG_STOP, 4) == 0) break;   // destructor asked us to stop
            continue;
        }
        DWORD len = 0;
        if (!readAll(hPipe, &len, 4) || len > kFrameMax) {
            m_curPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
            continue;
        }
        std::string payload(len, '\0');
        if (len > 0 && !readAll(hPipe, &payload[0], len)) {
            m_curPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
            continue;
        }

        std::shared_ptr<PipeRequest> req = std::make_shared<PipeRequest>(m_owner);
        req->tag.assign(tag, 4);
        req->payload = std::move(payload);
        // Pass a heap shared_ptr wrapper through the opaque Fl::awake data;
        // s_runOnUiThread takes ownership of the wrapper. req keeps its own
        // reference, so even if the UI callback is delayed past the wait_for
        // timeout the request object stays alive until the callback completes.
        Fl::awake(s_runOnUiThread, new std::shared_ptr<PipeRequest>(req));
        {
            std::unique_lock<std::mutex> lk(req->mu);
            // Wait in bounded slices and also abort when m_ready turns false
            // (teardown): without this, a request that never completes on the
            // UI thread (e.g. shutdown raced a request) would block the
            // destructor's join() for the full 30 s.
            while (!req->done && m_ready.load()) {
                req->cv.wait_for(lk, std::chrono::seconds(1));
            }
        }

        const char *rtag = req->ok ? PIPE_TAG_OK : PIPE_TAG_ERR;
        const std::string &reply = req->ok ? req->out : req->err;
        writeAll(hPipe, rtag, 4);
        DWORD plen = (DWORD)reply.size();
        writeAll(hPipe, &plen, 4);
        if (plen > 0) writeAll(hPipe, reply.data(), plen);

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        // The current connection is finished; clear the handle before
        // closing so the destructor never CancelIoEx's a closed handle.
        m_curPipe.store(nullptr, std::memory_order_relaxed);
        CloseHandle(hPipe);
    }

    m_curPipe.store(nullptr, std::memory_order_relaxed);
    CloseHandle(hAccept);
}
