// orc/WebSocket: the frame format ORC MoreMe speaks with the page
// (docs/orc-phones-plan.md, stage 2). The byte sequences are RFC 6455's own
// examples (section 1.3 for the handshake, 5.7 for the frames).

#include "WebSocket.h"

#include <cstdio>
#include <string>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace orc::ws;

static std::string bytes(std::initializer_list<int> b)
{
    std::string s;
    for (const int c : b) s.push_back(static_cast<char>(c));
    return s;
}

// A client frame: masked, as every browser sends it.
static std::string clientFrame(int b0, const std::string& payload)
{
    const unsigned char mask[4] = { 0x37, 0xfa, 0x21, 0x3d };
    std::string f;
    f.push_back(static_cast<char>(b0));
    const std::size_t n = payload.size();
    if (n < 126) f.push_back(static_cast<char>(0x80 | n));
    else { f.push_back(static_cast<char>(0x80 | 126)); f.push_back(static_cast<char>(n >> 8)); f.push_back(static_cast<char>(n & 0xFF)); }
    for (const unsigned char m : mask) f.push_back(static_cast<char>(m));
    for (std::size_t i = 0; i < n; ++i)
        f.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i & 3]));
    return f;
}

int main()
{
    // 1.3: the handshake's example key.
    EXPECT(acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // 5.7: unmasked "Hello" from the server.
    EXPECT(encodeFrame(kText, "Hello") == bytes({ 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f }));
    // 5.7: 256 bytes take the 16-bit length, 64 KiB the 64-bit one.
    {
        const std::string f = encodeFrame(kBinary, std::string(256, 'x'));
        EXPECT(f.substr(0, 4) == bytes({ 0x82, 0x7E, 0x01, 0x00 }) && f.size() == 4 + 256);
        const std::string g = encodeFrame(kBinary, std::string(65536, 'x'));
        EXPECT(g.substr(0, 10) == bytes({ 0x82, 0x7F, 0, 0, 0, 0, 0, 1, 0, 0 }) && g.size() == 10 + 65536);
        // 125 still fits in the first byte, 126 does not.
        EXPECT(encodeFrame(kText, std::string(125, 'a'))[1] == 125);
        EXPECT(static_cast<unsigned char>(encodeFrame(kText, std::string(126, 'a'))[1]) == 126);
    }

    // 5.7: a masked "Hello" from the client, byte for byte.
    {
        std::string buf = bytes({ 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 });
        Frame f;
        EXPECT(takeFrame(buf, f) == Take::Frame);
        EXPECT(f.opcode == kText && f.fin && f.payload == "Hello" && buf.empty());
        EXPECT(clientFrame(0x81, "Hello")
               == bytes({ 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 }));
    }
    // Arriving in pieces: Need until whole, then the frame, the rest stays.
    {
        const std::string whole = clientFrame(0x81, std::string(300, 'z')) + clientFrame(0x89, "p");
        std::string buf;
        Frame f;
        for (std::size_t i = 0; i < whole.size(); ++i) {
            buf.push_back(whole[i]);
            if (i + 1 > 4 + 4 + 300) continue;   // the ping: taken below
            const Take t = takeFrame(buf, f);
            if (i + 1 < 4 + 4 + 300) { EXPECT(t == Take::Need); }
            else { EXPECT(t == Take::Frame && f.payload == std::string(300, 'z')); }
        }
        EXPECT(takeFrame(buf, f) == Take::Frame && f.opcode == kPing && f.payload == "p");
    }
    // ⛔ What a server must refuse.
    {
        Frame f;
        std::string unmasked = bytes({ 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f });
        EXPECT(takeFrame(unmasked, f) == Take::Bad);
        std::string rsv = clientFrame(0xC1, "x");                    // RSV1
        EXPECT(takeFrame(rsv, f) == Take::Bad);
        std::string bigPing = clientFrame(0x89, std::string(126, 'p'));
        EXPECT(takeFrame(bigPing, f) == Take::Bad);
        std::string fragPing = clientFrame(0x09, "p");               // control without FIN
        EXPECT(takeFrame(fragPing, f) == Take::Bad);
        std::string huge = bytes({ 0x81, 0xFF, 0, 0, 0, 0, 0, 2, 0, 0 });   // 128 KiB announced
        EXPECT(takeFrame(huge, f) == Take::Bad);
    }

    // 5.7: a fragmented text, put back together; a ping in between is answered.
    {
        Reader r;
        std::string buf = clientFrame(0x01, "Hel") + clientFrame(0x89, "ping") + clientFrame(0x80, "lo");
        Frame f;
        std::string out;
        EXPECT(takeFrame(buf, f) == Take::Frame);
        EXPECT(r.feed(f, out) == Reader::Got::Nothing);
        EXPECT(takeFrame(buf, f) == Take::Frame);
        EXPECT(r.feed(f, out) == Reader::Got::Ping && out == "ping");
        EXPECT(takeFrame(buf, f) == Take::Frame);
        EXPECT(r.feed(f, out) == Reader::Got::Message && out == "Hello");
        // A continuation with nothing to continue, and a close.
        Frame c; c.opcode = kContinuation; c.fin = true;
        EXPECT(r.feed(c, out) == Reader::Got::Bad);
        Frame cl; cl.opcode = kClose;
        EXPECT(r.feed(cl, out) == Reader::Got::Close);
        // A binary message is not ours and is dropped.
        Frame b; b.opcode = kBinary; b.payload = "x";
        EXPECT(r.feed(b, out) == Reader::Got::Nothing);
    }

    // HTTP: the browser's upgrade request (Safari's header case), and a page.
    {
        Request r;
        EXPECT(parseRequest("GET /ws HTTP/1.1\r\nHost: studio:8088\r\nUpgrade: websocket\r\n"
                            "Connection: keep-alive, Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                            "X-Real-IP:  85.1.2.3 \r\n\r\n", r));
        EXPECT(r.method == "GET" && r.path == "/ws");
        EXPECT(r.header("x-real-ip") == "85.1.2.3");
        EXPECT(isUpgrade(r));
        EXPECT(upgradeResponse(r) == "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                     "Connection: Upgrade\r\nSec-WebSocket-Accept: "
                                     "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n");
        Request p;
        EXPECT(parseRequest("GET /?x=1 HTTP/1.1\r\nHost: a\r\n\r\n", p));
        EXPECT(p.path == "/" && !isUpgrade(p));
        EXPECT(!parseRequest("hello\r\n\r\n", p));
        EXPECT(response(404, "text/plain", "not found")
               == "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n"
                  "Cache-Control: no-cache\r\nConnection: close\r\n\r\nnot found");
    }

    if (g_fail) { std::printf("test_orc_websocket: %d failure(s)\n", g_fail); return 1; }
    std::printf("test_orc_websocket: ok\n");
    return 0;
}
