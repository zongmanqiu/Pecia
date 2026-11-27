// McpJson.cpp - see McpJson.h.
#include "McpJson.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace mcp {
namespace {

void encodeInto(const OpValue &v, std::string &out, int depth);

// Length of the UTF-8 sequence starting at s[i], or 0 when those bytes are
// not a valid sequence (bad continuation, overlong, surrogate half, out of
// range, truncated). Valid sequences are copied through verbatim; broken
// ones are replaced by U+FFFD so a frame is always valid UTF-8.
int utf8SeqLen(const std::string &s, size_t i)
{
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) return 1;
    if (c < 0xC2) return 0;                       // continuation byte / overlong
    if (c < 0xE0) {                               // 2-byte: C2..DF
        if (i + 1 >= s.size()) return 0;
        const unsigned char c1 = (unsigned char)s[i + 1];
        return (c1 >= 0x80 && c1 <= 0xBF) ? 2 : 0;
    }
    if (c < 0xF0) {                               // 3-byte: E0..EF
        if (i + 2 >= s.size()) return 0;
        const unsigned char c1 = (unsigned char)s[i + 1];
        const unsigned char c2 = (unsigned char)s[i + 2];
        if (c1 < 0x80 || c1 > 0xBF) return 0;
        if (c2 < 0x80 || c2 > 0xBF) return 0;
        if (c == 0xE0 && c1 < 0xA0) return 0;     // overlong
        if (c == 0xED && c1 > 0x9F) return 0;     // surrogate half
        return 3;
    }
    if (c < 0xF5) {                               // 4-byte: F0..F4
        if (i + 3 >= s.size()) return 0;
        const unsigned char c1 = (unsigned char)s[i + 1];
        const unsigned char c2 = (unsigned char)s[i + 2];
        const unsigned char c3 = (unsigned char)s[i + 3];
        if (c1 < 0x80 || c1 > 0xBF) return 0;
        if (c2 < 0x80 || c2 > 0xBF) return 0;
        if (c3 < 0x80 || c3 > 0xBF) return 0;
        if (c == 0xF0 && c1 < 0x90) return 0;     // overlong
        if (c == 0xF4 && c1 > 0x8F) return 0;     // beyond U+10FFFF
        return 4;
    }
    return 0;
}

void encodeString(const std::string &s, std::string &out)
{
    out += '"';
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
            }
            ++i;
            continue;
        }
        // Non-ASCII: keep valid UTF-8 as is, replace broken bytes with
        // U+FFFD. A frame carrying raw invalid UTF-8 is rejected WHOLE by a
        // strict client parser - the tool result would be lost with it.
        const int len = utf8SeqLen(s, i);
        if (len <= 0) {
            out += "\xEF\xBF\xBD";
            ++i;
        } else {
            out.append(s, i, (size_t)len);
            i += (size_t)len;
        }
    }
    out += '"';
}

void encodeInto(const OpValue &v, std::string &out, int depth)
{
    switch (v.kind()) {
    case OpValue::Kind::Null: out += "null"; break;
    case OpValue::Kind::Bool: out += v.getBool() ? "true" : "false"; break;
    case OpValue::Kind::Int: {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%lld", v.getInt());
        out += buf;
        break;
    }
    case OpValue::Kind::Str: encodeString(v.getString(), out); break;
    case OpValue::Kind::List: {
        // Depth rule, shared with the parser (see Parser::parseValue): a
        // container is dropped at the cap. The parser rejects the same
        // depth, so what it accepts is never rewritten by the encoder.
        if (depth >= kMaxDepth) { out += "null"; return; }   // defensive: builders are ours
        out += '[';
        bool first = true;
        for (const auto &e : v.list()) {
            if (!first) out += ',';
            first = false;
            encodeInto(e, out, depth + 1);
        }
        out += ']';
        break;
    }
    case OpValue::Kind::Map: {
        if (depth >= kMaxDepth) { out += "null"; return; }
        out += '{';
        bool first = true;
        for (const auto &kv : v.map()) {
            if (!first) out += ',';
            first = false;
            encodeString(kv.first, out);
            out += ':';
            encodeInto(kv.second, out, depth + 1);
        }
        out += '}';
        break;
    }
    }
}

struct Parser {
    const char *p;
    const char *end;
    std::string err;

    bool fail(const char *msg) { if (err.empty()) err = msg; return false; }

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool parseValue(OpValue &out, int depth)
    {
        skipWs();
        if (p >= end) return fail("unexpected end of input");
        char c = *p;
        if (c == '"') return parseString(out);
        // Depth cap on CONTAINERS only: the cap is a NESTING limit, and a
        // scalar inside a legal container is not a new level. depth counts
        // the enclosing containers (the top-level container is depth 0), so
        // kMaxDepth levels of containers are legal. The encoder applies the
        // same predicate (depth >= kMaxDepth) on the same quantity - both
        // sides agree on the boundary and a value the parser accepts is
        // never rewritten by the encoder.
        if (c == '{') {
            if (depth >= kMaxDepth) return fail("depth exceeds limit");
            return parseObject(out, depth);
        }
        if (c == '[') {
            if (depth >= kMaxDepth) return fail("depth exceeds limit");
            return parseArray(out, depth);
        }
        if (c == 't') {
            if (end - p >= 4 && !std::strncmp(p, "true", 4))  { p += 4; out = OpValue::makeBool(true);  return true; }
            return fail("bad literal");
        }
        if (c == 'f') {
            if (end - p >= 5 && !std::strncmp(p, "false", 5)) { p += 5; out = OpValue::makeBool(false); return true; }
            return fail("bad literal");
        }
        if (c == 'n') {
            if (end - p >= 4 && !std::strncmp(p, "null", 4))  { p += 4; out = OpValue();              return true; }
            return fail("bad literal");
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character");
    }

    bool parseNumber(OpValue &out)
    {
        const char *start = p;
        if (p < end && *p == '-') ++p;
        int digits = 0;
        if (p < end && *p == '0') {
            // JSON forbids leading zeros: "0" and "-0" are legal, "01" is not
            ++p;
            digits = 1;
            if (p < end && *p >= '0' && *p <= '9') return fail("leading zero in number");
        } else {
            while (p < end && *p >= '0' && *p <= '9') { ++p; ++digits; }
        }
        if (!digits) return fail("bad number");
        // floats are out of contract: reject fractional/exponent to keep
        // int64 semantics honest instead of silently truncating
        if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) return fail("floats unsupported");
        std::string num(start, (size_t)(p - start));
        errno = 0;
        long long v = std::strtoll(num.c_str(), nullptr, 10);
        if (errno == ERANGE) return fail("integer out of range");
        out = OpValue::makeInt(v);
        return true;
    }

    void utf8Append(std::string &s, unsigned cp)
    {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned &v)
    {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i, ++p) {
            char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(OpValue &out)
    {
        std::string s;
        ++p;   // opening quote
        while (true) {
            if (p >= end) return fail("unterminated string");
            char c = *p++;
            if (c == '"') break;
            if ((unsigned char)c < 0x20) return fail("raw control char in string");
            if (c != '\\') { s += c; if (s.size() > kMaxTextLen) return fail("string too long"); continue; }
            if (p >= end) return fail("unterminated escape");
            char e = *p++;
            switch (e) {
            case '"':  s += '"';  break;
            case '\\': s += '\\'; break;
            case '/':  s += '/';  break;
            case 'b':  s += '\b'; break;
            case 'f':  s += '\f'; break;
            case 'n':  s += '\n'; break;
            case 'r':  s += '\r'; break;
            case 't':  s += '\t'; break;
            case 'u': {
                unsigned hi = 0;
                if (!hex4(hi)) { --p; return fail("bad \\u escape"); }
                if (hi >= 0xD800 && hi <= 0xDBFF) {
                    // surrogate pair: \uXXXX\uXXXX
                    if (end - p >= 2 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo = 0;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { --p; return fail("bad surrogate pair"); }
                        utf8Append(s, 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00));
                    } else {
                        utf8Append(s, 0xFFFD);   // lone high surrogate
                    }
                } else if (hi >= 0xDC00 && hi <= 0xDFFF) {
                    utf8Append(s, 0xFFFD);       // lone low surrogate
                } else {
                    utf8Append(s, hi);
                }
                break;
            }
            default: return fail("bad escape");
            }
            if (s.size() > kMaxTextLen) return fail("string too long");
        }
        out = OpValue::makeStr(std::move(s));
        return true;
    }

    bool parseObject(OpValue &out, int depth)
    {
        OpValue m = OpValue::makeMap();
        ++p;   // '{'
        skipWs();
        if (p < end && *p == '}') { ++p; out = std::move(m); return true; }
        while (true) {
            skipWs();
            if (p >= end || *p != '"') return fail("expected string key");
            OpValue key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') return fail("expected ':'");
            ++p;
            OpValue val;
            if (!parseValue(val, depth + 1)) return false;
            m.set(key.getString(), std::move(val));
            skipWs();
            if (p >= end) return fail("unterminated object");
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; break; }
            return fail("expected ',' or '}'");
        }
        out = std::move(m);
        return true;
    }

    bool parseArray(OpValue &out, int depth)
    {
        OpValue l = OpValue::makeList();
        ++p;   // '['
        skipWs();
        if (p < end && *p == ']') { ++p; out = std::move(l); return true; }
        while (true) {
            OpValue val;
            if (!parseValue(val, depth + 1)) return false;
            l.push(std::move(val));
            skipWs();
            if (p >= end) return fail("unterminated array");
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; break; }
            return fail("expected ',' or ']'");
        }
        out = std::move(l);
        return true;
    }
};

} // namespace

std::string jsonEncode(const OpValue &v)
{
    std::string out;
    encodeInto(v, out, 0);
    return out;
}

bool jsonParse(const std::string &text, OpValue &out, std::string &err)
{
    if (text.size() > (kMaxLineLen << 2)) { err = "input too large"; return false; }
    Parser ps{text.data(), text.data() + text.size(), std::string()};
    OpValue v;
    if (!ps.parseValue(v, 0)) { err = ps.err; return false; }
    ps.skipWs();
    if (ps.p != ps.end) { err = "trailing data after JSON value"; return false; }
    out = std::move(v);
    err.clear();
    return true;
}

std::string sanitizeText(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '\n' || c == '\t') out += (char)c;
        else if (c < 0x20 || c == 0x7F) out += ' ';
        else out += (char)c;
    }
    return out;
}

} // namespace mcp
