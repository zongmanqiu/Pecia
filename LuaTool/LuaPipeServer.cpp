// LuaPipeServer.cpp - named-pipe server; requests run on the main UI thread.
#include "LuaPipeServer.h"

#include "ui/MainWindow.h"
#include "PipeProtocol.h"

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
    // Signal the listener to stop and wait for it to finish. Prevents the
    // listener (which holds `this`) from out-living the owning MainWindow
    // and keeps teardown deterministic (the old code detached the thread,
    // which ran forever even after the window was destroyed).
    m_ready = false;
    if (m_thread.joinable()) m_thread.join();
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
        HANDLE hPipe = CreateNamedPipeA(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) break;

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
                CloseHandle(hPipe);
                break;
            }
        } else {
            // Unexpected connect failure - give up the pipe server.
            CloseHandle(hPipe);
            break;
        }

        char tag[4] = {0};
        if (!readAll(hPipe, tag, 4)) {
            CloseHandle(hPipe);
            continue;
        }
        DWORD len = 0;
        if (!readAll(hPipe, &len, 4) || len > kFrameMax) {
            CloseHandle(hPipe);
            continue;
        }
        std::string payload(len, '\0');
        if (len > 0 && !readAll(hPipe, &payload[0], len)) {
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
            req->cv.wait_for(lk, std::chrono::seconds(30), [&req]() { return req->done; });
        }

        const char *rtag = req->ok ? PIPE_TAG_OK : PIPE_TAG_ERR;
        const std::string &reply = req->ok ? req->out : req->err;
        writeAll(hPipe, rtag, 4);
        DWORD plen = (DWORD)reply.size();
        writeAll(hPipe, &plen, 4);
        if (plen > 0) writeAll(hPipe, reply.data(), plen);

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    CloseHandle(hAccept);
}
