// Runtime helpers called by recompiled code (see recomp.h).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "psp/kernel.h"
#include "psp/memory.h"
#include "psp/recomp.h"

extern "C" {

// Allegrex division. Results for division by zero and INT_MIN / -1 follow PPSSPP's
// hardware-verified behaviour (Core/MIPS/Interpreter.cpp).
void rt_div(Ctx* c, uint32_t a, uint32_t b) {
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (y == 0) {
        c->lo = x < 0 ? 1 : 0xFFFFFFFFu;
        c->hi = a;
    } else if (x == INT32_MIN && y == -1) {
        c->lo = 0x80000000u;
        c->hi = 0; // hardware leaves the remainder at zero
    } else {
        c->lo = (uint32_t)(x / y);
        c->hi = (uint32_t)(x % y);
    }
}

void rt_divu(Ctx* c, uint32_t a, uint32_t b) {
    if (b == 0) {
        c->lo = a <= 0xFFFF ? 0xFFFFu : 0xFFFFFFFFu;
        c->hi = a;
    } else {
        c->lo = a / b;
        c->hi = a % b;
    }
}

// Unaligned word access, little-endian MIPS semantics.
uint32_t rt_lwl(Ctx* c, uint32_t addr, uint32_t old) {
    uint32_t shift = (addr & 3) * 8;
    uint32_t mem = psp::read32(addr & ~3u);
    return (old & (0x00FFFFFFu >> shift)) | (mem << (24 - shift));
}

uint32_t rt_lwr(Ctx* c, uint32_t addr, uint32_t old) {
    uint32_t shift = (addr & 3) * 8;
    uint32_t mem = psp::read32(addr & ~3u);
    return (old & (0xFFFFFF00u << (24 - shift))) | (mem >> shift);
}

void rt_swl(Ctx* c, uint32_t addr, uint32_t v) {
    uint32_t shift = (addr & 3) * 8;
    uint32_t mem = psp::read32(addr & ~3u);
    psp::write32(addr & ~3u, (mem & (0xFFFFFF00u << shift)) | (v >> (24 - shift)));
}

void rt_swr(Ctx* c, uint32_t addr, uint32_t v) {
    uint32_t shift = (addr & 3) * 8;
    uint32_t mem = psp::read32(addr & ~3u);
    psp::write32(addr & ~3u, (mem & (0x00FFFFFFu >> (24 - shift))) | (v << shift));
}

// Float -> int as the PSP FPU does it: saturate at the int32 limits, NaN -> INT_MAX
// (PPSSPP SaturatedFloatToInt, cpu/fpu/roundmode test).
static int32_t saturate(double d) {
    if (d >= 2147483648.0) return 0x7FFFFFFF;
    if (d <= -2147483648.0) return (int32_t)0x80000000;
    if (d != d) return 0x7FFFFFFF;
    return (int32_t)d;
}

uint32_t rt_f2i(float f, int mode) {
    double d = f;
    switch (mode) {
    case 0: d = std::nearbyint(d); break; // host default mode is round-to-nearest-even
    case 1: d = std::trunc(d); break;
    case 2: d = std::ceil(d); break;
    case 3: d = std::floor(d); break;
    }
    return (uint32_t)saturate(d);
}

uint32_t rt_fcmp(float a, float b, int cond) {
    bool unordered = std::isnan(a) || std::isnan(b);
    if (unordered) return (cond & 1) ? 1 : 0;
    bool eq = a == b, lt = a < b;
    return ((cond & 2) && eq) || ((cond & 4) && lt) ? 1 : 0;
}

uint32_t rt_cfc1(Ctx* c, int reg) {
    if (reg == 31) return (c->fcr31 & ~(1u << 23)) | ((c->fpcond & 1) << 23);
    if (reg == 0) return 0x00003351; // FCR0 implementation/revision (PPSSPP)
    return 0;
}

void rt_ctc1(Ctx* c, int reg, uint32_t v) {
    if (reg == 31) {
        c->fcr31 = v & 0x0181FFFF;
        c->fpcond = (v >> 23) & 1;
    }
}

void call_indirect(Ctx* c, uint32_t addr) {
    if (psp::kernel::callImportByAddress(c, addr)) return;
    unsigned lo = 0, hi = g_guest_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (g_guest_funcs[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_guest_func_count && g_guest_funcs[lo].addr == addr) {
        g_guest_funcs[lo].fn(c);
        return;
    }
    psp::kernel::fatal(c, "indirect call to unknown address %08x (ra=%08x)", addr, c->r[31]);
}

void rt_break(Ctx* c, uint32_t pc) { psp::kernel::fatal(c, "break instruction at %08x", pc); }
void rt_privileged(Ctx* c, uint32_t insn, uint32_t pc) {
    std::fprintf(stderr, "[CPU] privileged instruction %08x at %08x ignored\n", insn, pc);
}
void rt_unimplemented(Ctx* c, uint32_t insn, uint32_t pc) {
    psp::kernel::fatal(c, "unimplemented instruction %08x at %08x", insn, pc);
}
void rt_fell_off(Ctx* c, uint32_t addr) { psp::kernel::fatal(c, "execution fell off the end of code at %08x", addr); }
void hle_syscall(Ctx* c, uint32_t code) { psp::kernel::fatal(c, "raw syscall %x in game code", code); }

} // extern "C"
