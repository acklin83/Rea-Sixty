#pragma once
//
// EncoderRing: the order and visibility of the channel encoder's modes, the ring the
// "hold and turn" picker walks. One shape, two instances: the UF1's (MODE held +
// encoder) and the UF8's (ENC PUSH held + encoder). Pure logic over mode ints;
// main.cpp owns the live mode and the ExtState keys, tests/test_encoder_ring.cpp a
// table.
//
//   seq[pos]  = mode int at ring position pos (a permutation of 0..N-1)
//   vis[mode] = the picker offers this mode
//
// ⛔ A NEW MODE MUST NOT COST ANYONE THEIR RING. The UF1 ring used to accept only a
// stored list of exactly N entries; appending a mode to the enum made every saved
// order "malformed" and threw it away. Stored lists shorter than N are taken as they
// are, and the modes they do not name keep their factory place and visibility.
//
#include <atomic>
#include <cstdlib>
#include <string>

namespace encring {

template <int N>
struct Ring {
    std::atomic<int>  seq[N];
    std::atomic<bool> vis[N];
    int fallback = 0;               // the mode that stays visible when all are hidden

    // Factory ring: `order` first, every mode it leaves out after it in enum order.
    // `visible(mode)` gives the factory flag.
    template <class Visible>
    void setDefaults(const int* order, int nOrder, Visible visible, int fallbackMode)
    {
        fallback = fallbackMode;
        bool used[N] = {};
        int k = 0;
        for (int i = 0; i < nOrder && k < N; ++i) {
            const int m = order[i];
            if (m < 0 || m >= N || used[m]) continue;
            used[m] = true;
            seq[k++].store(m);
        }
        for (int m = 0; m < N && k < N; ++m)
            if (!used[m]) seq[k++].store(m);
        for (int m = 0; m < N; ++m) vis[m].store(visible(m));
    }

    std::string seqCsv() const
    {
        std::string s;
        for (int k = 0; k < N; ++k) {
            if (k) s += ',';
            s += std::to_string(seq[k].load());
        }
        return s;
    }
    std::string visCsv() const
    {
        std::string s;
        for (int m = 0; m < N; ++m) {
            if (m) s += ',';
            s += vis[m].load() ? '1' : '0';
        }
        return s;
    }

    // A stored order. Every entry must be a known mode, no mode twice; it may name
    // fewer than N (saved before a mode was added). The modes it leaves out keep the
    // order they have now (the factory order when called after setDefaults) and go
    // behind. false = malformed, nothing changed.
    bool loadSeq(const char* csv)
    {
        if (!csv || !*csv) return false;
        int got[N];
        bool seen[N] = {};
        int n = 0;
        for (const char* p = csv; *p; ) {
            char* end = nullptr;
            const long v = std::strtol(p, &end, 10);
            if (end == p || v < 0 || v >= N || seen[v] || n >= N) return false;
            seen[v] = true;
            got[n++] = (int)v;
            p = end;
            while (*p == ',' || *p == ' ') ++p;
        }
        int rest[N];
        int r = 0;
        for (int k = 0; k < N; ++k) {
            const int m = seq[k].load();
            if (m >= 0 && m < N && !seen[m]) { seen[m] = true; rest[r++] = m; }
        }
        int k = 0;
        for (int i = 0; i < n; ++i) seq[k++].store(got[i]);
        for (int i = 0; i < r && k < N; ++i) seq[k++].store(rest[i]);
        return k == N;
    }

    // Stored flags, indexed by mode. Fewer than N: the rest keep their flag. All
    // off: the fallback mode is switched back on. false = malformed.
    bool loadVis(const char* csv)
    {
        if (!csv || !*csv) return false;
        bool got[N];
        int n = 0;
        for (const char* p = csv; *p; ) {
            char* end = nullptr;
            const long v = std::strtol(p, &end, 10);
            if (end == p || n >= N) return false;
            got[n++] = v != 0;
            p = end;
            while (*p == ',' || *p == ' ') ++p;
        }
        for (int m = 0; m < n; ++m) vis[m].store(got[m]);
        keepOneVisible();
        return true;
    }

    void keepOneVisible()
    {
        for (int m = 0; m < N; ++m) if (vis[m].load()) return;
        if (fallback >= 0 && fallback < N) vis[fallback].store(true);
    }

    bool visible(int mode) const { return mode >= 0 && mode < N && vis[mode].load(); }

    // The visible modes in ring order. Returns the count.
    int visibleList(int* out) const
    {
        int n = 0;
        for (int k = 0; k < N; ++k) {
            const int m = seq[k].load();
            if (visible(m)) out[n++] = m;
        }
        return n;
    }

    // `delta` visible positions on from `cur`, wrapping. A hidden or unknown `cur`
    // counts as the first visible one. Nothing visible: the fallback.
    int step(int cur, int delta) const
    {
        int v[N];
        const int n = visibleList(v);
        if (n == 0) return fallback;
        int idx = 0;
        for (int i = 0; i < n; ++i) if (v[i] == cur) { idx = i; break; }
        idx = ((idx + delta) % n + n) % n;
        return v[idx];
    }

    // The first visible mode after `hidden` in ring order (wrapping): where the live
    // mode goes when its own mode was just hidden.
    int nextVisibleAfter(int hidden) const
    {
        int pos = 0;
        for (int k = 0; k < N; ++k) if (seq[k].load() == hidden) { pos = k; break; }
        for (int s = 1; s <= N; ++s) {
            const int m = seq[(pos + s) % N].load();
            if (visible(m)) return m;
        }
        return fallback;
    }

    // The editor's checkbox. Returns the mode the live one has to move to, or `live`
    // when it may stay.
    int setVisible(int mode, bool on, int live)
    {
        if (mode < 0 || mode >= N) return live;
        vis[mode].store(on);
        keepOneVisible();
        return visible(live) ? live : nextVisibleAfter(live);
    }

    // The editor's ▲ / ▼: swap position `pos` with `pos + dir`.
    bool move(int pos, int dir)
    {
        const int other = pos + dir;
        if (pos < 0 || pos >= N || other < 0 || other >= N) return false;
        const int a = seq[pos].load();
        seq[pos].store(seq[other].load());
        seq[other].store(a);
        return true;
    }
};

} // namespace encring
