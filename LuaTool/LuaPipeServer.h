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

#if defined(_WIN32)
#include <windows.h>   // HANDLE (m_curPipe)
#endif

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

private:
    void listenLoop();
    // Connect to our own pipe and send a STOP frame. Unblocks the listener
    // if it is parked in the accept wait; pairs with CancelIoEx (which
    // interrupts pipe reads/writes) so ~LuaPipeServer's join() cannot hang
    // forever waiting for a client that never sends data.
    void wakeUp();

    MainWindow  *m_owner;
    std::string  m_pipeName;
    std::atomic<bool> m_ready = false;
    // Current accepted pipe handle; written by the listener before each
    // blocking read/write, cleared before CloseHandle. The destructor reads
    // it to CancelIoEx the in-flight I/O (ReadFile/WriteFile have no
    // timeout; a stuck client would otherwise make join() hang forever).
    std::atomic<HANDLE> m_curPipe{nullptr};
    std::thread  m_thread;
};
