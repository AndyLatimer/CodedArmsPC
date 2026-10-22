// Game-specific hooks called by the recompiled code (tools/recomp/hooks_ULES00124.tsv).
//
// Mouse aim: once per frame Coded Arms updates the player's view with the function at
// 0x0889ba9c(view object, input, ...). It turns the view by integrating the turn/look inputs
// (input+0x78 / +0x79) into the yaw at view+0x51C and the pitch at view+0x524 (radians), clamps
// the pitch to +-1.309 rad and normalises the yaw (TECHNICAL_NOTES.md section 13). The hook at
// its entry adds the mouse motion accumulated by the front end to those angles first.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "psp/host.h"
#include "psp/memory.h"
#include "psp/recomp.h"

namespace psp::host {
Aim g_aim;
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
    if (trace) std::fprintf(stderr, "[HOOK] view=%08x input=%08x yaw=%f pitch=%f\n", view, input, rdf(view + 0x51C), rdf(view + 0x524));
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
