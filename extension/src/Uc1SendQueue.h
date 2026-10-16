#pragma once
//
// Uc1SendQueue — the UC1's outgoing frame queue, without USB, so
// tests/test_uc1_send_queue.cpp can pin it. Not thread-safe on its own: the
// caller holds its lock around push() and takeAll().
//
// ⇨ ONE LED CELL, ONE WAITING FRAME (16.10.2026). An LED write
// FF 13 04 <bank> <cell> <b5> <state> sets that cell; a newer write for the
// same cell makes an unsent older one pointless. On 16.10. the UC1 got up to
// 9721 of them in one second (15 full repaints a second, ~50 frames expected)
// and locked up during an LED flood that morning. A newer write empties the
// waiting one, which takeAll() then leaves out, and joins the back, so the
// newest writes keep their own order: SSL 360 sends some cell pairs in a set
// order, and nothing says the firmware does not care.
// std::deque: pushing at the back and popping at the front never moves the
// other elements, so the pointers in waiting_ stay good until takeAll(), which
// clears them.
//
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

class Uc1SendQueue {
public:
    void push(std::vector<uint8_t> frame)
    {
        const bool led = frame.size() == 8 && frame[0] == 0xFF
                      && frame[1] == 0x13 && frame[2] == 0x04;
        if (!led) {
            q_.push_back(std::move(frame));
            return;
        }
        const uint32_t key = (uint32_t(frame[3]) << 16)
                           | (uint32_t(frame[4]) << 8) | frame[5];
        if (auto it = waiting_.find(key); it != waiting_.end()) {
            it->second->clear();
            ++superseded_;
        }
        q_.push_back(std::move(frame));
        waiting_[key] = &q_.back();
    }

    bool empty() const { return q_.empty(); }

    // Everything still worth sending, oldest first.
    std::vector<std::vector<uint8_t>> takeAll()
    {
        std::vector<std::vector<uint8_t>> out;
        out.reserve(q_.size());
        for (auto& f : q_)
            if (!f.empty()) out.push_back(std::move(f));
        q_.clear();
        waiting_.clear();
        return out;
    }

    // LED writes dropped since the last call, for the stats line.
    int takeSuperseded()
    {
        const int n = superseded_;
        superseded_ = 0;
        return n;
    }

private:
    std::deque<std::vector<uint8_t>> q_;
    std::unordered_map<uint32_t, std::vector<uint8_t>*> waiting_;
    int superseded_ = 0;
};
