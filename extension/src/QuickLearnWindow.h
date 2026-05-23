#pragma once
//
// QuickLearnWindow — standalone popup for rapid FX parameter mapping.
//
// Triggered by the "quick_learn" builtin action. Opens a floating ImGui
// window that guides the user through mapping plugin parameters to
// UC1 slots and/or UF8 V-Pot banks via sequential wiggle-detection.
//
// Flow: Setup (domain/fader-banks) → Sequential Mapping → Review & Save
//

namespace uf8 {

class QuickLearnWindow {
public:
    QuickLearnWindow();
    ~QuickLearnWindow();

    QuickLearnWindow(const QuickLearnWindow&)            = delete;
    QuickLearnWindow& operator=(const QuickLearnWindow&) = delete;

    void toggle();
    bool isOpen() const;

    // Called from onTimer() each tick. No-op when closed.
    void onRunTick();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uf8
