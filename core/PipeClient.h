// PipeClient.h - generic named-pipe client: sends a tagged frame and
// waits for the OK/ERR reply. Used by PeciaLua (RUN to Pecia) and by
// AIChat (INS to Pecia and INS to PeciaLua).
#pragma once

#include <string>

struct PipeReply {
    bool        ok = false;              // peer answered "OK "
    bool        peerConnected = false;   // pipe opened at all
    std::string payload;                 // OK: output / ERR: message
};

// Send one frame to `pipeName` and read the reply. Returns
// peerConnected=false if the pipe could not be opened.
PipeReply pipeSend(const std::string &pipeName,
                   const std::string &tag,   // 4 chars, e.g. "RUN "
                   const std::string &payload,
                   int waitMs = 8000);
