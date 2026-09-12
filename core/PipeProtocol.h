// PipeProtocol.h - shared frame tags for the inter-process pipes.
// Frame: 4-byte tag + u32 LE length + payload.
//   Pecia  (per-pid pipe, passed to the tools at launch):
//     RUN <script> -> OK <print output> | ERR <message>
//     INS <text>   -> OK                | ERR <message>   (insert at cursor /
//                                                          replace selection)
//   PeciaLua (fixed pipe \\.\pipe\pecia-lua-tool):
//     INS <text>   -> OK | ERR   (insert into the Lua tool's script editor)
#pragma once

#define PIPE_TAG_RUN "RUN "
#define PIPE_TAG_INS "INS "
#define PIPE_TAG_GETSEL "GSEL"
#define PIPE_TAG_GETDOC "GDOC"
#define PIPE_TAG_APPLY "APPL"
#define PIPE_TAG_SEND "SEND"   // Lua console -> main -> AIChat: forward text to AI
#define PIPE_TAG_CLRSEL "CLRS"
#define PIPE_TAG_RELOAD "RLOD"
#define PIPE_TAG_OK  "OK  "
#define PIPE_TAG_ERR "ERR "
#define PIPE_TAG_STOP "STOP"   // internal: teardown wake-up frame (server sends
                               // it to itself; listener exits without replying)

// Fixed pipe name of the Lua tool (single instance -> fixed name works).
#define PIPE_LUA_TOOL "\\\\.\\pipe\\pecia-lua-tool"

// Single-instance mutex names for the standalone tools.
#define MUTEX_LUA_TOOL "PeciaLua.Singleton"
#define MUTEX_AI_CHAT  "PeciaAIChat.Singleton"
