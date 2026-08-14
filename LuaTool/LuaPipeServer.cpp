// LuaPipeServer.cpp - named-pipe server; requests run on the main UI thread.
#include "LuaPipeServer.h"

#include "ui/MainWindow.h"
#include "PipeProtocol.h"

#include <windows.h>
#include <FL/Fl.H>

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

const DWORD kFrameMax = 16 << 20;   // 16 MB frame cap (document snapshots)

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

// Request frame marshalled to the UI thread.
struct PipeRequest {
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

void s_runOnUiThread(void *data) {
    PipeRequest *r = static_cast<PipeRequest *>(data);
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
    std::thread([this]() { listenLoop(); }).detach();
}

LuaPipeServer::~LuaPipeServer() {
    m_ready = false;
}

void LuaPipeServer::listenLoop() {
    for (;;) {
        HANDLE hPipe = CreateNamedPipeA(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) return;

        if (!ConnectNamedPipe(hPipe, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe);
            continue;
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

        PipeRequest req;
        req.owner = m_owner;
        req.tag.assign(tag, 4);
        req.payload = std::move(payload);
        Fl::awake(s_runOnUiThread, &req);

        {
            std::unique_lock<std::mutex> lk(req.mu);
            req.cv.wait_for(lk, std::chrono::seconds(30), [&req]() { return req.done; });
        }

        const char *rtag = req.ok ? PIPE_TAG_OK : PIPE_TAG_ERR;
        const std::string &reply = req.ok ? req.out : req.err;
        writeAll(hPipe, rtag, 4);
        DWORD plen = (DWORD)reply.size();
        writeAll(hPipe, &plen, 4);
        if (plen > 0) writeAll(hPipe, reply.data(), plen);

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}
