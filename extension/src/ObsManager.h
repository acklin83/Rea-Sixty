#pragma once
//
// The OBS link: one worker thread, one socket, and the state the surface reads.
//
// Shape copied from HueManager, deliberately and down to the details, because
// the problem is the same one: a device on the other side of a wire, a surface
// that must not wait for it, and a settings pane that must not know about
// threads. What differs is that OBS talks back on its own — record state and
// scene changes arrive as events — so the live values here are fed by the peer
// rather than polled.
//
// ⛔ THE WORKER NEVER TOUCHES THE REAPER API. It parses JSON and moves values
// into atomics and mutex-guarded lists; the main thread reads them when it
// paints. Same invariant as HueManager and StreamDeckBridge.
//
// Commands go the other way through a queue: the main thread pushes a finished
// request (ObsProto::request built with the values it already has), the worker
// sends it. Discrete events, so a queue and not the coalescing atomics Hue uses
// for fader traffic — pressing record twice must mean twice.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reasixty::obs {

enum class LinkState { Off, Connecting, Ready, Failed };

struct Config {
    bool        enabled = false;
    std::string host    = "127.0.0.1";
    int         port    = 4455;
};

class Manager {
  public:
    Manager() = default;
    ~Manager();
    Manager(const Manager&)            = delete;
    Manager& operator=(const Manager&) = delete;

    void start();
    void stop();

    // ---- configuration (main thread) ------------------------------------
    Config      config() const;
    void        setConfig(const Config& c);
    std::string password() const;
    void        setPassword(const std::string& p);

    // True once, when something changed that belongs in ExtState. The transport
    // tick asks; the settings pane must not save by itself (see HueManager).
    bool takeConfigDirty() { return cfgDirty_.exchange(false); }

    std::string serialize() const;                       // host \t port \t enabled
    bool        deserialize(const std::string& s);       // false = kept as it was
    std::string serializeCredentials() const;
    bool        deserializeCredentials(const std::string& s);

    // ---- live state (any thread) ----------------------------------------
    LinkState   link() const { return link_.load(); }
    std::string status() const;
    bool        recording() const { return recording_.load(); }
    bool        paused() const    { return paused_.load(); }
    std::string obsVersion() const;

    std::vector<std::string> scenes() const;
    std::string              currentScene() const;
    // Name of scene `idx` (0-based) or "" — what a soft key asks for.
    std::string              sceneAt(int idx) const;

    // ---- commands (main thread) ------------------------------------------
    void toggleRecord();
    void toggleRecordPause();
    void chapter(const std::string& name);
    void switchScene(const std::string& sceneName);
    void refreshScenes();

  private:
    void workerLoop();
    void push(const std::string& payload);
    void setStatus(LinkState st, const std::string& text);

    std::thread       worker_;
    std::atomic<bool> run_{false};

    mutable std::mutex mx_;
    Config             cfg_;
    std::string        password_;
    std::string        status_ = "off";
    std::string        obsVersion_;
    std::vector<std::string> scenes_;
    std::string        currentScene_;
    std::vector<std::string> queue_;

    std::atomic<LinkState> link_{LinkState::Off};
    std::atomic<bool>      recording_{false};
    std::atomic<bool>      paused_{false};
    std::atomic<bool>      cfgDirty_{false};
    std::atomic<unsigned>  reqSeq_{0};
};

Manager& manager();

}  // namespace reasixty::obs
