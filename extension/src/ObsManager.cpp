#include "ObsManager.h"

#include "ObsProto.h"
#include "WsClient.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace reasixty::obs {
namespace {

// ExtState is INI-backed: a newline truncates the value silently and the whole
// config is gone at the next start. Same scrub as HueManager's.
std::string sanitize(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (const char c : s)
        if (c != '\n' && c != '\r' && c != '\t' && c != ';') o += c;
    return o;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else          { cur += c; }
    }
    out.push_back(cur);
    return out;
}

constexpr int  kPollMs        = 50;     // how long one read may wait
constexpr int  kConnectMs     = 2000;   // TCP + handshake deadline
constexpr long kRetryMs       = 3000;   // between attempts after a failure
constexpr long kStatusPollMs  = 5000;   // safety net under the events

}  // namespace

Manager::~Manager() { stop(); }

Manager& manager()
{
    static Manager m;
    return m;
}

void Manager::start()
{
    if (run_.exchange(true)) return;
    try {
        worker_ = std::thread([this] { workerLoop(); });
    } catch (...) {
        run_.store(false);                    // same rollback sslcore::start does
    }
}

void Manager::stop()
{
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    link_.store(LinkState::Off);
}

// ---- configuration --------------------------------------------------------

Config Manager::config() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return cfg_;
}

void Manager::setConfig(const Config& c)
{
    {
        std::lock_guard<std::mutex> lk(mx_);
        cfg_ = c;
        cfg_.host = sanitize(cfg_.host);
        if (cfg_.port <= 0 || cfg_.port > 65535) cfg_.port = 4455;
    }
    cfgDirty_.store(true);
}

std::string Manager::password() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return password_;
}

void Manager::setPassword(const std::string& p)
{
    {
        std::lock_guard<std::mutex> lk(mx_);
        password_ = sanitize(p);
    }
    cfgDirty_.store(true);
}

std::string Manager::serialize() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return sanitize(cfg_.host) + "\t" + std::to_string(cfg_.port) + "\t"
         + (cfg_.enabled ? "1" : "0");
}

bool Manager::deserialize(const std::string& s)
{
    if (s.empty()) return false;
    const std::vector<std::string> f = split(s, '\t');
    if (f.size() < 3) return false;                 // unchanged on a bad string
    Config c;
    c.host    = f[0];
    c.port    = std::atoi(f[1].c_str());
    c.enabled = (f[2] == "1");
    if (c.host.empty()) c.host = "127.0.0.1";
    if (c.port <= 0 || c.port > 65535) c.port = 4455;
    {
        std::lock_guard<std::mutex> lk(mx_);
        cfg_ = c;
    }
    return true;
}

std::string Manager::serializeCredentials() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return sanitize(password_);
}

bool Manager::deserializeCredentials(const std::string& s)
{
    std::lock_guard<std::mutex> lk(mx_);
    password_ = s;
    return true;
}

// ---- live state -----------------------------------------------------------

std::string Manager::status() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return status_;
}

std::string Manager::obsVersion() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return obsVersion_;
}

std::vector<std::string> Manager::scenes() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return scenes_;
}

std::string Manager::currentScene() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return currentScene_;
}

std::string Manager::sceneAt(int idx) const
{
    std::lock_guard<std::mutex> lk(mx_);
    if (idx < 0 || idx >= static_cast<int>(scenes_.size())) return std::string();
    return scenes_[static_cast<size_t>(idx)];
}

void Manager::setStatus(LinkState st, const std::string& text)
{
    link_.store(st);
    std::lock_guard<std::mutex> lk(mx_);
    status_ = text;
}

// ---- commands -------------------------------------------------------------

void Manager::push(const std::string& payload)
{
    std::lock_guard<std::mutex> lk(mx_);
    // A surface key pressed while OBS is away must not pile up for an hour and
    // then fire in a burst when it comes back.
    if (queue_.size() >= 32) queue_.erase(queue_.begin());
    queue_.push_back(payload);
}

void Manager::toggleRecord()
{
    push(request("ToggleRecord", "r" + std::to_string(++reqSeq_)));
}

void Manager::toggleRecordPause()
{
    push(request("ToggleRecordPause", "p" + std::to_string(++reqSeq_)));
}

void Manager::chapter(const std::string& name)
{
    push(request("CreateRecordChapter", "c" + std::to_string(++reqSeq_),
                 chapterData(name)));
}

void Manager::switchScene(const std::string& sceneName)
{
    if (sceneName.empty()) return;
    push(request("SetCurrentProgramScene", "s" + std::to_string(++reqSeq_),
                 sceneSwitchData(sceneName)));
}

void Manager::refreshScenes()
{
    push(request("GetSceneList", "l" + std::to_string(++reqSeq_)));
}

// ---- the worker -----------------------------------------------------------

void Manager::workerLoop()
{
    ws::Client sock;
    bool identified = false;
    auto nextTry    = std::chrono::steady_clock::now();
    auto nextStatus = std::chrono::steady_clock::now();

    auto drop = [&](const std::string& why) {
        sock.close();
        identified = false;
        recording_.store(false);
        paused_.store(false);
        setStatus(LinkState::Failed, why);
        nextTry = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kRetryMs);
    };

    while (run_.load()) {
        const Config cfg = config();

        if (!cfg.enabled) {
            if (sock.connected()) { sock.close(); identified = false; }
            if (link_.load() != LinkState::Off) setStatus(LinkState::Off, "off");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (!sock.connected()) {
            if (std::chrono::steady_clock::now() < nextTry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            setStatus(LinkState::Connecting, "connecting to " + cfg.host);
            if (!sock.connect(cfg.host, cfg.port, "/", kConnectMs)) {
                drop(sock.lastError().empty() ? "no connection" : sock.lastError());
                continue;
            }
            setStatus(LinkState::Connecting, "waiting for OBS to say hello");
            continue;                       // the Hello arrives through poll()
        }

        std::string text;
        const int r = sock.poll(text, kPollMs);
        if (r < 0) {
            drop(sock.lastError().empty() ? "connection lost" : sock.lastError());
            continue;
        }
        if (r > 0) {
            Message m;
            if (parse(text, m)) {
                switch (m.kind) {
                    case MsgKind::Hello: {
                        {
                            std::lock_guard<std::mutex> lk(mx_);
                            obsVersion_ = m.obsVersion;
                        }
                        std::string auth;
                        if (m.authRequired) {
                            const std::string pw = password();
                            if (pw.empty()) {
                                drop("OBS wants a password, none is set");
                                break;
                            }
                            auth = authToken(pw, m.salt, m.challenge);
                        }
                        if (!sock.sendText(identify(auth, kEventSubscriptions)))
                            drop("could not answer the hello");
                        break;
                    }
                    case MsgKind::Identified:
                        identified = true;
                        setStatus(LinkState::Ready, "connected to OBS " + obsVersion());
                        push(request("GetSceneList", "l0"));
                        push(request("GetRecordStatus", "g0"));
                        break;

                    case MsgKind::Event:
                        if (m.hasRecord) {
                            recording_.store(m.recordActive);
                            paused_.store(m.recordPaused);
                        }
                        if (m.hasScene) {
                            std::lock_guard<std::mutex> lk(mx_);
                            currentScene_ = m.sceneName;
                        }
                        // A scene added or removed in OBS changes the key row,
                        // and the cheapest way to learn the new list is to ask.
                        if (m.eventType == "SceneListChanged"
                            || m.eventType == "SceneCreated"
                            || m.eventType == "SceneRemoved"
                            || m.eventType == "SceneNameChanged")
                            push(request("GetSceneList", "l1"));
                        break;

                    case MsgKind::Response:
                        if (m.hasRecord) {
                            recording_.store(m.recordActive);
                            paused_.store(m.recordPaused);
                        }
                        if (m.hasScenes) {
                            std::lock_guard<std::mutex> lk(mx_);
                            scenes_       = m.scenes;
                            currentScene_ = m.currentScene;
                        }
                        if (!m.ok && !m.comment.empty())
                            setStatus(LinkState::Ready, m.requestType + ": " + m.comment);
                        break;

                    default:
                        break;
                }
            }
            continue;                        // read the rest before sending
        }

        if (!identified) continue;           // nothing may go out before Identified

        // Drain what the surface asked for.
        std::vector<std::string> out;
        {
            std::lock_guard<std::mutex> lk(mx_);
            out.swap(queue_);
        }
        bool broken = false;
        for (const std::string& p : out) {
            if (!sock.sendText(p)) { broken = true; break; }
        }
        if (broken) { drop("OBS stopped listening"); continue; }

        // Events carry the record state, so this is only the net under them: if
        // one is ever missed, the LED is right again within five seconds.
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextStatus) {
            nextStatus = now + std::chrono::milliseconds(kStatusPollMs);
            sock.sendText(request("GetRecordStatus", "g1"));
        }
    }

    sock.close();
    link_.store(LinkState::Off);
}

}  // namespace reasixty::obs
