// McpServer.h - MCP stdio endpoint for Pecia (m021).
//
// Transport: newline-delimited JSON-RPC on stdin/stdout (NDJSON).
// stdout carries ONLY protocol frames; diagnostics go to stderr (which is
// typically an invalid handle in this GUI exe - silently dropped, matching
// the product's zero-console-noise behavior).
//
// Protocol stance (2026-11-25 research, spec checklists g5):
//   * supports 2025-03-26 / 2025-06-18 / 2025-11-25; an unknown requested
//     version is answered with the newest supported one, never an error;
//   * tool semantic failures come back as results with isError:true
//     (SEP-1303); JSON-RPC errors are reserved for protocol shapes
//     (-32700 parse, -32600 invalid request, -32601 method not found,
//     -32602 unknown tool / bad params object, -32603 internal incl. the
//     self-chosen dispatch-timeout code);
//   * an internal failure (the registry's "internal:" prefix: a dispatch
//     timeout, a handler exception) is a -32603 JSON-RPC error and NOT an
//     isError result: the queued packet may still land after the timeout,
//     so an isError would promise "nothing changed" and invite a retry
//     that applies the edit twice;
//   * notifications (a request without an id) are consumed and NEVER
//     answered - every method, initialize included. A tools/call sent as a
//     notification is EXECUTED (the tool really runs) and still not
//     answered; a peer sending a RESPONSE is not answered either.
//
// Threading: runStdioLoop() executes on a detached worker thread created
// by main(); every tool call is marshalled to the UI thread through
// HostRegistry::dispatch. The thread is NEVER joined at exit: a blocking
// ReadFile on stdin cannot be interrupted portably, and joining it would
// hang process shutdown (contract copied from cxxmcp's transport notes).
#pragma once

#include <string>

namespace mcp {

// True while the --mcp-stdio endpoint mode was requested (read by main.cpp
// to skip startup conveniences that would surprise an agent).
bool stdioModeRequested(int argc, char **argv);

// Main loop: returns on stdin EOF. serverInfo comes from the build's own
// identity (PECIA_VERSION), not from a string the caller can get wrong.
// Never throws.
void runStdioLoop();

// Scripted in-process selftest (no stdio, no UI thread): drives handleLine
// against a fixture registry and the JSON layer, verifies the fake-server
// nails (wrong version tolerated, unknown method -> -32601, unknown tool
// -> -32602, empty required stays [], lone surrogate -> U+FFFD, floats
// rejected...). Returns 0 when every check passed, prints FAIL lines to
// stderr otherwise. Also used by ctest target mcp_selftest via Pecia.exe.
int runSelfTest();

} // namespace mcp
