// Coded Arms (PC) - native front end for the statically recompiled game.
//
// The recompiled game code runs on its own threads inside the HLE runtime (src/psp); this
// file owns the window, the OpenGL context (the GE renderer executes here), keyboard/mouse/
// gamepad input mapped onto the PSP controller, audio output and the user settings
// (config.ini next to the executable or in the user's preferences folder).
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/Config.h"
#include "platform/Input.h"
#include "psp/gpu/button_glyphs.h"
#include "psp/gpu/gpu.h"
#include "psp/host.h"
#include "psp/kernel.h"
#include "render/GL.h"

namespace {

// ---- settings -----------------------------------------------------------------------------

enum PspInput {
    IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_CROSS, IN_CIRCLE, IN_SQUARE, IN_TRIANGLE, IN_L, IN_R, IN_START, IN_SELECT,
    IN_STICK_UP, IN_STICK_DOWN, IN_STICK_LEFT, IN_STICK_RIGHT, IN_COUNT
};
const char* kInputNames[IN_COUNT] = {"up", "down", "left", "right", "cross", "circle", "square", "triangle",
                                     "l", "r", "start", "select", "stick_up", "stick_down", "stick_left", "stick_right"};
const uint32_t kInputButtons[12] = {
    psp::host::BTN_UP, psp::host::BTN_DOWN, psp::host::BTN_LEFT, psp::host::BTN_RIGHT, psp::host::BTN_CROSS,
    psp::host::BTN_CIRCLE, psp::host::BTN_SQUARE, psp::host::BTN_TRIANGLE, psp::host::BTN_LTRIGGER,
    psp::host::BTN_RTRIGGER, psp::host::BTN_START, psp::host::BTN_SELECT};
// Defaults follow the game's control type A: analog = move, triangle/cross/square/circle =
// look up/look down/turn left/turn right (also driven by the mouse), up = zoom, down = reload,
// left/right = previous/next weapon, R = attack, L = jump, select = pause, start = map.
const char* kDefaultBindings[IN_COUNT] = {
    "key:Up, mouse:right, pad:dpup", "key:Down, key:R, pad:dpdown", "key:Left, key:Q, mouse:wheeldown, pad:dpleft",
    "key:Right, key:E, mouse:wheelup, pad:dpright", "key:Return, key:K, pad:a", "key:Backspace, key:L, pad:b",
    "key:J, pad:x", "key:I, pad:y", "key:Space, pad:leftshoulder, pad:lefttrigger", "mouse:left, pad:rightshoulder, pad:righttrigger",
    "key:M, key:Tab, pad:back", "key:Escape, key:P, pad:start", "key:W", "key:S", "key:A", "key:D"};

struct FrontSettings {
    std::string iso;
    int width = 1280, height = 720;
    int fullscreen = 0;     // 0 windowed, 1 borderless desktop, 2 exclusive
    bool vsync = true;
    int renderScale = 0;    // 0 = from the window height
    bool linearOutput = true;
    bool forceLinearTextures = false;
    bool stretch = false;
    int widescreen = 1;     // 1: widen the view to the window's aspect (Hor+); 0: original 480x272
    bool pcButtonHints = true; // PC keys in place of the PSP button symbols
    int frameRate = 60;     // 60 or 30 (lowered automatically when the PC cannot keep up); 0 = the game's own (20 in missions)
    float volume = 1.0f;
    float deadzone = 0.2f;
    // Mouse look (until the game's camera is driven directly: mapped onto the look buttons).
    float mouseSensX = 1.0f, mouseSensY = 1.0f;
    bool invertY = false;
    std::string lookUp = "triangle", lookDown = "cross", lookLeft = "square", lookRight = "circle";
    bool mouseLook = true;
    int mouseLookMode = 1;   // 0 off, 1 direct aim, 2 look buttons
    float deadzoneRight = 0.15f, padCurve = 1.6f, padTurnRate = 3.0f; // rad/s at full deflection
    float padSensX = 1.0f, padSensY = 0.8f;
    bool padInvertX = false, padInvertY = false;
    bool invertX = false;
    std::vector<ca::Binding> bindings[IN_COUNT];
    std::string bindingText[IN_COUNT];
};

float getF(const ca::Ini& ini, const char* s, const char* k, float d) {
    std::string v = ini.get(s, k, "");
    return v.empty() ? d : (float)std::atof(v.c_str());
}

void parseList(const std::string& text, std::vector<ca::Binding>& out) {
    out.clear();
    size_t p = 0;
    while (p <= text.size()) {
        size_t c = text.find(',', p);
        std::string part = text.substr(p, c == std::string::npos ? std::string::npos : c - p);
        ca::Binding b;
        if (ca::parseBinding(part, b)) out.push_back(b);
        if (c == std::string::npos) break;
        p = c + 1;
    }
}

void loadSettings(FrontSettings& s, ca::Ini& ini, const std::string& path) {
    ini.load(path);
    s.iso = ini.get("game", "iso", s.iso);
    s.width = (int)getF(ini, "video", "width", (float)s.width);
    s.height = (int)getF(ini, "video", "height", (float)s.height);
    s.fullscreen = (int)getF(ini, "video", "fullscreen", (float)s.fullscreen);
    s.vsync = getF(ini, "video", "vsync", 1) != 0;
    s.renderScale = (int)getF(ini, "video", "render_scale", 0);
    s.linearOutput = getF(ini, "video", "linear_output", 1) != 0;
    s.forceLinearTextures = getF(ini, "video", "force_linear_textures", 0) != 0;
    s.stretch = getF(ini, "video", "stretch", 0) != 0;
    s.widescreen = (int)getF(ini, "video", "widescreen", 1);
    s.frameRate = (int)getF(ini, "video", "frame_rate", 60);
    s.pcButtonHints = getF(ini, "video", "pc_button_hints", 1) != 0;
    s.volume = getF(ini, "audio", "volume", 1.0f);
    s.deadzone = getF(ini, "input", "deadzone", 0.2f);
    s.mouseLookMode = (int)getF(ini, "input", "mouse_look", 1);
    s.mouseLook = s.mouseLookMode != 0;
    s.invertX = getF(ini, "input", "invert_x", 0) != 0;
    s.deadzoneRight = getF(ini, "input", "deadzone_right", 0.15f);
    s.padCurve = getF(ini, "input", "pad_look_curve", 1.6f);
    s.padTurnRate = getF(ini, "input", "pad_turn_rate", 3.0f);
    s.padSensX = getF(ini, "input", "pad_sens_x", 1.0f);
    s.padSensY = getF(ini, "input", "pad_sens_y", 0.8f);
    s.padInvertX = getF(ini, "input", "pad_invert_x", 0) != 0;
    s.padInvertY = getF(ini, "input", "pad_invert_y", 0) != 0;
    s.mouseSensX = getF(ini, "input", "mouse_sens_x", 1.0f);
    s.mouseSensY = getF(ini, "input", "mouse_sens_y", 1.0f);
    s.invertY = getF(ini, "input", "invert_y", 0) != 0;
    for (int i = 0; i < IN_COUNT; ++i) {
        s.bindingText[i] = ini.get("bindings", kInputNames[i], kDefaultBindings[i]);
        if (i == IN_CIRCLE && s.bindingText[i] == "key:L, key:Backspace, pad:b") // earlier default: Backspace
            s.bindingText[i] = kDefaultBindings[i];                               // first for the button hints
        parseList(s.bindingText[i], s.bindings[i]);
    }
}

void saveSettings(const FrontSettings& s, ca::Ini& ini, const std::string& path) {
    ini.set("game", "iso", s.iso);
    ini.set("video", "width", std::to_string(s.width));
    ini.set("video", "height", std::to_string(s.height));
    ini.set("video", "fullscreen", std::to_string(s.fullscreen));
    ini.set("video", "vsync", s.vsync ? "1" : "0");
    ini.set("video", "render_scale", std::to_string(s.renderScale));
    ini.set("video", "linear_output", s.linearOutput ? "1" : "0");
    ini.set("video", "force_linear_textures", s.forceLinearTextures ? "1" : "0");
    ini.set("video", "stretch", s.stretch ? "1" : "0");
    ini.set("video", "widescreen", std::to_string(s.widescreen));
    ini.set("video", "frame_rate", std::to_string(s.frameRate));
    ini.set("video", "pc_button_hints", s.pcButtonHints ? "1" : "0");
    ini.set("audio", "volume", std::to_string(s.volume));
    ini.set("input", "deadzone", std::to_string(s.deadzone));
    ini.set("input", "mouse_look", std::to_string(s.mouseLookMode));
    ini.set("input", "invert_x", s.invertX ? "1" : "0");
    ini.set("input", "deadzone_right", std::to_string(s.deadzoneRight));
    ini.set("input", "pad_look_curve", std::to_string(s.padCurve));
    ini.set("input", "pad_turn_rate", std::to_string(s.padTurnRate));
    ini.set("input", "pad_sens_x", std::to_string(s.padSensX));
    ini.set("input", "pad_sens_y", std::to_string(s.padSensY));
    ini.set("input", "pad_invert_x", s.padInvertX ? "1" : "0");
    ini.set("input", "pad_invert_y", s.padInvertY ? "1" : "0");
    ini.set("input", "mouse_sens_x", std::to_string(s.mouseSensX));
    ini.set("input", "mouse_sens_y", std::to_string(s.mouseSensY));
    ini.set("input", "invert_y", s.invertY ? "1" : "0");
    for (int i = 0; i < IN_COUNT; ++i) ini.set("bindings", kInputNames[i], s.bindingText[i]);
    ini.save(path);
}

// ---- input --------------------------------------------------------------------------------

struct InputState {
    SDL_GameController* pad = nullptr;
    float lookAccumX = 0, lookAccumY = 0;
    bool wheelUp = false, wheelDown = false;
};

bool bindingHeld(const ca::Binding& b, InputState& in, const Uint8* keys, Uint32 mouse) {
    using T = ca::Binding::Type;
    switch (b.type) {
    case T::Key: return keys[b.code] != 0;
    case T::MouseButton: return (mouse & SDL_BUTTON(b.code)) != 0;
    case T::MouseWheelUp: return in.wheelUp;
    case T::MouseWheelDown: return in.wheelDown;
    case T::PadButton: return in.pad && SDL_GameControllerGetButton(in.pad, (SDL_GameControllerButton)b.code);
    case T::PadTrigger: return in.pad && SDL_GameControllerGetAxis(in.pad, (SDL_GameControllerAxis)b.code) > 12000;
    }
    return false;
}

int inputIndex(const std::string& name) {
    for (int i = 0; i < 12; ++i) if (name == kInputNames[i]) return i;
    return -1;
}

// Mouse counts to radians at sensitivity 1 (about 0.13 degrees per count).
constexpr float kRadiansPerCount = 0.0022f;

void updateController(const FrontSettings& s, InputState& in, bool focused, float dt) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    Uint32 mouse = focused ? SDL_GetMouseState(nullptr, nullptr) : 0;
    uint32_t buttons = 0;
    bool held[IN_COUNT] = {};
    for (int i = 0; i < IN_COUNT; ++i)
        for (const auto& b : s.bindings[i])
            if (bindingHeld(b, in, keys, mouse)) held[i] = true;
    for (int i = 0; i < 12; ++i) if (held[i]) buttons |= kInputButtons[i];

    // Analog stick: gamepad left stick, or the digital stick bindings.
    float ax = 0, ay = 0;
    if (in.pad) {
        ax = SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        ay = SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        ca::Vec2 v = ca::applyRadialDeadzone(ax, ay, s.deadzone);
        ax = v.x;
        ay = v.y;
        // Right stick: direct aim at a turn rate (rad/s) with a radial dead zone and a response
        // curve, scaled by the frame time.
        float rx = SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        ca::Vec2 r = ca::applyRadialDeadzone(rx, ry, s.deadzoneRight);
        float mag = std::sqrt(r.x * r.x + r.y * r.y);
        if (mag > 0) {
            float k = std::pow(mag, s.padCurve) / mag;
            std::lock_guard<std::mutex> lk(psp::host::g_aim.m);
            psp::host::g_aim.enabled = true;
            psp::host::g_aim.yaw += r.x * k * s.padTurnRate * s.padSensX * dt * (s.padInvertX ? -1.0f : 1.0f);
            psp::host::g_aim.pitch += r.y * k * s.padTurnRate * s.padSensY * dt * (s.padInvertY ? -1.0f : 1.0f);
        }
    }
    if (held[IN_STICK_LEFT]) ax = -1;
    if (held[IN_STICK_RIGHT]) ax = 1;
    if (held[IN_STICK_UP]) ay = -1;
    if (held[IN_STICK_DOWN]) ay = 1;
    if (ax != 0 && ay != 0 && (held[IN_STICK_LEFT] || held[IN_STICK_RIGHT]) && (held[IN_STICK_UP] || held[IN_STICK_DOWN])) {
        ax *= 0.7071f;
        ay *= 0.7071f;
    }

    // Mouse: direct aim (default) adds the motion to the game's view angles (game_hooks.cpp);
    // mouse_look = 2 instead spends it as presses of the look buttons like the original pad.
    if (s.mouseLookMode == 1 && focused && SDL_GetRelativeMouseMode()) {
        std::lock_guard<std::mutex> lk(psp::host::g_aim.m);
        psp::host::g_aim.enabled = true;
        psp::host::g_aim.yaw += in.lookAccumX * kRadiansPerCount;
        psp::host::g_aim.pitch += in.lookAccumY * kRadiansPerCount;
        in.lookAccumX = in.lookAccumY = 0;
    } else if (s.mouseLookMode == 2 && focused && SDL_GetRelativeMouseMode()) {
        auto press = [&](const std::string& n) { int k = inputIndex(n); if (k >= 0) buttons |= kInputButtons[k]; };
        const float step = 6.0f;
        if (in.lookAccumX <= -step) { press(s.lookLeft); in.lookAccumX += step; }
        else if (in.lookAccumX >= step) { press(s.lookRight); in.lookAccumX -= step; }
        else in.lookAccumX *= 0.8f;
        if (in.lookAccumY <= -step) { press(s.lookUp); in.lookAccumY += step; }
        else if (in.lookAccumY >= step) { press(s.lookDown); in.lookAccumY -= step; }
        else in.lookAccumY *= 0.8f;
    }
    in.wheelUp = in.wheelDown = false;

    psp::host::g_ctrl.buttons = buttons;
    psp::host::g_ctrl.lx = (uint8_t)std::clamp((int)std::lround(128 + ax * 127), 0, 255);
    psp::host::g_ctrl.ly = (uint8_t)std::clamp((int)std::lround(128 + ay * 127), 0, 255);
}

// ---- button hints -----------------------------------------------------------------------------

// Short label of the first keyboard/mouse binding of a PSP button, for the keycaps drawn over
// the game's PSP button symbols (psp/gpu/button_glyphs.h).
std::string keyLabel(const std::vector<ca::Binding>& list) {
    using T = ca::Binding::Type;
    for (const ca::Binding& b : list) {
        if (b.type == T::MouseButton) {
            static const char* n[] = {"", "LMB", "MMB", "RMB", "MB4", "MB5"};
            return b.code >= 1 && b.code <= 5 ? n[b.code] : "";
        }
        if (b.type == T::MouseWheelUp) return "WHU";
        if (b.type == T::MouseWheelDown) return "WHD";
        if (b.type != T::Key) continue;
        const std::string name = SDL_GetScancodeName(SDL_Scancode(b.code));
        static const std::pair<const char*, const char*> kShort[] = {
            {"Return", "\x01"}, {"Keypad Enter", "\x01"}, {"Up", "\x02"}, {"Down", "\x03"}, {"Left", "\x04"},
            {"Right", "\x05"}, {"Backspace", "BKS"}, {"Space", "SPC"}, {"Escape", "ESC"}, {"Tab", "TAB"},
            {"Left Shift", "SHF"}, {"Right Shift", "SHF"}, {"Left Ctrl", "CTL"}, {"Right Ctrl", "CTL"},
            {"Left Alt", "ALT"}, {"Right Alt", "ALT"}, {"CapsLock", "CAP"}, {"Delete", "DEL"}, {"Insert", "INS"},
            {"Home", "HOM"}, {"End", "END"}, {"PageUp", "PGU"}, {"PageDown", "PGD"}};
        for (const auto& [full, label] : kShort)
            if (name == full) return label;
        std::string out;
        for (char c : name)
            if (std::isalnum((unsigned char)c) && out.size() < 3) out += (char)std::toupper((unsigned char)c);
        return out;
    }
    return "";
}

void installButtonLabels(const FrontSettings& s) {
    psp::gpu::ButtonLabels l;
    l.cross = keyLabel(s.bindings[IN_CROSS]);
    l.circle = keyLabel(s.bindings[IN_CIRCLE]);
    l.square = keyLabel(s.bindings[IN_SQUARE]);
    l.triangle = keyLabel(s.bindings[IN_TRIANGLE]);
    l.l = keyLabel(s.bindings[IN_L]);
    l.r = keyLabel(s.bindings[IN_R]);
    l.start = keyLabel(s.bindings[IN_START]);
    l.select = keyLabel(s.bindings[IN_SELECT]);
    psp::gpu::setButtonLabels(l);
}

// ---- audio --------------------------------------------------------------------------------

std::atomic<float> g_volume{1.0f};

std::atomic<int> g_audioUnderruns{0};

// Mixes the game's sceAudio channels. Blocking outputs on the game side wait for these queues
// to drain (host::g_audioConsumer), so the game is paced by this callback and the queues hold
// a steady ~2048 frames: enough to ride out scheduling jitter without audible gaps.
void audioCallback(void*, Uint8* stream, int len) {
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    const int n = len / 2;
    std::vector<int> mix((size_t)n, 0);
    {
        std::lock_guard<std::mutex> lk(psp::host::g_audioMutex);
        for (auto& ch : psp::host::g_audio) {
            if (ch.queue.empty()) continue;
            size_t take = std::min<size_t>(ch.queue.size(), (size_t)n);
            if (take < (size_t)n && ch.reserved) g_audioUnderruns++;
            for (size_t i = 0; i < take; ++i) mix[i] += ch.queue[i];
            ch.queue.erase(ch.queue.begin(), ch.queue.begin() + (long)take);
        }
    }
    psp::kernel::notifyStateChanged();
    const float v = g_volume.load();
    for (int i = 0; i < n; ++i) out[i] = (int16_t)std::clamp((int)(mix[i] * v), -32768, 32767);
}

// ---- present --------------------------------------------------------------------------------

const char* kBlitVs =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "uniform vec4 uRect;\n" // x0,y0,x1,y1 in NDC
    "uniform vec2 uUV1;\n"
    "void main() {\n"
    "  vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : 0.0, (gl_VertexID & 2) != 0 ? 1.0 : 0.0);\n"
    "  vUV = vec2(p.x * uUV1.x, p.y * uUV1.y);\n"
    "  gl_Position = vec4(mix(uRect.x, uRect.z, p.x), mix(uRect.y, uRect.w, p.y), 0.0, 1.0);\n"
    "}\n";
const char* kBlitFs =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vec4(texture(uTex, vUV).rgb, 1.0); }\n";

GLuint makeProgram(const char* vs, const char* fs) {
    auto sh = [](GLenum t, const char* src) {
        GLuint s = glCreateShader(t);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint p = glCreateProgram();
    glAttachShader(p, sh(GL_VERTEX_SHADER, vs));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

std::string findIso(const std::string& configured) {
    if (!configured.empty()) return configured;
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
    for (const char* name : {"game/CodedArms.iso", "CodedArms.iso", "game/CodedArms_EUR.iso"}) {
        std::string p = base + name;
        if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    FrontSettings s;
    ca::Ini ini;
    const std::string cfgPath = ca::resolveConfigPath();
    loadSettings(s, ini, cfgPath);
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iso") && i + 1 < argc) s.iso = argv[++i];
        else if (!std::strcmp(argv[i], "--windowed")) s.fullscreen = 0;
        else if (!std::strcmp(argv[i], "--fullscreen")) s.fullscreen = 1;
    }
    saveSettings(s, ini, cfgPath); // writes the defaults on first run

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    std::string iso = findIso(s.iso);
    if (iso.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Coded Arms",
                                 "Your Coded Arms (ULES-00124) UMD image was not found.\n"
                                 "Put it at game/CodedArms.iso next to the executable, set [game] iso= in config.ini,\n"
                                 "or start with --iso PATH.", nullptr);
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (s.fullscreen == 1) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (s.fullscreen == 2) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* win = SDL_CreateWindow("Coded Arms", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, s.width, s.height, flags);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Coded Arms", "OpenGL 3.3 is required.", win);
        return 1;
    }
    const char* missing = nullptr;
    if (!ca::loadGL(&missing)) { std::fprintf(stderr, "missing GL function %s\n", missing); return 1; }
    SDL_GL_SetSwapInterval(s.vsync ? 1 : 0);

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    psp::gpu::Settings gs;
    gs.scale = s.renderScale > 0 ? s.renderScale : std::clamp((dh + 271) / 272, 1, 8);
    gs.forceLinear = s.forceLinearTextures;
    // Hor+: the PSP's 480x272 is ~16:9; wider windows see more to the sides (set at start-up).
    if (s.widescreen && dw > 0 && dh > 0 && (float)dw / dh > 480.0f / 272.0f + 0.01f) gs.wideAspect = (float)dw / dh;
    psp::gpu::install(gs);
    if (s.pcButtonHints) installButtonLabels(s);
    psp::host::g_frameInterval = s.frameRate >= 60 ? 1 : (s.frameRate >= 30 ? 2 : 0);
    if (!psp::gpu::renderInit()) return 1;
    GLuint blit = makeProgram(kBlitVs, kBlitFs);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    // Audio
    g_volume = s.volume;
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) {
        psp::host::g_audioConsumer = !std::getenv("CA_AUDIO_WALLCLOCK");
        SDL_PauseAudioDevice(audio, 0);
    }

    InputState in;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { in.pad = SDL_GameControllerOpen(i); break; }

    psp::kernel::BootOptions opt;
    opt.isoPath = iso;
    std::thread game([opt] {
        int r = psp::kernel::boot(opt);
        SDL_Event q{};
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
        (void)r;
    });
    game.detach();

    SDL_SetRelativeMouseMode(s.mouseLook ? SDL_TRUE : SDL_FALSE);
    const bool perf = std::getenv("CA_PERF") != nullptr;
    Uint64 lastTicks = SDL_GetPerformanceCounter();
    psp::gpu::PresentInfo frame;
    bool haveFrame = false, running = true, focused = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_KEYDOWN:
                if (e.key.keysym.scancode == SDL_SCANCODE_F11 ||
                    (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (e.key.keysym.scancode == SDL_SCANCODE_F10) {
                    SDL_SetRelativeMouseMode(SDL_GetRelativeMouseMode() ? SDL_FALSE : SDL_TRUE);
                }
                break;
            case SDL_MOUSEMOTION:
                if (SDL_GetRelativeMouseMode()) {
                    in.lookAccumX += e.motion.xrel * s.mouseSensX * (s.invertX ? -1.0f : 1.0f);
                    in.lookAccumY += e.motion.yrel * s.mouseSensY * (s.invertY ? -1.0f : 1.0f);
                }
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y > 0) in.wheelUp = true;
                if (e.wheel.y < 0) in.wheelDown = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!in.pad) in.pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (in.pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in.pad))) {
                    SDL_GameControllerClose(in.pad);
                    in.pad = nullptr;
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) focused = false;
                break;
            }
        }
        Uint64 nowTicks = SDL_GetPerformanceCounter();
        float dt = std::min(0.1f, (float)(nowTicks - lastTicks) / (float)SDL_GetPerformanceFrequency());
        lastTicks = nowTicks;
        updateController(s, in, focused, dt);
        if (perf) {
            static Uint64 lastReport = nowTicks;
            if (nowTicks - lastReport >= SDL_GetPerformanceFrequency()) {
                lastReport = nowTicks;
                std::fprintf(stderr, "[perf] audio underruns %d\n", g_audioUnderruns.exchange(0));
            }
        }

        psp::gpu::PresentInfo p;
        if (psp::gpu::renderPump(p, 4)) { frame = p; haveFrame = true; }

        SDL_GL_GetDrawableSize(win, &dw, &dh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(1, 1, 1, 1);
        glViewport(0, 0, dw, dh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (haveFrame) {
            // Fit 480:272 into the window (or stretch). Framebuffer row 0 is the top row.
            float aspect = (frame.width * frame.u1) / std::max(1.0f, frame.height * frame.v1), wa = (float)dw / (float)std::max(dh, 1);
            float sx = 1, sy = 1;
            if (!s.stretch) { if (wa > aspect) sx = aspect / wa; else sy = wa / aspect; }
            glUseProgram(blit);
            glUniform4f(glGetUniformLocation(blit, "uRect"), -sx, sy, sx, -sy);
            glUniform2f(glGetUniformLocation(blit, "uUV1"), frame.u1, frame.v1);
            glUniform1i(glGetUniformLocation(blit, "uTex"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frame.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s.linearOutput ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.linearOutput ? GL_LINEAR : GL_NEAREST);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        SDL_GL_SwapWindow(win);
    }
    psp::host::g_audioConsumer = false;
    if (audio) SDL_CloseAudioDevice(audio);
    std::_Exit(0);
}
