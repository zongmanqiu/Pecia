// PipeClient.cpp - generic named-pipe client.
#include "PipeClient.h"

#include "PipeProtocol.h"

#include <windows.h>
#include <cstring>

namespace {

// Read exactly n bytes from an OVERLAPPED pipe, bounded by a per-call
// timeout (ms). Returns false on timeout (half a frame read) or I/O error.
// Opening the pipe with FILE_FLAG_OVERLAPPED lets us abort a stalled peer
// instead of blocking forever when the server hangs mid-reply.
bool readAll(HANDLE h, void *buf, DWORD n, DWORD timeoutMs, HANDLE ev) {
    char *p = static_cast<char *>(buf);
    while (n > 0) {
        DWORD got = 0;
        OVERLAPPED ov = {};
        ov.hEvent = ev;
        if (!ReadFile(h, p, n, &got, &ov)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD wr = WaitForSingleObject(ev, timeoutMs);
                if (wr != WAIT_OBJECT_0) {   // timeout / error
                    CancelIoEx(h, nullptr);
                    return false;
                }
                if (!GetOverlappedResult(h, &ov, &got, FALSE)) return false;
            } else {
                return false;
            }
        }
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

} // namespace

PipeReply pipeSend(const std::string &pipeName,
                   const std::string &tag,
                   const std::string &payload,
                   int waitMs) {
    PipeReply rep;
    if (pipeName.empty()) return rep;

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 25; ++attempt) {
        // Overlapped open so the reply read can time out.
        hPipe = CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return rep;   // peer not running
        WaitNamedPipeA(pipeName.c_str(), waitMs / 25 + 1);
    }
    if (hPipe == INVALID_HANDLE_VALUE) return rep;

    rep.peerConnected = true;

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    // A pending-overlap event used for the (timeout-bounded) reads. Writes
    // stay synchronous; reads are the only point a stalled peer could hang us.
    HANDLE hReadEv = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!hReadEv) {
        CloseHandle(hPipe);
        return rep;
    }
    // Half of the caller's budget goes to reading (the rest was the
    // connect attempts); a server that stalls mid-reply then times out.
    const DWORD readTimeout = (DWORD)(waitMs > 0 ? waitMs / 2 : 3000);

    if (!writeAll(hPipe, tag.data(), 4)) {
        CloseHandle(hReadEv); CloseHandle(hPipe);
        return rep;
    }
    DWORD len = (DWORD)payload.size();
    if (!writeAll(hPipe, &len, 4) ||
        (len > 0 && !writeAll(hPipe, payload.data(), len))) {
        CloseHandle(hReadEv); CloseHandle(hPipe);
        return rep;
    }

    char rtag[4] = {0};
    bool ok = readAll(hPipe, rtag, 4, readTimeout, hReadEv);
    DWORD plen = 0;
    if (ok) ok = readAll(hPipe, &plen, 4, readTimeout, hReadEv) && plen <= (16 << 20);
    if (ok && plen > 0) {
        rep.payload.resize(plen);
        ok = readAll(hPipe, &rep.payload[0], plen, readTimeout, hReadEv);
    }
    CloseHandle(hReadEv);
    CloseHandle(hPipe);

    rep.ok = ok && memcmp(rtag, PIPE_TAG_OK, 4) == 0;
    return rep;
}
