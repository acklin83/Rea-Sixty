// The Windows side of OrcMedia.h: media keys through SendInput, the system volume
// through the default render endpoint.

#include "OrcMedia.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>

namespace orc::media {

namespace {

WORD virtualKey(Key k)
{
    switch (k) {
        case Key::PlayPause:  return VK_MEDIA_PLAY_PAUSE;
        case Key::Next:       return VK_MEDIA_NEXT_TRACK;
        case Key::Previous:   return VK_MEDIA_PREV_TRACK;
        case Key::VolumeUp:   return VK_VOLUME_UP;
        case Key::VolumeDown: return VK_VOLUME_DOWN;
        case Key::Mute:       return VK_VOLUME_MUTE;
    }
    return 0;
}

// COM once per calling thread (the device's worker); a thread that already has
// another apartment keeps it, the endpoint works from either.
void comOnThisThread()
{
    thread_local bool done = false;
    if (done) return;
    done = true;
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

} // namespace

void press(Key k)
{
    const WORD vk = virtualKey(k);
    if (!vk) return;
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ::SendInput(2, in, sizeof(INPUT));
}

void stepSystemVolume(double delta)
{
    if (delta == 0.0) return;
    comOnThisThread();
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))))
        return;
    IMMDevice* dev = nullptr;
    IAudioEndpointVolume* vol = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))
        && SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&vol)))) {
        float v = 0.0f;
        if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&v))) {
            const float nv = static_cast<float>(std::clamp(static_cast<double>(v) + delta, 0.0, 1.0));
            if (nv != v) vol->SetMasterVolumeLevelScalar(nv, nullptr);
        }
        // As the keyboard's volume keys do: turning brings the sound back.
        BOOL muted = FALSE;
        if (SUCCEEDED(vol->GetMute(&muted)) && muted) vol->SetMute(FALSE, nullptr);
        vol->Release();
    }
    if (dev) dev->Release();
    en->Release();
}

} // namespace orc::media
