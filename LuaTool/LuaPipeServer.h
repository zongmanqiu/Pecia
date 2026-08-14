// LuaPipeServer.h - named-pipe server in the main Pecia process.
// PeciaLua.exe / PeciaAIChat.exe connect to \\.\pipe\pecia-lua-<pid> and
// send tagged frames:
//   RUN <script> -> runs the script on the active document
//   INS <text>   -> inserts text at the cursor / replaces the selection
//   GSEL         -> returns the current selection text (OK <text> / ERR)
// Work is marshalled to the UI thread via Fl::awake.
#pragma once

#include <atomic>
#include <string>
#include <thread>

class MainWindow;

class LuaPipeServer {
public:
    // Starts the listener thread. `owner` executes the requests.
    explicit LuaPipeServer(MainWindow *owner);
    // Stops the listener thread and joins it (safe teardown).
    ~LuaPipeServer();

    LuaPipeServer(const LuaPipeServer &) = delete;
    LuaPipeServer &operator=(const LuaPipeServer &) = delete;

    // Full pipe name the tools should connect to ("\\.\pipe\pecia-lua-<pid>").
    const char *pipeName() const { return m_pipeName.c_str(); }

    bool ready() const { return m_ready.load(); }

private:
    void listenLoop();

    MainWindow  *m_owner;
    std::string  m_pipeName;
    std::atomic<bool> m_ready = false;
    std::thread  m_thread;
};
