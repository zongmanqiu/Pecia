// AiApiClient.h - minimal OpenAI-compatible chat client.
// Pure WinHTTP + manual JSON; no FLTK dependency so it can run on a
// background thread. Synchronous: the caller is responsible for threading.
#pragma once

#include <atomic>
#include <string>
#include <vector>

struct AiChatMessage {
    // std::string (not const char*): callers build the vector from SSO-sized
    // literals and history entries; a pointer member invites a dangling read
    // once the owning container is cleared while a worker still holds a copy.
    std::string role;      // "system" / "user" / "assistant"
    std::string content;
};

struct AiChatResult {
    bool    ok = false;      // HTTP 200 + content extracted
    int     httpStatus = 0;  // 0 = transport error
    std::string content;     // choices[0].message.content (empty if none)
    std::string error;       // human-readable error (transport / HTTP / parse)
};

// POST {model, messages, temperature, stream} to `endpoint` (a FULL URL
// such as https://open.bigmodel.cn/api/paas/v4/chat/completions) with
// Bearer auth. `timeoutMs` covers connect/send/receive.
// No max_tokens is sent: there is no setting for it anywhere in the project,
// so the provider's own default applies (the previous comment was stale).
//
// Only standard OpenAI-compatible fields are sent, so the configured
// provider (Zhipu by default) accepts it.
// cancelFlag: if non-null and set to true during the request, the HTTP
// connection is closed immediately and an error is returned.
AiChatResult aiChatCompletion(const std::string &apiKey,
                              const std::string &model,
                              const std::vector<AiChatMessage> &messages,
                              const std::string &endpoint,
                              int timeoutMs = 300000,
                              std::atomic<bool> *cancelFlag = nullptr);

// Escape a string for embedding in a JSON string literal.
std::string aiJsonEscape(const std::string &s);
