#pragma once
// State shared between the HLE layer (guest threads) and the native front end
// (window, input, renderer, audio). Plain atomics/mutex-protected data only.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace psp::host {

// PSP controller buttons (SceCtrlData.Buttons).
enum : uint32_t {
    BTN_SELECT = 0x0001, BTN_START = 0x0008, BTN_UP = 0x0010, BTN_RIGHT = 0x0020, BTN_DOWN = 0x0040,
    BTN_LEFT = 0x0080, BTN_LTRIGGER = 0x0100, BTN_RTRIGGER = 0x0200, BTN_TRIANGLE = 0x1000,
    BTN_CIRCLE = 0x2000, BTN_CROSS = 0x4000, BTN_SQUARE = 0x8000,
};

struct Controller {
    std::atomic<uint32_t> buttons{0};
    std::atomic<uint8_t> lx{128}, ly{128};
};
extern Controller g_ctrl;

struct Display {
    std::atomic<uint32_t> topAddr{0}, bufferWidth{512}, pixelFormat{3};
    std::atomic<uint32_t> frameCount{0}; // incremented by sceDisplaySetFrameBuf
};
extern Display g_display;

// Audio: guest channels push 16-bit stereo samples; the host mixer consumes them.
struct AudioChannel {
    bool reserved = false;
    uint32_t sampleCount = 0, format = 0; // format 0x00 stereo, 0x10 mono
    int volLeft = 0x8000, volRight = 0x8000;
    std::vector<int16_t> queue;           // interleaved stereo, after volume
};
constexpr int kAudioChannels = 8;
extern AudioChannel g_audio[kAudioChannels];
extern std::mutex g_audioMutex;
size_t audioQueuedFrames(int ch);

} // namespace psp::host
