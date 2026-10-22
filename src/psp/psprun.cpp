// ca_psprun: headless runner for the recompiled game (development tool).
//   ca_psprun --iso CodedArms.iso [--trace] [--seconds N] [--wav out.wav]
//             [--press BUTTON@SECONDS[:HOLD]]... [--memstick DIR]
// --memstick (or CA_MEMSTICK) is the host folder mapped to ms0:/ (default ./memstick).
// System dialogs (savedata, messages) have no screen here: they are logged, and answered by the
// policy in CA_DIALOG_POLICY after CA_DIALOG_DELAY_MS (default 500) of being shown:
//   default  - the dialog's default: the highlighted Yes/No choice, the highlighted savedata
//              entry, OK; a screen with only a Back button is closed with it
//   yes / no - choose Yes / No in yes/no questions, otherwise as default
//   cancel   - press the cancel button
//   none     - never answer
//   buttons  - no policy: the dialogs read the emulated controller like PPSSPP (use --press)
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
#include <vector>

#include "psp/host.h"
#include "psp/host_dialog.h"
#include "psp/kernel.h"

namespace psp::kernel {
extern std::string g_hostMsRoot;
}

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

struct Press { uint32_t buttons; double at, hold; };

void logDialog(const psp::host::DialogView& v) {
    using namespace psp::host;
    std::string text = v.text;
    for (char& ch : text) if (ch == '\n') ch = '|';
    std::string extra;
    if (v.yesNo) extra += v.yesNoChoice ? " [YES] no" : " yes [NO]";
    if (v.ok) extra += " [OK]";
    if (v.busy) extra += " (busy)";
    if (v.confirmButton) extra += " confirm=" + v.confirmLabel;
    if (v.cancelButton) extra += " cancel=" + v.cancelLabel;
    std::fprintf(stderr, "[DIALOG] %s%s%s: \"%s\"%s\n", v.kind == DialogKind::Message ? "message" : "savedata",
                 v.banner.empty() ? "" : " ", v.banner.c_str(), text.c_str(), extra.c_str());
    for (size_t i = 0; i < v.entries.size(); ++i) {
        const DialogSaveEntry& e = v.entries[i];
        std::fprintf(stderr, "[DIALOG]   %c %s: %s / %s (%lld bytes%s, icon %zu bytes)\n", (int)i == v.selected ? '>' : ' ',
                     e.saveName.c_str(), e.title.c_str(), e.savedataTitle.c_str(), (long long)e.sizeBytes, e.broken ? ", broken" : "",
                     e.iconPng.size());
    }
    if (v.showsEntry)
        std::fprintf(stderr, "[DIALOG]   slot %s: %s / %s (%lld bytes, icon %zu bytes)\n", v.entry.saveName.c_str(), v.entry.title.c_str(),
                     v.entry.savedataTitle.c_str(), (long long)v.entry.sizeBytes, v.entry.iconPng.size());
}

void installDialogPolicy() {
    using namespace psp::host;
    const char* p = std::getenv("CA_DIALOG_POLICY");
    std::string policy = p ? p : "default";
    const char* d = std::getenv("CA_DIALOG_DELAY_MS");
    uint64_t delayUs = (uint64_t)(d ? std::atoi(d) : 500) * 1000;
    setDialogObserver(logDialog);
    if (policy == "buttons") { setDialogUsesController(true); return; }
    if (policy == "none") return;
    setDialogAutoResponder([policy, delayUs](const DialogView& v, uint64_t visibleUs, DialogInput& in) {
        if (visibleUs < delayUs || v.fade < 255) return false;
        if (policy == "cancel") {
            if (!v.cancelButton) return false;
            in.cancel = true;
        } else if (v.yesNo) {
            in.yesNoChoice = policy == "yes" ? 1 : (policy == "no" ? 0 : v.yesNoChoice);
            in.confirm = true;
        } else if (v.confirmButton) {
            in.select = v.selected;
            in.confirm = true;
        } else if (v.cancelButton) {
            in.cancel = true; // result screens ("Save completed") close with the cancel button
        } else {
            return false;
        }
        std::fprintf(stderr, "[DIALOG] auto answer (%s): %s%s\n", policy.c_str(), in.confirm ? "confirm" : "cancel",
                     in.yesNoChoice < 0 ? "" : (in.yesNoChoice ? " yes" : " no"));
        return true;
    });
}

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

int main(int argc, char** argv) {
    psp::kernel::BootOptions opt;
    if (const char* ms = std::getenv("CA_MEMSTICK")) psp::kernel::g_hostMsRoot = ms;
    int seconds = 0;
    std::vector<Press> presses;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iso") && i + 1 < argc) opt.isoPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace")) opt.traceHle = true;
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--press") && i + 1 < argc) {
            std::string spec = argv[++i];
            size_t at = spec.find('@'), colon = spec.find(':');
            Press p{buttonByName(spec.substr(0, at)), 0, 0.15};
            if (at == std::string::npos || !p.buttons) { std::fprintf(stderr, "bad --press %s\n", spec.c_str()); return 2; }
            p.at = std::atof(spec.c_str() + at + 1);
            if (colon != std::string::npos) p.hold = std::atof(spec.c_str() + colon + 1);
            presses.push_back(p);
        }
        else if (!std::strcmp(argv[i], "--memstick") && i + 1 < argc) psp::kernel::g_hostMsRoot = argv[++i];
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
            if (g_wav) { writeWavHeader(g_wav, g_wavFrames); std::fflush(g_wav); }
            std::fflush(stderr);
            std::_Exit(0);
        }).detach();
    }
    if (!psp::kernel::g_hostMsRoot.empty() && psp::kernel::g_hostMsRoot.back() != '/') psp::kernel::g_hostMsRoot += '/';
    installDialogPolicy();
    if (!presses.empty()) std::thread(pressThread, presses).detach();
    return psp::kernel::boot(opt);
}
