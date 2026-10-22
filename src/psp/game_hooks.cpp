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
// pointed to by 0x09bd2004). The main loop (0x08837f70) waits until N vblanks have passed and
// the game logic scales its per-frame steps by those fields, so the game speed is the same at
// any N. Missions use N = 3 (20 fps), menus N = 2, some screens N = 1. The hook caps N to the
// interval the player chose (1 = 60 fps).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "psp/host.h"
#include "psp/memory.h"
#include "psp/recomp.h"

namespace psp::host {
Aim g_aim;
std::atomic<int> g_frameInterval{0};
}

namespace {
float rdf(uint32_t a) { uint32_t v = psp::read32(a); float f; std::memcpy(&f, &v, 4); return f; }
void wrf(uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); psp::write32(a, v); }
constexpr float kPitchLimit = 1.3090000f; // 0x3FA78D37, the game's own clamp
constexpr float kPi = 3.14159265f;
}

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
    if (trace) std::fprintf(stderr, "[HOOK] view=%08x input=%08x yaw=%f pitch=%f\n", view, input, rdf(view + 0x51C), rdf(view + 0x524));
    auto& aim = psp::host::g_aim;
    std::lock_guard<std::mutex> lk(aim.m);
    aim.lastUpdateUs = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
    const float oldPitch = rdf(view + 0x524);
    aim.gamePitch = oldPitch;
    if (!aim.enabled || (aim.yaw == 0 && aim.pitch == 0)) return;
    float yaw = rdf(view + 0x51C) - aim.yaw; // mouse right turns right: the yaw decreases
    float pitch = oldPitch + aim.pitch;      // mouse up looks up: the pitch decreases
    yaw = std::remainder(yaw, 2 * kPi);
    pitch = std::fmax(-kPitchLimit, std::fmin(kPitchLimit, pitch));
    wrf(view + 0x51C, yaw);
    wrf(view + 0x524, pitch);
    aim.appliedYaw += aim.yaw;
    aim.appliedPitch += pitch - oldPitch; // what the clamp let through
    aim.gamePitch = pitch;
    aim.yaw = aim.pitch = 0;
}

// Frame rate --------------------------------------------------------------------------------
namespace {
constexpr double kVblankMs = 1001.0 / 60.0;
struct FrameRate {
    uint32_t settings = 0; // the block the setter wrote
    int requested = 0;     // the game's own interval for the current screen
    int effective = 0;     // the interval in use
    std::chrono::steady_clock::time_point frameStart{};
    int window = 0, over = 0, fitting = 0;
    int patience = 180; // frames with headroom before lowering the interval
} g_fr;

void applyInterval(int n) {
    g_fr.effective = n;
    if (!g_fr.settings) return;
    psp::write16(g_fr.settings + 16, (uint16_t)n);
    psp::write32(g_fr.settings + 12, (uint32_t)(60 / n));
    wrf(g_fr.settings + 8, (float)(60 / n));
}
} // namespace

extern "C" void ca_hook_frame_interval(Ctx* c) {
    // At 0x08879ff8 (sll v1, a1, 16), the entry of SetFrameInterval(settings, interval).
    g_fr.settings = c->r[4];
    g_fr.requested = (int16_t)c->r[5];
    g_fr.window = g_fr.over = g_fr.fitting = 0;
    g_fr.patience = 180;
    const int cap = psp::host::g_frameInterval.load();
    if (cap > 0 && g_fr.requested > cap) {
        c->r[5] = (uint32_t)cap;
        c->r[3] = (uint32_t)cap << 16;
        g_fr.effective = cap;
    } else {
        g_fr.effective = g_fr.requested;
    }
}

// The main loop's frame wait (0x08837f70): entry = the frame's work is done, exit = the next
// frame starts, with a0 = the vblanks the frame took (the counter at s0+4). When frames do
// not fit in the capped interval (a slow PC), the game would run in slow motion like a PSP
// dropping frames, because its logic advances a fixed step per frame; so the interval is
// raised (up to the game's own) and lowered again after a long stretch with headroom.
extern "C" void ca_hook_frame_work_done(Ctx*) {
    static const bool perf = std::getenv("CA_PERF") != nullptr;
    if (perf) {
        static auto last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::seconds(1)) {
            std::fprintf(stderr, "[PERF] frame interval: game %d, in use %d\n", g_fr.requested, g_fr.effective);
            last = now;
        }
    }
    const int cap = psp::host::g_frameInterval.load();
    if (cap <= 0 || !g_fr.settings || g_fr.requested <= cap || g_fr.frameStart.time_since_epoch().count() == 0) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_fr.frameStart).count();
    const int n = g_fr.effective;
    g_fr.fitting = (n > cap && ms < (n - 1) * kVblankMs * 0.7) ? g_fr.fitting + 1 : 0;
    if (g_fr.fitting >= g_fr.patience) {
        applyInterval(n - 1);
        g_fr.fitting = g_fr.window = g_fr.over = 0;
    }
}

extern "C" void ca_hook_frame_start(Ctx* c) {
    g_fr.frameStart = std::chrono::steady_clock::now();
    // Debugging: CA_PROBE=addr,addr,... logs those words (as int and float) at every frame.
    static const char* probe = std::getenv("CA_PROBE");
    if (probe) {
        static const auto t0 = std::chrono::steady_clock::now();
        std::string line = "[PROBE] t=" + std::to_string(std::chrono::duration<double, std::milli>(g_fr.frameStart - t0).count()) +
                           " vbl=" + std::to_string(c->r[4]) + " pad=" + std::to_string(psp::host::g_ctrl.buttons.load()) +
                           " stick=" + std::to_string(psp::host::g_ctrl.ly.load());
        for (const char* p = probe; *p;) {
            char* end = nullptr;
            const uint32_t a = (uint32_t)std::strtoul(p, &end, 16);
            char b[64];
            std::snprintf(b, sizeof b, " %08x=%d/%g", a, (int32_t)psp::read32(a), rdf(a));
            line += b;
            p = *end == ',' ? end + 1 : end;
            if (end == p && *p == 0) break;
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    const int cap = psp::host::g_frameInterval.load();
    if (cap <= 0 || !g_fr.settings || g_fr.requested <= cap) return;
    const int n = g_fr.effective;
    g_fr.over += (int)c->r[4] > n;
    if (++g_fr.window == 30) {
        if (g_fr.over >= 6 && n < g_fr.requested) {
            applyInterval(n + 1);
            g_fr.patience = std::min(g_fr.patience * 2, 3600); // do not oscillate
        }
        g_fr.window = g_fr.over = 0;
    }
}
