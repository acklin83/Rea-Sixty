#include "WebSocket.h"

#include "Sha256.h"
#include "WDL/sha.h"

#include <cctype>
#include <cstdio>

namespace orc::ws {

std::string acceptKey(const std::string& clientKey)
{
    static const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string s = clientKey + kGuid;
    WDL_SHA1 sha;
    sha.add(s.data(), static_cast<int>(s.size()));
    unsigned char raw[WDL_SHA1SIZE];
    sha.result(raw);
    return reasixty::base64Encode(raw, sizeof(raw));
}

std::string encodeFrame(std::uint8_t opcode, const std::string& payload)
{
    std::string f;
    const std::size_t n = payload.size();
    f.push_back(static_cast<char>(0x80 | (opcode & 0x0F)));
    if (n < 126) {
        f.push_back(static_cast<char>(n));
    } else if (n < 65536) {
        f.push_back(static_cast<char>(126));
        f.push_back(static_cast<char>((n >> 8) & 0xFF));
        f.push_back(static_cast<char>(n & 0xFF));
    } else {
        f.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (8 * i)) & 0xFF));
    }
    return f + payload;
}

Take takeFrame(std::string& buf, Frame& out)
{
    if (buf.size() < 2) return Take::Need;
    const auto b0 = static_cast<unsigned char>(buf[0]);
    const auto b1 = static_cast<unsigned char>(buf[1]);
    if (b0 & 0x70) return Take::Bad;           // RSV1-3: no extension was agreed
    if (!(b1 & 0x80)) return Take::Bad;        // a client MUST mask
    const std::uint8_t op = b0 & 0x0F;
    const bool fin = (b0 & 0x80) != 0;
    std::uint64_t len = b1 & 0x7F;
    std::size_t off = 2;
    if (len == 126) {
        if (buf.size() < 4) return Take::Need;
        len = (static_cast<std::uint64_t>(static_cast<unsigned char>(buf[2])) << 8)
            | static_cast<unsigned char>(buf[3]);
        off = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return Take::Need;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<unsigned char>(buf[2 + i]);
        off = 10;
    }
    if (op >= 0x8 && (len > 125 || !fin)) return Take::Bad;   // control frames (5.5)
    if (len > kMaxMessage) return Take::Bad;
    if (buf.size() < off + 4) return Take::Need;
    unsigned char mask[4];
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<unsigned char>(buf[off + i]);
    off += 4;
    if (buf.size() < off + len) return Take::Need;
    out.opcode = op;
    out.fin    = fin;
    out.payload.assign(buf, off, static_cast<std::size_t>(len));
    for (std::size_t i = 0; i < out.payload.size(); ++i)
        out.payload[i] = static_cast<char>(static_cast<unsigned char>(out.payload[i]) ^ mask[i & 3]);
    buf.erase(0, off + static_cast<std::size_t>(len));
    return Take::Frame;
}

Reader::Got Reader::feed(const Frame& f, std::string& out)
{
    switch (f.opcode) {
        case kPing:  out = f.payload; return Got::Ping;
        case kPong:  return Got::Nothing;
        case kClose: return Got::Close;
        case kText:
        case kBinary:
            if (inMessage_) return Got::Bad;   // a new message inside an unfinished one
            if (f.fin) {
                if (f.opcode != kText) return Got::Nothing;   // binary: not ours, ignored
                out = f.payload;
                return Got::Message;
            }
            inMessage_ = true;
            isText_    = f.opcode == kText;
            msg_       = f.payload;
            return Got::Nothing;
        case kContinuation:
            if (!inMessage_) return Got::Bad;
            if (msg_.size() + f.payload.size() > kMaxMessage) return Got::Bad;
            msg_ += f.payload;
            if (!f.fin) return Got::Nothing;
            inMessage_ = false;
            if (!isText_) { msg_.clear(); return Got::Nothing; }
            out.swap(msg_);
            msg_.clear();
            return Got::Message;
        default:
            return Got::Bad;
    }
}

// ── HTTP ─────────────────────────────────────────────────────────────────────

namespace {

std::string trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "keep-alive, Upgrade" contains the token "upgrade".
bool hasToken(const std::string& value, const std::string& token)
{
    std::size_t at = 0;
    const std::string v = lower(value);
    while (at <= v.size()) {
        const std::size_t c = v.find(',', at);
        const std::string part = trim(v.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (part == token) return true;
        if (c == std::string::npos) break;
        at = c + 1;
    }
    return false;
}

const char* reason(int status)
{
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "Error";
    }
}

} // namespace

bool parseRequest(const std::string& head, Request& out)
{
    const std::size_t eol = head.find("\r\n");
    if (eol == std::string::npos) return false;
    const std::string line = head.substr(0, eol);
    const std::size_t s1 = line.find(' ');
    const std::size_t s2 = s1 == std::string::npos ? std::string::npos : line.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos) return false;
    if (line.compare(s2 + 1, 7, "HTTP/1.") != 0) return false;
    out.method = line.substr(0, s1);
    std::string target = line.substr(s1 + 1, s2 - s1 - 1);
    const std::size_t q = target.find_first_of("?#");
    out.path = q == std::string::npos ? target : target.substr(0, q);
    out.headers.clear();
    std::size_t at = eol + 2;
    while (at < head.size()) {
        const std::size_t e = head.find("\r\n", at);
        const std::string h = head.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (h.empty()) break;
        const std::size_t colon = h.find(':');
        if (colon != std::string::npos)
            out.headers[lower(trim(h.substr(0, colon)))] = trim(h.substr(colon + 1));
        if (e == std::string::npos) break;
        at = e + 2;
    }
    return !out.method.empty() && !out.path.empty();
}

bool isUpgrade(const Request& r)
{
    return r.method == "GET" && lower(r.header("upgrade")) == "websocket"
        && hasToken(r.header("connection"), "upgrade")
        && !r.header("sec-websocket-key").empty();
}

std::string upgradeResponse(const Request& r)
{
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + acceptKey(r.header("sec-websocket-key")) + "\r\n\r\n";
}

std::string response(int status, const std::string& contentType, const std::string& body)
{
    char head[256];
    std::snprintf(head, sizeof(head),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %zu\r\n"
                  "Cache-Control: no-cache\r\n"
                  "Connection: close\r\n\r\n",
                  status, reason(status), contentType.c_str(), body.size());
    return head + body;
}

} // namespace orc::ws
