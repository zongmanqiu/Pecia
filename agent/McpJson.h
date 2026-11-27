// McpJson.h - JSON encode/parse over core/HostOps' OpValue (m021).
//
// Scope is deliberately tiny: the MCP endpoint is the only consumer, it
// only ever moves JSON objects/arrays/strings/ints/bools, and OpValue
// already models exactly that subset. No floats, no pretty mode, no
// arbitrary-precision numbers - ints are int64 and that is the contract.
//
// Hard limits (protocol resilience rules):
//   kMaxDepth   nesting cap: at most kMaxDepth levels of nested containers
//               (the top-level container is level 1); deeper input is
//               rejected, not recursed to stack exhaustion (untrusted peer).
//               The encoder uses the same cap, so what the parser accepts
//               survives a round trip untouched.
//   kMaxTextLen single string cap; longer is a parse error. Kept equal to
//               kMaxLineLen so a string accepted by the parser can actually
//               arrive on one NDJSON line (the transport drops longer lines).
#pragma once

#include "core/HostOps.h"

#include <string>

namespace mcp {

constexpr int    kMaxDepth    = 32;
// kMaxLineLen is the NDJSON TRANSPORT limit (encoded bytes per line).
// kMaxTextLen is the semantic limit on a single decoded string. They are set
// equal here, but an accepted 1 MB string can still exceed the transport cap
// once encoded: control chars become \uXXXX (6 wire bytes per char) and
// non-ASCII UTF-8 can expand too, so a max-size text may be rejected on the
// wire. Callers must not assume "parses here" => "fits one NDJSON line".
constexpr size_t kMaxLineLen  = 1u << 20;    // 1 MB per NDJSON request line (encoded)
constexpr size_t kMaxTextLen  = kMaxLineLen; // single decoded string cap

// Compact encoder. Control characters are \u-escaped (NUL and friends must
// never reach the peer raw). Output is always valid UTF-8 JSON: valid
// multi-byte sequences pass through, broken byte sequences are replaced by
// U+FFFD (a strict client parser rejects a whole frame that carries raw
// invalid UTF-8, and the tool result would be lost with it).
std::string jsonEncode(const OpValue &v);

// Recursive-descent parser. On failure returns false + human-readable err;
// out is untouched. Leading/trailing whitespace tolerated; trailing
// non-whitespace rejected (NDJSON = exactly one value per line).
bool jsonParse(const std::string &text, OpValue &out, std::string &err);

// Replace control bytes (<0x20 except tab) and 0x7F in place - used on
// model-facing error strings coming from script output.
std::string sanitizeText(const std::string &in);

} // namespace mcp
