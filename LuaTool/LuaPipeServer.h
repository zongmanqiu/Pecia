// LuaPipeServer.h - named-pipe server in the main Pecia process.
// PeciaLua.exe / PeciaAIChat.exe connect to \\.\pipe\pecia-lua-<pid> and
// send tagged frames:
//   RUN <script> -> runs the script on the active document
//   INS <text>   -> inserts text at the cursor / replaces the selection
//   GSEL         -> returns the current selection text (OK <text> / ERR)
// Work is marshalled to the UI thread via Fl::awake.
#pragma once

#include <string>

class MainWindow;

class LuaPipeServer {
public:
    // Starts the listener thread. `owner` executes the requests.
    explicit LuaPipeServer(MainWindow *owner);
    ~LuaPipeServer();

    // Full pipe name the tools should connect to ("\\.\pipe\pecia-lua-<pid>").
    const char *pipeName() const { return m_pipeName.c_str(); }

    bool ready() const { return m_ready; }

private:
    void listenLoop();

    MainWindow  *m_owner;
    std::string  m_pipeName;
    bool         m_ready = false;
};
