#pragma once
//
// The WinUSB driver for the SSL surfaces under Windows, for Rea-Sixty and ORC
// (docs/orc-windows-plan.md, section 6). One installer, one script, one INF:
// libusb can only open a device that has WinUSB bound to it.
//
// ⛔ It takes the devices away from SSL 360, and it binds only what is plugged in.
// That is the user's decision, so it is a button and never part of an installation.

#include <string>
#include <vector>

namespace winusb {

struct Device {
    const char* id;     // what the hardware id holds: "VID_31E9&PID_0025"
    const char* name;   // "UF1"
};

struct DeviceState {
    std::string name;
    bool        connected = false;
    bool        ready     = false;   // WinUSB is bound: libusb can open it
    std::string service;             // the driver service, "SSLBUS" while SSL 360 has it
    std::string maker;
};

// What each device is bound to right now. Needs no elevation. It walks the whole USB
// tree, so on request and not every frame, and ⛔ never from a USB thread.
std::vector<DeviceState> status(const std::vector<Device>& devices);

// One UAC prompt, on a worker. True when the worker started; the outcome comes from
// jobMessage() once jobState() says 2.
bool install(const std::vector<Device>& devices, std::string* errOut);
bool uninstall(std::string* errOut);

int         jobState();     // 0 idle, 1 running, 2 finished
std::string jobMessage();
void        jobClear();     // the UI has taken the result

} // namespace winusb
