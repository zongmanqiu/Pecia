// PipeClient.cpp - generic named-pipe client.
#include "PipeClient.h"

#include "PipeProtocol.h"

#include <windows.h>
#include <cstring>

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

} // namespace

PipeReply pipeSend(const std::string &pipeName,
                   const std::string &tag,
                   const std::string &payload,
                   int waitMs) {
    PipeReply rep;
    if (pipeName.empty()) return rep;

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 25; ++attempt) {
        hPipe = CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return rep;   // peer not running
        WaitNamedPipeA(pipeName.c_str(), waitMs / 25 + 1);
    }
    if (hPipe == INVALID_HANDLE_VALUE) return rep;

    rep.peerConnected = true;

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    if (!writeAll(hPipe, tag.data(), 4)) {
        CloseHandle(hPipe);
        return rep;
    }
    DWORD len = (DWORD)payload.size();
    if (!writeAll(hPipe, &len, 4) ||
        (len > 0 && !writeAll(hPipe, payload.data(), len))) {
        CloseHandle(hPipe);
        return rep;
    }

    char rtag[4] = {0};
    bool ok = readAll(hPipe, rtag, 4);
    DWORD plen = 0;
    if (ok) ok = readAll(hPipe, &plen, 4) && plen <= (16 << 20);
    if (ok && plen > 0) {
        rep.payload.resize(plen);
        ok = readAll(hPipe, &rep.payload[0], plen);
    }
    CloseHandle(hPipe);

    rep.ok = ok && memcmp(rtag, PIPE_TAG_OK, 4) == 0;
    return rep;
}
