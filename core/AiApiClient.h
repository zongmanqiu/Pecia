// AiApiClient.h - minimal OpenAI-compatible chat client.
// Pure WinHTTP + manual JSON; no FLTK dependency so it can run on a
// background thread. Synchronous: the caller is responsible for threading.
#pragma once

#include <string>
#include <vector>

struct AiChatMessage {
    const char *role;      // "system" / "user" / "assistant"
    std::string content;
};

struct AiChatResult {
    bool    ok = false;      // HTTP 200 + content extracted
    int     httpStatus = 0;  // 0 = transport error
    std::string content;     // choices[0].message.content (empty if none)
    std::string error;       // human-readable error (transport / HTTP / parse)
};

// POST {model, messages, max_tokens, temperature} to `endpoint` (a FULL URL
// such as https://open.bigmodel.cn/api/paas/v4/chat/completions) with
// Bearer auth. `timeoutMs` covers connect/send/receive.
//
// Only standard OpenAI-compatible fields are sent, so the configured
// provider (Zhipu by default) accepts it.
AiChatResult aiChatCompletion(const std::string &apiKey,
                              const std::string &model,
                              const std::vector<AiChatMessage> &messages,
                              const std::string &endpoint,
                              int timeoutMs = 300000);

// Escape a string for embedding in a JSON string literal.
std::string aiJsonEscape(const std::string &s);
