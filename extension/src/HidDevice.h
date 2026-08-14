#pragma once
//
// HidDevice — hidapi reader for the UF8 HID interface (VID 0x31E9 / PID 0x0022).
//
// ⚠ CURRENTLY UNUSED. Nothing constructs one: main.cpp declares g_hid but never
// assigns it, and initDevices_ does not open it. Do not debug UF8 input here.
//
// The early assumption was that PID 0x0021 carried display/colour OUT only and
// that faders, V-Pots and buttons had to come back over this separate HID
// interface. That turned out to be wrong: 0x0021 is bidirectional, and ALL UF8
// input arrives on its bulk IN endpoint 0x81. The live path is
// UF8Device::readCallback_ -> rawInputHandler_ -> onUf8Input (main.cpp), which
// does its own FF-frame walk; uf8::parseButtonEvent covers the button frames.
//
// Kept because the interface is real hardware we may still want to read (the
// report format was never characterised), but until something opens it this is
// dead weight — see COMPENDIUM.md §6.
//

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

struct hid_device_;
typedef struct hid_device_ hid_device;

namespace uf8 {

class HidDevice {
public:
    using ReportHandler = std::function<void(const uint8_t* data, size_t len)>;

    HidDevice() = default;
    ~HidDevice();
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    bool open(uint16_t vid, uint16_t pid);
    void close();
    bool isOpen() const { return handle_ != nullptr; }

    void setHandler(ReportHandler h) { handler_ = std::move(h); }

    const std::string& lastError() const { return lastError_; }

private:
    void readerLoop_();

    hid_device*       handle_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> stop_{false};
    ReportHandler     handler_;
    std::string       lastError_;
};

} // namespace uf8
