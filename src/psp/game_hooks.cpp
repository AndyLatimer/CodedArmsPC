// Game-specific hooks called by the recompiled code (tools/recomp/hooks_ULES00124.tsv).
//
// Mouse aim: once per frame Coded Arms updates the player's view with the function at
// 0x0889ba9c(view object, input, ...). It turns the view by integrating the turn/look inputs
// (input+0x78 / +0x79) into the yaw at view+0x51C and the pitch at view+0x524 (radians), clamps
// the pitch to +-1.309 rad and normalises the yaw (TECHNICAL_NOTES.md section 13). The hook at
// its entry adds the mouse motion accumulated by the front end to those angles first.
//
// Frame rate: the game renders a frame every N vblanks. 0x08879ff8(settings, N) is its frame
// interval setter: it stores N at settings+16, 60/N at +12 and (float)(60/N) at +8 (the block
// pointed to by 0x09bd2004). Missions use N = 3 (20 fps), menus N = 2. The hook lowers N when
// the setter runs (60 fps: 1, 30 fps: 2).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "psp/host.h"
#include "psp/memory.h"
#include "psp/recomp.h"

namespace psp::host {
Aim g_aim;
std::atomic<bool> g_ads{false};
DevMode g_dev;
std::atomic<int> g_targetFps{0};
}

namespace {
float rdf(uint32_t a) { uint32_t v = psp::read32(a); float f; std::memcpy(&f, &v, 4); return f; }
void wrf(uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); psp::write32(a, v); }
constexpr float kPitchLimit = 1.3090000f; // 0x3FA78D37, the game's own clamp
constexpr float kPi = 3.14159265f;
}

uint32_t frameSettingsForTrace();

extern "C" void ca_hook_player_view(Ctx* c) {
    const uint32_t view = c->r[19], input = c->r[18];
    static const bool trace = std::getenv("CA_HOOK_TRACE") != nullptr;
    static const bool perf = std::getenv("CA_PERF") != nullptr;
    if (perf) {
        static uint32_t calls = 0;
        static auto last = std::chrono::steady_clock::now();
        ++calls;
        auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::seconds(1)) {
            std::fprintf(stderr, "[PERF] game view updates/s=%u\n", calls);
            calls = 0;
            last = now;
        }
    }
    if (trace)
        std::fprintf(stderr, "[HOOK] view=%08x input=%08x N=%d yaw=%f pitch=%f pos=%f,%f t=%lld\n", view, input,
                     frameSettingsForTrace() ? (int)(int16_t)psp::read16(frameSettingsForTrace() + 16) : -1, rdf(view + 0x51C),
                     rdf(view + 0x524), rdf(view), rdf(view + 8),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    std::lock_guard<std::mutex> lk(psp::host::g_aim.m);
    if (!psp::host::g_aim.enabled || (psp::host::g_aim.yaw == 0 && psp::host::g_aim.pitch == 0)) return;
    float yaw = rdf(view + 0x51C) - psp::host::g_aim.yaw; // mouse right turns right: the yaw decreases
    float pitch = rdf(view + 0x524) + psp::host::g_aim.pitch; // mouse up looks up: the pitch decreases
    yaw = std::remainder(yaw, 2 * kPi);
    pitch = std::fmax(-kPitchLimit, std::fmin(kPitchLimit, pitch));
    wrf(view + 0x51C, yaw);
    wrf(view + 0x524, pitch);
    psp::host::g_aim.yaw = psp::host::g_aim.pitch = 0;
}

// Frame rate --------------------------------------------------------------------------------
namespace {
struct FrameRate {
    uint32_t settings = 0; // the block the setter wrote
    int requested = 0;     // the game's own interval for the current screen
    int effective = 0;     // the interval in use
} g_fr;
} // namespace
uint32_t frameSettingsForTrace() { return g_fr.settings; }

extern "C" void ca_hook_frame_interval(Ctx* c) {
    // At 0x08879ff8 (sll v1, a1, 16), the entry of SetFrameInterval(settings, interval). The
    // interval may only change here: the game derives per-frame steps (e.g. the player's
    // movement) from it when a screen starts, so changing it afterwards changes the game's
    // speed (TECHNICAL_NOTES.md section 15).
    g_fr.settings = c->r[4];
    g_fr.requested = (int16_t)c->r[5];
    const int fps = psp::host::g_targetFps.load();
    const int n = fps >= 60 ? 1 : (fps >= 30 ? 2 : 0);
    g_fr.effective = g_fr.requested;
    if (n && g_fr.requested > n) {
        c->r[5] = (uint32_t)n;
        c->r[3] = (uint32_t)n << 16;
        g_fr.effective = n;
    }
}

// The main loop's frame wait (0x08837f70): entry = the frame's work is done, exit = the next
// frame starts with a0 = the vblanks the last frame took (the counter at s0+4).
extern "C" void ca_hook_frame_work_done(Ctx*) {}

extern "C" void ca_hook_frame_start(Ctx* c) {
    static const bool perf = std::getenv("CA_PERF") != nullptr;
    if (!perf) return;
    static auto last = std::chrono::steady_clock::now();
    static int frames = 0, late = 0;
    ++frames;
    late += (int)c->r[4] > g_fr.effective && g_fr.effective > 0;
    auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(1)) {
        std::fprintf(stderr, "[PERF] game frames %d (late %d), interval: game %d, in use %d\n", frames, late, g_fr.requested,
                     g_fr.effective);
        frames = late = 0;
        last = now;
    }
}

// Dev mode -----------------------------------------------------------------------------------
// When a mission starts, the game computes the 5 weapon slots from the player's inventory into
// five s16 item ids on the stack (f_08926d88) and passes them to SetLoadout (0x088612b4). The
// hooks after the first of those loads replace them with the dev loadout. Item ids: weapons
// 0-22, grenades 23-32 (in the order of the game's item name table, TECHNICAL_NOTES.md).
namespace {
void devLoadout(Ctx* c, uint32_t buf, int firstReg) {
    auto& d = psp::host::g_dev;
    if (!d.enabled) return;
    for (int i = 0; i < 5; ++i) psp::write16(buf + 2 * i, (uint16_t)(int16_t)d.loadout[i]);
    c->r[firstReg] = (uint32_t)(int32_t)d.loadout[firstReg == 6 ? 1 : 0];
}
} // namespace

extern "C" void ca_hook_dev_loadout_mission(Ctx* c) { devLoadout(c, c->r[29] + 36, 6); } // after 0x0890247c
extern "C" void ca_hook_dev_loadout_respawn(Ctx* c) { devLoadout(c, c->r[29] + 20, 5); } // after 0x08900e18
