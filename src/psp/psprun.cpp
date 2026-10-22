// ca_psprun: headless runner for the recompiled game (development tool).
//   ca_psprun --iso CodedArms.iso [--trace] [--seconds N] [--wav out.wav]
//             [--press BUTTON@SECONDS[:HOLD]]... [--gl [--scale N] [--shot SECONDS:FILE.png]...]
// --gl renders the GE output with OpenGL in a hidden window (works under Xvfb) and --shot
// saves the presented frame at the given times (for comparison with reference frames).
// --wav consumes the guest audio channels in real time like a host mixer would and writes
// the mix to a 44.1 kHz stereo WAV file (for checking sound output without a window).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <map>
#include <mutex>
#include <vector>

#include <SDL.h>

#include "psp/ge.h"
#include "psp/gpu/gpu.h"
#include "render/GL.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "psp/host.h"
#include "psp/memory.h"
#include "psp/kernel.h"

namespace {
FILE* g_wav = nullptr;
uint32_t g_wavFrames = 0;

void writeWavHeader(FILE* f, uint32_t frames) {
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f); u32(36 + frames * 4); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(frames * 4);
    std::fseek(f, 0, SEEK_END);
}

// Every 10 ms takes 441 frames from each channel (silence where a channel ran dry), mixes
// them and appends them to the WAV file.
void wavThread() {
    using namespace psp::host;
    auto next = std::chrono::steady_clock::now();
    std::vector<int16_t> out(441 * 2);
    for (;;) {
        next += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next);
        std::vector<int> mix(441 * 2, 0);
        {
            std::lock_guard<std::mutex> lk(g_audioMutex);
            for (auto& ch : g_audio) {
                size_t n = std::min<size_t>(ch.queue.size(), mix.size());
                for (size_t i = 0; i < n; ++i) mix[i] += ch.queue[i];
                ch.queue.erase(ch.queue.begin(), ch.queue.begin() + (long)n);
            }
        }
        for (size_t i = 0; i < mix.size(); ++i) out[i] = (int16_t)std::clamp(mix[i], -32768, 32767);
        std::fwrite(out.data(), 2, out.size(), g_wav);
        g_wavFrames += 441;
    }
}

// CA_GE_STATS=1: counts GE commands, primitive/vertex types and texture/framebuffer formats
// and prints them at exit (scopes what the renderer must support).
struct GeStats : psp::ge::Renderer {
    std::mutex m;
    std::map<std::string, uint64_t> counts;
    void bump(const char* fmt, uint32_t v) { char b[64]; std::snprintf(b, sizeof b, fmt, v); counts[b]++; }
    void command(uint32_t, uint32_t cmd, uint32_t data, uint32_t, uint32_t) override {
        std::lock_guard<std::mutex> lk(m);
        bump("cmd %02x", cmd);
        switch (cmd) {
        case 0x04: bump("prim type %u", (data >> 16) & 7);
            bump("prim vtype %06x", psp::ge::reg(0x12));
            bump("prim texfmt %u", psp::ge::reg(0x1E) ? psp::ge::reg(0xC3) & 0xF : 99);
            bump("prim clear %u", psp::ge::reg(0xD3) & 1);
            bump("prim fbfmt %u", psp::ge::reg(0xD2) & 3);
            bump("prim fb %06x", psp::ge::reg(0x9C));
            bump("prim tex0 %06x", psp::ge::reg(0x1E) ? psp::ge::reg(0xA0) : 0);
            bump("prim blend %06x", psp::ge::reg(0x21) ? psp::ge::reg(0xDF) : 0xFFFFFF);
            bump("prim lighting %u", psp::ge::reg(0x17) & 1);
            bump("prim fog %u", psp::ge::reg(0x1F) & 1);
            bump("prim stencil %u", psp::ge::reg(0x24) & 1);
            bump("prim texfunc %06x", psp::ge::reg(0xC9));
            bump("prim texmap %06x", psp::ge::reg(0xC0));
            break;
        case 0x05: case 0x06: bump("patch %u", cmd); break;
        case 0xC4: bump("clutfmt %06x", psp::ge::reg(0xC5)); break;
        case 0xEA: bump("transfer bpp %u", data & 1);
            bump("transfer src %06x", psp::ge::reg(0xB2)); bump("transfer dst %06x", psp::ge::reg(0xB4)); break;
        }
    }
    ~GeStats() override {}
};
GeStats* g_geStats = nullptr;

struct Press { uint32_t buttons; double at, hold; };

uint32_t buttonByName(const std::string& n) {
    using namespace psp::host;
    static const std::pair<const char*, uint32_t> names[] = {
        {"SELECT", BTN_SELECT}, {"START", BTN_START}, {"UP", BTN_UP}, {"RIGHT", BTN_RIGHT},
        {"DOWN", BTN_DOWN}, {"LEFT", BTN_LEFT}, {"L", BTN_LTRIGGER}, {"R", BTN_RTRIGGER},
        {"TRIANGLE", BTN_TRIANGLE}, {"CIRCLE", BTN_CIRCLE}, {"CROSS", BTN_CROSS}, {"SQUARE", BTN_SQUARE}};
    for (auto& [name, bit] : names) if (n == name) return bit;
    return 0;
}

// Plays scripted button presses (debugging aid: lets headless runs get past menus).
void pressThread(std::vector<Press> presses) {
    auto t0 = std::chrono::steady_clock::now();
    std::sort(presses.begin(), presses.end(), [](const Press& a, const Press& b) { return a.at < b.at; });
    for (const Press& p : presses) {
        std::this_thread::sleep_until(t0 + std::chrono::milliseconds((long long)(p.at * 1000)));
        psp::host::g_ctrl.buttons |= p.buttons;
        std::fprintf(stderr, "[RUN] press %04x at %.2fs\n", p.buttons, p.at);
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)(p.hold * 1000)));
        psp::host::g_ctrl.buttons &= ~p.buttons;
    }
}
} // namespace

// Runs the game on a worker thread and the renderer on this one, in a hidden GL window.
int runWithGl(const psp::kernel::BootOptions& opt, int scale, std::vector<std::pair<double, std::string>> shots) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("ca_psprun", 0, 0, 480, 272, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "GL context: %s\n", SDL_GetError()); return 1; }
    const char* missing = nullptr;
    if (!ca::loadGL(&missing)) { std::fprintf(stderr, "missing GL function %s\n", missing); return 1; }
    std::fprintf(stderr, "[RUN] GL: %s / %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    psp::gpu::Settings gs;
    gs.scale = scale;
    psp::gpu::install(gs);
    if (!psp::gpu::renderInit()) return 1;
    std::thread([opt] { std::_Exit(psp::kernel::boot(opt)); }).detach();
    std::sort(shots.begin(), shots.end());
    auto t0 = std::chrono::steady_clock::now();
    psp::gpu::PresentInfo last;
    bool have = false;
    uint64_t frames = 0;
    for (;;) {
        psp::gpu::PresentInfo p;
        if (psp::gpu::renderPump(p, 5)) { last = p; have = true; ++frames; }
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        while (!shots.empty() && t >= shots.front().first) {
            std::vector<uint32_t> px;
            int w = 0, h = 0;
            if (have && psp::gpu::readPresented(last, px, w, h)) {
                stbi_write_png(shots.front().second.c_str(), w, h, 4, px.data(), w * 4);
                std::fprintf(stderr, "[RUN] shot %s at %.2fs (frame %llu, %llu presented)\n", shots.front().second.c_str(), t,
                             (unsigned long long)last.frame, (unsigned long long)frames);
            } else {
                std::fprintf(stderr, "[RUN] shot %s at %.2fs: nothing presented yet\n", shots.front().second.c_str(), t);
            }
            shots.erase(shots.begin());
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
    }
}

int main(int argc, char** argv) {
    psp::kernel::BootOptions opt;
    int seconds = 0;
    std::vector<Press> presses;
    bool gl = false;
    int scale = 1;
    std::vector<std::pair<double, std::string>> shots;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iso") && i + 1 < argc) opt.isoPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace")) opt.traceHle = true;
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gl")) gl = true;
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            std::string spec = argv[++i];
            size_t c = spec.find(':');
            if (c == std::string::npos) { std::fprintf(stderr, "bad --shot %s\n", spec.c_str()); return 2; }
            shots.push_back({std::atof(spec.substr(0, c).c_str()), spec.substr(c + 1)});
        }
        else if (!std::strcmp(argv[i], "--press") && i + 1 < argc) {
            std::string spec = argv[++i];
            size_t at = spec.find('@'), colon = spec.find(':');
            Press p{buttonByName(spec.substr(0, at)), 0, 0.15};
            if (at == std::string::npos || !p.buttons) { std::fprintf(stderr, "bad --press %s\n", spec.c_str()); return 2; }
            p.at = std::atof(spec.c_str() + at + 1);
            if (colon != std::string::npos) p.hold = std::atof(spec.c_str() + colon + 1);
            presses.push_back(p);
        }
        else if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) {
            g_wav = std::fopen(argv[++i], "wb");
            if (g_wav) { writeWavHeader(g_wav, 0); std::thread(wavThread).detach(); }
        }
    }
    if (opt.isoPath.empty()) {
        std::fprintf(stderr, "usage: ca_psprun --iso CodedArms.iso [--trace] [--seconds N] [--wav out.wav]\n");
        return 2;
    }
    if (seconds > 0) {
        std::thread([seconds] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            std::fprintf(stderr, "[RUN] time limit reached\n");
            if (g_geStats) {
                std::lock_guard<std::mutex> lk(g_geStats->m);
                for (auto& [k, v] : g_geStats->counts) std::fprintf(stderr, "[GE] %-24s %llu\n", k.c_str(), (unsigned long long)v);
            }
            if (g_wav) { writeWavHeader(g_wav, g_wavFrames); std::fflush(g_wav); }
            std::fflush(stderr);
            std::_Exit(0);
        }).detach();
    }
    if (!presses.empty()) std::thread(pressThread, presses).detach();
    if (gl) return runWithGl(opt, scale, shots);
    if (std::getenv("CA_GE_STATS")) { g_geStats = new GeStats; psp::ge::setRenderer(g_geStats); }
    return psp::kernel::boot(opt);
}
