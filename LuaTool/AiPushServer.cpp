// AiPushServer.cpp - see AiPushServer.h.
#include "LuaTool/AiPushServer.h"

#include "core/OpLog.h"
#include "core/PipeSecurity.h"
#include <windows.h>

#include <cstdio>

namespace {

bool writeAll(HANDLE h, const void *buf, DWORD n) {
    const char *p = static_cast<const char *>(buf);
    while (n > 0) {
        DWORD sent = 0;
        // NOTE: synchronous I/O on overlapped handle — see readAll() comment.
        if (!WriteFile(h, p, n, &sent, nullptr) || sent == 0) return false;
        p += sent;
        n -= sent;
    }
    return true;
}

} // namespace

AiPushServer::AiPushServer() {
    char name[128];
    snprintf(name, sizeof(name), "%s%lu",
             "\\\\.\\pipe\\pecia-ai-push-", GetCurrentProcessId());
    m_pipeName = name;
    m_ready = true;
    m_thread = std::thread([this]() { listenLoop(); });
}

AiPushServer::~AiPushServer() {
    // Signal the listener to stop and wait for it to finish. This prevents
    // the listener thread (which touches m_pipeName/m_cv/m_mu/m_queue) from
    // outliving this object - the old code detached the thread, so once this
    // object was destroyed any still-blocked thread read freed members (UB).
    m_ready = false;
    m_cv.notify_all();
    // A client that connected but stopped reading leaves the listener stuck
    // in WriteFile (the 64 KB pipe buffer fills); notify_all() cannot wake a
    // thread blocked in kernel I/O, so cancel the in-flight I/O directly
    // (same teardown pattern as LuaPipeServer).
    if (HANDLE cur = m_curPipe.load(std::memory_order_relaxed))
        CancelIoEx(cur, nullptr);
    if (m_thread.joinable()) m_thread.join();
}

void AiPushServer::push(const std::string &text) {
    if (text.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_queue.push_back(text);
    }
    m_cv.notify_one();
    opLog("aipush: queued %zu bytes", text.size());
}

void AiPushServer::listenLoop() {
    // Overlapped connect + exit event so the outer accept can be interrupted
    // promptly by the destructor (synchronous ConnectNamedPipe would block
    // forever and make join() hang). Wait on the event with a poll timeout so
    // the loop also notices m_ready turning false even with no incoming client.
    HANDLE hAccept = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (hAccept == nullptr) return;

    while (m_ready) {
        // User-only DACL (see core/PipeSecurity.h): the push channel feeds
        // text straight into the AI chat; lock it to the current user.
        SECURITY_DESCRIPTOR sd;
        SECURITY_ATTRIBUTES sa;
        PACL acl = nullptr;
        bool secOk = pipeSec::makeUserOnlySa(sd, sa, acl);
        HANDLE hPipe = CreateNamedPipeA(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 65536, 65536, 0, secOk ? &sa : nullptr);
        if (acl) LocalFree(acl);
        if (hPipe == INVALID_HANDLE_VALUE) break;

        // Register for the destructor's CancelIoEx (see ~AiPushServer).
        m_curPipe.store(hPipe, std::memory_order_relaxed);

        ResetEvent(hAccept);
        OVERLAPPED ov = {};
        ov.hEvent = hAccept;
        BOOL connOk = ConnectNamedPipe(hPipe, &ov);
        DWORD err = GetLastError();
        if (connOk || err == ERROR_PIPE_CONNECTED) {
            // Client connected (synchronously or in the accept gap).
        } else if (err == ERROR_IO_PENDING) {
            for (;;) {
                DWORD wr = WaitForSingleObject(hAccept, 200);
                if (wr == WAIT_OBJECT_0) break;          // connected
                if (!m_ready) {                          // teardown requested
                    CancelIoEx(hPipe, nullptr);
                    break;
                }
            }
            if (!m_ready) {
                CancelIoEx(hPipe, nullptr);
                m_curPipe.store(nullptr, std::memory_order_relaxed);
                CloseHandle(hPipe);
                break;
            }
        } else {
            m_curPipe.store(nullptr, std::memory_order_relaxed);
            CloseHandle(hPipe);
            break;
        }

        // Client connected: serve queued messages until it disconnects.
        opLog("aipush: client connected");
        for (;;) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lk(m_mu);
                m_cv.wait(lk, [this]() { return !m_ready || !m_queue.empty(); });
                if (!m_ready) {
                    DisconnectNamedPipe(hPipe);
                    m_curPipe.store(nullptr, std::memory_order_relaxed);
                    CloseHandle(hPipe);
                    CloseHandle(hAccept);
                    return;
                }
                msg = std::move(m_queue.front());
                m_queue.pop_front();
            }
            DWORD len = (DWORD)msg.size();
            if (!writeAll(hPipe, &len, 4)) break;         // client gone
            if (len > 0 && !writeAll(hPipe, msg.data(), len)) break;
            FlushFileBuffers(hPipe);
            opLog("aipush: wrote %u bytes", len);
        }
        opLog("aipush: client disconnected");
        DisconnectNamedPipe(hPipe);
        m_curPipe.store(nullptr, std::memory_order_relaxed);
        CloseHandle(hPipe);
        // Remaining queued messages survive in m_queue; the next client
        // connection (or the same client reconnecting) drains them.
    }

    CloseHandle(hAccept);
}
