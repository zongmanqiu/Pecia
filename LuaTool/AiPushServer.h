// AiPushServer.h - one-way named-pipe push channel from the main Pecia
// process to the AI chat window (PeciaAIChat.exe).
//
// The AI window connects to \\.\pipe\pecia-ai-push-<pid> and polls it
// (PeekNamedPipe + timer). The main window calls push() to queue text;
// the server thread writes queued frames (4-byte little-endian length
// prefix + UTF-8 payload) to the connected client. Messages queued while
// no client is connected are delivered once the client (re)connects.
#pragma once

#include <windows.h>   // HANDLE (m_curPipe)

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class AiPushServer {
public:
    AiPushServer();
    ~AiPushServer();

    // Queue a message for the AI window (non-blocking).
    void push(const std::string &text);

    // Full push pipe name ("\\.\pipe\pecia-ai-push-<pid>").
    const std::string &pipeName() const { return m_pipeName; }

private:
    void listenLoop();

    std::string m_pipeName;
    std::atomic<bool> m_ready = false;
    // Handle of the currently accepted client pipe (or nullptr). The
    // destructor CancelIoEx's it: a connected-but-not-reading client can
    // leave the listener blocked inside a synchronous WriteFile forever,
    // and notify_all() cannot reach a thread parked in kernel I/O - only
    // cancelling the I/O can (otherwise join() would hang the process).
    std::atomic<HANDLE> m_curPipe = nullptr;
    std::thread m_thread;
    std::mutex  m_mu;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;
};
