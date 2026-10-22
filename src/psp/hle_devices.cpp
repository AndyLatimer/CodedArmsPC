// Device HLE: display, controller, GE front end, audio, utility dialogs, UMD, power,
// impose, RTC. Network (ad hoc multiplayer) is reported unavailable for now; MPEG video is in
// hle_mpeg.cpp.
#include <chrono>
#include <cmath>
#include <ctime>

#include "psp/ge.h"
#include "psp/gpu/gpu.h"
#include "psp/host.h"
#include "psp/kernel.h"
#include "psp/memory.h"

namespace psp::host {
Controller g_ctrl;
Display g_display;
AudioChannel g_audio[kAudioChannels];
std::mutex g_audioMutex;
size_t audioQueuedFrames(int ch) { return g_audio[ch].queue.size() / 2; }
} // namespace psp::host

namespace psp::kernel {

namespace {

constexpr double kVblankUs = 1001000000.0 / 60000.0; // 59.94 Hz
uint64_t vcount() { return (uint64_t)(nowUs() / kVblankUs); }
void waitVblank(bool cb) {
    if (cb) runPendingCallbacks();
    uint64_t next = (vcount() + 1) * (uint64_t)kVblankUs + 1;
    uint64_t now = nowUs();
    delayCurrentThread(next > now ? next - now : 1, cb);
    if (cb) runPendingCallbacks();
}

int g_umdCallback = 0;
// PSP_UMD_* bits (PPSSPP sceUmd.h): PRESENT 0x02, READY 0x10, READABLE 0x20. The activation
// notification carries PRESENT|READABLE (READY only for modules with a compiled SDK version,
// which this game does not declare).
constexpr uint32_t kUmdPresent = 0x02, kUmdReady = 0x10, kUmdReadable = 0x20;

// Utility dialogs share one simple state machine: 0 none, 1 init, 2 visible, 3 quit, 4 finished.
struct Dialog {
    int status = 0;
    uint32_t params = 0;
};
Dialog g_savedata, g_msg, g_netconf;

void dialogInit(Dialog& d, uint32_t params) { d.status = 2; d.params = params; }
void dialogShutdown(Dialog& d) { d.status = 0; }
void dialogUpdate(Dialog& d) { if (d.status == 2) d.status = 3; }
uint32_t dialogStatus(Dialog& d) {
    uint32_t s = (uint32_t)d.status;
    if (d.status == 4) d.status = 0;
    return s;
}

// Audio pacing: a blocking output returns when the previous buffer of the channel would
// have finished playing, like the hardware DMA.
uint64_t g_chanBusyUntil[host::kAudioChannels];

uint32_t audioOutput(int ch, int volL, int volR, uint32_t buf, bool blocking) {
    using namespace host;
    if (ch < 0 || ch >= kAudioChannels || !g_audio[ch].reserved) return 0x80260003;
    uint32_t n = g_audio[ch].sampleCount;
    if (blocking) {
        uint64_t now = nowUs();
        if (g_chanBusyUntil[ch] > now) delayCurrentThread(g_chanBusyUntil[ch] - now);
    }
    {
        std::lock_guard<std::mutex> lk(g_audioMutex);
        AudioChannel& a = g_audio[ch];
        bool mono = a.format == 0x10;
        if (a.queue.size() < 44100 * 2) { // drop if the host is not consuming (headless)
            for (uint32_t i = 0; i < n; ++i) {
                int16_t l = (int16_t)read16(buf + (mono ? i * 2 : i * 4));
                int16_t r = mono ? l : (int16_t)read16(buf + i * 4 + 2);
                a.queue.push_back((int16_t)((l * volL) >> 15));
                a.queue.push_back((int16_t)((r * volR) >> 15));
            }
        }
    }
    uint64_t now = nowUs();
    uint64_t dur = (uint64_t)n * 1000000ull / 44100ull;
    g_chanBusyUntil[ch] = std::max(g_chanBusyUntil[ch], now) + dur;
    return n;
}

} // namespace

void registerDeviceHle() {
    using namespace host;
    // ---- sceDisplay
    registerHle("sceDisplaySetMode", [](Ctx* c) { ret(c, 0); });
    registerHle("sceDisplaySetFrameBuf", [](Ctx* c) {
        g_display.topAddr = A(c, 0);
        g_display.bufferWidth = A(c, 1);
        g_display.pixelFormat = A(c, 2);
        g_display.frameCount++;
        ret(c, 0);
        if (gpu::installed() && A(c, 0)) gpu::setDisplay(A(c, 0), A(c, 1), A(c, 2));
    });
    registerHle("sceDisplayGetVcount", [](Ctx* c) { ret(c, (uint32_t)vcount()); });
    registerHle("sceDisplayWaitVblankStart", [](Ctx* c) { waitVblank(false); ret(c, 0); });
    registerHle("sceDisplayWaitVblankStartCB", [](Ctx* c) { waitVblank(true); ret(c, 0); });
    registerHle("sceDisplayGetFramePerSec", [](Ctx* c) { c->f[0] = 59.9400599f; ret(c, 0); });

    // ---- sceCtrl
    registerHle("sceCtrlSetSamplingCycle", [](Ctx* c) { ret(c, 0); });
    registerHle("sceCtrlSetSamplingMode", [](Ctx* c) { ret(c, 1); });
    registerHle("sceCtrlPeekBufferPositive", [](Ctx* c) {
        uint32_t p = A(c, 0);
        int n = (int)A(c, 1);
        for (int i = 0; i < n; ++i, p += 16) {
            write32(p, (uint32_t)nowUs());
            write32(p + 4, g_ctrl.buttons.load());
            write8(p + 8, g_ctrl.lx.load());
            write8(p + 9, g_ctrl.ly.load());
            for (int k = 10; k < 16; ++k) write8(p + k, 0);
        }
        ret(c, (uint32_t)n);
    });

    // ---- sceGe_user
    registerHle("sceGeEdramGetAddr", [](Ctx* c) { ret(c, 0x04000000); });
    registerHle("sceGeGetCmd", [](Ctx* c) { ret(c, ge::reg((int)A(c, 0))); });
    registerHle("sceGeListEnQueue", [](Ctx* c) { ret(c, (uint32_t)ge::enqueue(A(c, 0), A(c, 1), (int)A(c, 2), false)); });
    registerHle("sceGeListEnQueueHead", [](Ctx* c) { ret(c, (uint32_t)ge::enqueue(A(c, 0), A(c, 1), (int)A(c, 2), true)); });
    registerHle("sceGeListUpdateStallAddr", [](Ctx* c) { ret(c, (uint32_t)ge::updateStall((int)A(c, 0), A(c, 1))); });
    registerHle("sceGeListSync", [](Ctx* c) {
        int id = (int)A(c, 0);
        if (A(c, 1) == 1) { ret(c, ge::listDone(id) ? 0 : 2); return; } // peek
        waitUntil([id] { return ge::listDone(id); }, 0);
        ret(c, 0);
    });
    registerHle("sceGeDrawSync", [](Ctx* c) {
        if (A(c, 0) == 1) { ret(c, ge::allDone() ? 0 : 2); return; }
        waitUntil([] { return ge::allDone(); }, 0);
        ret(c, 0);
    });
    registerHle("sceGeBreak", [](Ctx* c) { ret(c, 0); });
    registerHle("sceGeContinue", [](Ctx* c) { ret(c, 0); });
    registerHle("sceGeSetCallback", [](Ctx* c) {
        uint32_t p = A(c, 0);
        ret(c, (uint32_t)ge::setCallback(read32(p), read32(p + 4), read32(p + 8), read32(p + 12)));
    });
    registerHle("sceGeUnsetCallback", [](Ctx* c) { ge::unsetCallback((int)A(c, 0)); ret(c, 0); });

    // ---- sceAudio
    registerHle("sceAudioChReserve", [](Ctx* c) {
        std::lock_guard<std::mutex> lk(g_audioMutex);
        int ch = (int)A(c, 0);
        if (ch < 0) {
            for (int i = kAudioChannels - 1; i >= 0; --i)
                if (!g_audio[i].reserved) { ch = i; break; }
        }
        if (ch < 0 || ch >= kAudioChannels || g_audio[ch].reserved) { ret(c, 0x80260002); return; }
        g_audio[ch].reserved = true;
        g_audio[ch].sampleCount = A(c, 1);
        g_audio[ch].format = A(c, 2);
        g_audio[ch].queue.clear();
        ret(c, (uint32_t)ch);
    });
    registerHle("sceAudioChRelease", [](Ctx* c) {
        std::lock_guard<std::mutex> lk(g_audioMutex);
        int ch = (int)A(c, 0);
        if (ch >= 0 && ch < kAudioChannels) g_audio[ch].reserved = false;
        ret(c, 0);
    });
    registerHle("sceAudioOutputBlocking", [](Ctx* c) {
        int v = (int)A(c, 1);
        ret(c, audioOutput((int)A(c, 0), v, v, A(c, 2), true));
    });
    registerHle("sceAudioOutputPanned", [](Ctx* c) { ret(c, audioOutput((int)A(c, 0), (int)A(c, 1), (int)A(c, 2), A(c, 3), false)); });
    registerHle("sceAudioOutputPannedBlocking", [](Ctx* c) { ret(c, audioOutput((int)A(c, 0), (int)A(c, 1), (int)A(c, 2), A(c, 3), true)); });
    registerHle("sceAudioGetChannelRestLength", [](Ctx* c) {
        int ch = (int)A(c, 0);
        uint64_t now = nowUs();
        uint64_t left = ch >= 0 && ch < kAudioChannels && g_chanBusyUntil[ch] > now ? g_chanBusyUntil[ch] - now : 0;
        ret(c, (uint32_t)(left * 44100 / 1000000));
    });
    registerHle("sceAudioSetChannelDataLen", [](Ctx* c) {
        int ch = (int)A(c, 0);
        if (ch >= 0 && ch < kAudioChannels) g_audio[ch].sampleCount = A(c, 1);
        ret(c, 0);
    });
    registerHle("sceAudioChangeChannelConfig", [](Ctx* c) {
        int ch = (int)A(c, 0);
        if (ch >= 0 && ch < kAudioChannels) g_audio[ch].format = A(c, 1);
        ret(c, 0);
    });
    registerHle("sceAudioChangeChannelVolume", [](Ctx* c) { ret(c, 0); });

    // ---- sceUtility
    registerHle("sceUtilitySavedataInitStart", [](Ctx* c) {
        uint32_t p = A(c, 0);
        log("SavedataInitStart mode=%u", read32(p + 48));
        // Report "no save data" (result field at +28); saving is implemented later.
        write32(p + 28, 0x80110307);
        dialogInit(g_savedata, p);
        ret(c, 0);
    });
    registerHle("sceUtilitySavedataShutdownStart", [](Ctx* c) { g_savedata.status = 4; ret(c, 0); });
    registerHle("sceUtilitySavedataUpdate", [](Ctx* c) { dialogUpdate(g_savedata); ret(c, 0); });
    registerHle("sceUtilitySavedataGetStatus", [](Ctx* c) { ret(c, dialogStatus(g_savedata)); });
    registerHle("sceUtilityMsgDialogInitStart", [](Ctx* c) {
        uint32_t p = A(c, 0);
        log("MsgDialog: %s", readString(p + 0x30 + 0x10, 512).c_str());
        dialogInit(g_msg, p);
        ret(c, 0);
    });
    registerHle("sceUtilityMsgDialogShutdownStart", [](Ctx* c) { g_msg.status = 4; ret(c, 0); });
    registerHle("sceUtilityMsgDialogUpdate", [](Ctx* c) { dialogUpdate(g_msg); ret(c, 0); });
    registerHle("sceUtilityMsgDialogGetStatus", [](Ctx* c) { ret(c, dialogStatus(g_msg)); });
    registerHle("sceUtilityNetconfShutdownStart", [](Ctx* c) { g_netconf.status = 4; ret(c, 0); });
    registerHle("sceUtilityNetconfUpdate", [](Ctx* c) { dialogUpdate(g_netconf); ret(c, 0); });
    registerHle("sceUtilityNetconfGetStatus", [](Ctx* c) { ret(c, dialogStatus(g_netconf)); });
    registerHle("sceUtilityGetSystemParamInt", [](Ctx* c) {
        uint32_t id = A(c, 0), out = A(c, 1);
        uint32_t v = 0;
        switch (id) {
        case 8: v = 1; break; // language: English
        case 9: v = 1; break; // confirm button: cross
        case 2: v = 0; break; // adhoc channel auto
        default: v = 0; break;
        }
        write32(out, v);
        ret(c, 0);
    });

    // ---- sceUmdUser
    registerHle("sceUmdCheckMedium", [](Ctx* c) { ret(c, 1); });
    registerHle("sceUmdActivate", [](Ctx* c) {
        if (g_umdCallback) notifyCallback(g_umdCallback, 1, kUmdPresent | kUmdReadable);
        ret(c, 0);
    });
    registerHle("sceUmdWaitDriveStat", [](Ctx* c) { ret(c, 0); });
    registerHle("sceUmdGetDriveStat", [](Ctx* c) { ret(c, kUmdPresent | kUmdReady | kUmdReadable); });
    registerHle("sceUmdRegisterUMDCallBack", [](Ctx* c) { g_umdCallback = (int)A(c, 0); ret(c, 0); });
    registerHle("sceUmdUnRegisterUMDCallBack", [](Ctx* c) { g_umdCallback = 0; ret(c, 0); });

    // ---- scePower / sceImpose / sceRtc
    registerHle("scePowerRegisterCallback", [](Ctx* c) { ret(c, 0); });
    registerHle("sceImposeSetLanguageMode", [](Ctx* c) { ret(c, 0); });
    registerHle("sceImposeGetLanguageMode", [](Ctx* c) {
        if (A(c, 0)) write32(A(c, 0), 1);
        if (A(c, 1)) write32(A(c, 1), 1);
        ret(c, 0);
    });
    registerHle("sceRtcGetCurrentTick", [](Ctx* c) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(now).count() + 62135596800000000ull;
        write32(A(c, 0), (uint32_t)us);
        write32(A(c, 0) + 4, (uint32_t)(us >> 32));
        ret(c, 0);
    });
    registerHle("sceRtcGetCurrentClockLocalTime", [](Ctx* c) {
        std::time_t t = std::time(nullptr);
        std::tm lt = *std::localtime(&t);
        uint32_t p = A(c, 0);
        write16(p, (uint16_t)(lt.tm_year + 1900));
        write16(p + 2, (uint16_t)(lt.tm_mon + 1));
        write16(p + 4, (uint16_t)lt.tm_mday);
        write16(p + 6, (uint16_t)lt.tm_hour);
        write16(p + 8, (uint16_t)lt.tm_min);
        write16(p + 10, (uint16_t)lt.tm_sec);
        write32(p + 12, 0);
        ret(c, 0);
    });

    // ---- network (ad hoc multiplayer): not available yet
    for (const char* n : {"sceNetInit", "sceNetTerm", "sceNetAdhocInit", "sceNetAdhocTerm", "sceNetAdhocctlInit",
                          "sceNetAdhocctlTerm"})
        registerHle(n, [](Ctx* c) { ret(c, 0); });
    for (const char* n : {"sceNetAdhocctlConnect", "sceNetAdhocctlScan", "sceNetAdhocPdpCreate", "sceNetAdhocPtpOpen",
                          "sceNetAdhocPtpListen", "sceNetAdhocPtpConnect"})
        registerHle(n, [](Ctx* c) { ret(c, 0x80410B80); }); // generic ad hoc error
}

} // namespace psp::kernel
