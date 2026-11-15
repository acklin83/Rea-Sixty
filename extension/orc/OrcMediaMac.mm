// The Mac side of OrcMedia.h: media keys as the keyboard sends them, the system
// volume through Core Audio.

#include "OrcMedia.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>
#include <IOKit/hidsystem/ev_keymap.h>

#include <algorithm>
#include <atomic>

namespace orc::media {

namespace {

int keyType(Key k)
{
    switch (k) {
        case Key::PlayPause:  return NX_KEYTYPE_PLAY;
        case Key::Next:       return NX_KEYTYPE_NEXT;
        case Key::Previous:   return NX_KEYTYPE_PREVIOUS;
        case Key::VolumeUp:   return NX_KEYTYPE_SOUND_UP;
        case Key::VolumeDown: return NX_KEYTYPE_SOUND_DOWN;
        case Key::Mute:       return NX_KEYTYPE_MUTE;
    }
    return -1;
}

// A system-defined event of subtype 8 is how the keyboard's media keys arrive:
// data1 = key type << 16, then 0xA (down) or 0xB (up) << 8.
void post(int type, bool down)
{
    const NSInteger data1 = (static_cast<NSInteger>(type) << 16) | ((down ? 0xA : 0xB) << 8);
    NSEvent* e = [NSEvent otherEventWithType:NSEventTypeSystemDefined
                                    location:NSZeroPoint
                               modifierFlags:(down ? 0xA00 : 0xB00)
                                   timestamp:0
                                windowNumber:0
                                     context:nil
                                     subtype:8
                                       data1:data1
                                       data2:-1];
    if (e) CGEventPost(kCGHIDEventTap, [e CGEvent]);
}

AudioObjectID defaultOutput()
{
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 size = sizeof dev;
    const AudioObjectPropertyAddress a{ kAudioHardwarePropertyDefaultOutputDevice,
                                        kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &dev) != noErr)
        return kAudioObjectUnknown;
    return dev;
}

} // namespace

void press(Key k)
{
    const int type = keyType(k);
    if (type < 0) return;
    // The bindings fire on the device's thread; AppKit's events are made on the main one.
    dispatch_async(dispatch_get_main_queue(), ^{
        // ⇨ ASK ONCE PER RUN (Frank 15.11.2026): without the right to post events
        // macOS drops them; the request puts ORC into Accessibility and shows the
        // system's question. Asking again on every press would only nag.
        static bool asked = false;
        if (!asked && !CGPreflightPostEventAccess()) {
            asked = true;
            CGRequestPostEventAccess();
        }
        post(type, true);
        post(type, false);
    });
}

// ⇨ THE VOLUME THE MENU BAR'S SLIDER MOVES: the device's virtual main volume,
// through AudioObject* (the AudioHardwareService calls are deprecated since 10.11;
// the property itself is not).
void stepSystemVolume(double delta)
{
    const AudioObjectID dev = defaultOutput();
    if (dev == kAudioObjectUnknown || delta == 0.0) return;
    const AudioObjectPropertyAddress vol{ kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
                                          kAudioDevicePropertyScopeOutput,
                                          kAudioObjectPropertyElementMain };
    if (!AudioObjectHasProperty(dev, &vol)) return;
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(dev, &vol, &settable) != noErr || !settable) return;
    Float32 v = 0;
    UInt32 size = sizeof v;
    if (AudioObjectGetPropertyData(dev, &vol, 0, nullptr, &size, &v) != noErr) return;
    const Float32 nv = static_cast<Float32>(std::clamp(static_cast<double>(v) + delta, 0.0, 1.0));
    if (nv != v) AudioObjectSetPropertyData(dev, &vol, 0, nullptr, sizeof nv, &nv);

    // As the keyboard's volume keys do: turning brings the sound back.
    const AudioObjectPropertyAddress mute{ kAudioDevicePropertyMute,
                                           kAudioDevicePropertyScopeOutput,
                                           kAudioObjectPropertyElementMain };
    if (!AudioObjectHasProperty(dev, &mute)) return;
    UInt32 muted = 0;
    size = sizeof muted;
    if (AudioObjectGetPropertyData(dev, &mute, 0, nullptr, &size, &muted) != noErr || !muted) return;
    Boolean muteSettable = false;
    if (AudioObjectIsPropertySettable(dev, &mute, &muteSettable) != noErr || !muteSettable) return;
    const UInt32 off = 0;
    AudioObjectSetPropertyData(dev, &mute, 0, nullptr, sizeof off, &off);
}

} // namespace orc::media
