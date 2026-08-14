// AiPushServer.cpp - see AiPushServer.h.
#include "LuaTool/AiPushServer.h"

#include "core/OpLog.h"
#include <windows.h>

#include <cstdio>

namespace {

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
    m_ready = false;
    m_cv.notify_all();
    m_thread.detach();   // blocked in ConnectNamedPipe; process exit kills it
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
    for (;;) {
        HANDLE hPipe = CreateNamedPipeA(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 65536, 65536, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) return;

        if (!ConnectNamedPipe(hPipe, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe);
            continue;
        }

        // Client connected: serve queued messages until it disconnects.
        opLog("aipush: client connected");
        for (;;) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lk(m_mu);
                m_cv.wait(lk, [this]() { return !m_ready || !m_queue.empty(); });
                if (!m_ready) {
                    CloseHandle(hPipe);
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
        CloseHandle(hPipe);
        // Remaining queued messages survive in m_queue; the next client
        // connection (or the same client reconnecting) drains them.
    }
}
