#include "App.h"
#include "core/Log.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace ca {

static constexpr float kTickRate = 60.0f;        // simulation ticks per second (provisional, see notes)
static constexpr float kDefaultUnitsPerMeter = 16.0f; // original units -> metres: 40-unit cell = 2.5 m (provisional)
static const Vec3 kClearColor{0.02f, 0.03f, 0.05f};

static const float kWhite[4] = {1, 1, 1, 1};
static const float kCyan[4] = {0.45f, 1.0f, 0.85f, 1};
static const float kDim[4] = {0.6f, 0.75f, 0.8f, 1};
static const float kPanel[4] = {0.0f, 0.05f, 0.08f, 0.82f};

static bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

std::string App::findIso() const {
    if (!opt_.isoPath.empty()) return opt_.isoPath;
    Ini ini;
    if (ini.load(configPath_)) {
        std::string p = ini.get("game", "iso", "");
        if (!p.empty() && fileExists(p)) return p;
    }
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
    for (const char* name : {"game/CodedArms.iso", "game/game.iso", "CodedArms.iso", "game.iso"})
        if (fileExists(base + name)) return base + name;
    return "";
}

int App::run(const LaunchOptions& opt) {
    opt_ = opt;
    autotest_ = !opt.autotestDir.empty();
    configPath_ = resolveConfigPath();
    std::string logPath = configPath_.substr(0, configPath_.size() - std::string("config.ini").size()) + "codedarms.log";
    log::init(autotest_ ? "" : logPath);
    log::info("Coded Arms PC port - starting. Config: %s", configPath_.c_str());

    Ini ini;
    if (ini.load(configPath_)) settings_.fromIni(ini);
    Input::defaultBindings(settings_);
    if (autotest_) {
        settings_.width = 1280;
        settings_.height = 720;
        settings_.fullscreen = FullscreenMode::Windowed;
        settings_.vsync = false;
    }
    showOverlay_ = settings_.showOverlay;
    if (!autotest_) saveSettings(); // writes defaults + any newly added keys

    if (!initWindow()) return 1;
    input_.init(settings_);
    loadWorld();

    const double freq = double(SDL_GetPerformanceFrequency());
    uint64_t last = SDL_GetPerformanceCounter();
    double acc = 0, fpsAcc = 0;
    int fpsFrames = 0;
    const float dt = 1.0f / kTickRate;
    int frame = 0;

    while (running_) {
        uint64_t now = SDL_GetPerformanceCounter();
        double frameDt = autotest_ ? 1.0 / 60.0 : double(now - last) / freq;
        last = now;
        frameMs_ = frameDt * 1000.0;
        fpsAcc += frameDt;
        if (++fpsFrames >= 30) { fps_ = fpsFrames / fpsAcc; fpsAcc = 0; fpsFrames = 0; }

        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        input_.update();

        if (paused_) {
            menuInput();
            input_.consumeTick();
        } else {
            if (input_.pressed(Action::Pause)) {
                paused_ = true;
                menuIndex_ = 0;
                SDL_SetRelativeMouseMode(SDL_FALSE);
                input_.consumeTick();
            }
            updateLook(float(frameDt));
            acc += std::min(frameDt, 0.25);
            while (acc >= dt) {
                tick(dt);
                input_.consumeTick();
                acc -= dt;
            }
        }
        if (statusTime_ > 0) statusTime_ -= float(frameDt);
        render(float(acc / dt));

        if (autotest_) {
            ++frame;
            auto shot = [&](int f, const char* name) {
                if (frame == f) takeScreenshot(opt_.autotestDir + "/" + name);
            };
            shot(40, "fp_start.png");
            shot(160, "fp_walk.png");
            if (frame == 161) {
                noclip_ = true;
                Vec3 mn = useOriginal_ ? original_.collision().boundsMin() : provisionalCollision_.boundsMin();
                Vec3 mx = useOriginal_ ? original_.collision().boundsMax() : provisionalCollision_.boundsMax();
                Vec3 c = (mn + mx) * 0.5f;
                float ext = std::max(mx.x - mn.x, mx.z - mn.z);
                flyPos_ = flyPrev_ = {c.x, mx.y + ext * 0.45f, c.z + ext * 0.75f};
                player_.yaw = 0;
                player_.pitch = degToRad(-45);
            }
            shot(163, "overview.png");
            if (frame >= 164) running_ = false;
        }
        SDL_GL_SwapWindow(window_);

        if (!autotest_ && !settings_.vsync && settings_.fpsCap > 0) {
            double target = 1.0 / settings_.fpsCap;
            double spent = double(SDL_GetPerformanceCounter() - now) / freq;
            if (spent < target - 0.002) SDL_Delay(Uint32((target - spent - 0.002) * 1000.0));
            while (double(SDL_GetPerformanceCounter() - now) / freq < target) {}
        }
    }

    Vec3 p = player_.position();
    log::info("Exit: player at (%.2f, %.2f, %.2f) grounded=%d ticks=%llu", p.x, p.y, p.z, player_.grounded() ? 1 : 0,
              (unsigned long long)tickCount_);
    if (!autotest_) saveSettings();
    input_.shutdown();
    original_.unload(renderer_);
    renderer_.shutdown();
    SDL_GL_DeleteContext(gl_);
    SDL_DestroyWindow(window_);
    log::shutdown();
    return 0;
}

bool App::initWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int w = settings_.width > 0 ? settings_.width : 1280, h = settings_.height > 0 ? settings_.height : 720;
    // Try with the configured MSAA first; some drivers reject multisampled formats, in
    // which case we retry without it rather than failing to start.
    for (int attempt = 0; attempt < 2 && !gl_; ++attempt) {
        bool msaa = attempt == 0 && settings_.msaa > 0;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa ? settings_.msaa : 0);
        if (attempt == 1 && settings_.msaa > 0) log::warn("MSAA x%d unavailable, continuing without it", settings_.msaa);
        window_ = SDL_CreateWindow("Coded Arms (PC port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (window_) gl_ = SDL_GL_CreateContext(window_);
        if (!gl_ && window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
        if (!msaa && settings_.msaa == 0) break;
    }
    if (!gl_) {
        log::error("OpenGL 3.3 context: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Coded Arms", "OpenGL 3.3 is required.", nullptr);
        return false;
    }
    if (!renderer_.init(settings_)) return false;
    applyVideoMode();
    return true;
}

void App::applyVideoMode() {
    if (settings_.fullscreen == FullscreenMode::Windowed) {
        SDL_SetWindowFullscreen(window_, 0);
        if (settings_.width > 0 && settings_.height > 0) SDL_SetWindowSize(window_, settings_.width, settings_.height);
    } else if (settings_.fullscreen == FullscreenMode::Borderless) {
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(window_), &mode) == 0) {
            if (settings_.width > 0 && settings_.height > 0) { mode.w = settings_.width; mode.h = settings_.height; }
            SDL_DisplayMode closest;
            if (SDL_GetClosestDisplayMode(SDL_GetWindowDisplayIndex(window_), &mode, &closest)) SDL_SetWindowDisplayMode(window_, &closest);
        }
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN);
    }
    // Adaptive vsync first (less stutter when missing refresh), then regular vsync.
    if (settings_.vsync) {
        if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1);
    } else {
        SDL_GL_SetSwapInterval(0);
    }
    SDL_GL_GetDrawableSize(window_, &drawW_, &drawH_);
    if (!paused_ && !autotest_) SDL_SetRelativeMouseMode(SDL_TRUE);
}

void App::loadWorld() {
    useOriginal_ = false;
    std::string iso = opt_.forceProvisional ? "" : findIso();
    if (!iso.empty() && data_.open(iso)) {
        log::info("Game image: %s", iso.c_str());
        levels_.clear();
        for (const std::string& n : data_.arcEntries("MAIN.ARC")) // DGxx_[LMS]_00P.RLVSNAP
            if (n.size() > 8 && n.compare(n.size() - 8, 8, ".RLVSNAP") == 0 && n.rfind("DG", 0) == 0)
                levels_.push_back({"DG_" + n.substr(2, 2), n.substr(0, n.size() - 8)});
        for (const std::string& n : data_.arcEntries("DG.ARC"))
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".BPX") == 0) levels_.push_back({n.substr(0, n.size() - 4), ""});
        for (size_t i = 0; i < levels_.size(); ++i)
            if (!opt_.tileset.empty() && (levels_[i].snapshot == opt_.tileset || (levels_[i].snapshot.empty() && levels_[i].tileset == opt_.tileset)))
                levelIndex_ = int(i);
        useOriginal_ = !levels_.empty() && loadLevel(levelIndex_);
        // Original title logo, used in the pause menu (MAIN.ARC/TPP_TITLE.BPX, image 1).
        if (!logoTex_) {
            Image logo;
            ByteView title = data_.arcEntry("MAIN.ARC", "TPP_TITLE.BPX");
            if (title.valid() && loadPspiImage(bpxFind(title, "TEX.PSPIP"), 1, logo)) {
                logoTex_ = renderer_.uploadTexture(logo);
                logoW_ = logo.width;
                logoH_ = logo.height;
            }
        }
    } else {
        log::warn("No game image found: put your Coded Arms ISO at game/CodedArms.iso or set [game] iso= in config.ini");
    }
    if (!useOriginal_) buildProvisionalWorld();
}

bool App::loadLevel(int index) {
    const LevelEntry& L = levels_[size_t(index)];
    float upm = opt_.unitsPerMeter > 0 ? opt_.unitsPerMeter : kDefaultUnitsPerMeter;
    std::string err;
    bool ok = (original_.tileset() == L.tileset && original_.instanceCount() > 0) ||
              original_.loadTileset(data_, renderer_, L.tileset, upm, err);
    if (ok) {
        if (L.snapshot.empty()) original_.showAllPieces();
        else ok = original_.buildFromSnapshot(data_, L.snapshot, err);
    }
    if (!ok) {
        log::warn("Could not load %s (%s)", L.snapshot.empty() ? L.tileset.c_str() : L.snapshot.c_str(), err.c_str());
        return false;
    }
    player_.spawn(original_.suggestedSpawn(), original_.suggestedYaw());
    noclip_ = L.snapshot.empty(); // inspection mode starts with the free camera
    flyPos_ = flyPrev_ = player_.eye(1.0f);
    status_ = original_.description();
    statusTime_ = 3;
    return true;
}

void App::buildProvisionalWorld() {
    level_.generate(0xC0DEA125u);
    level_.buildCollision(provisionalCollision_);
    // Placeholder look: plain tiles tinted by surface orientation. NOT original art.
    std::vector<ModelVertex> tris;
    level_.forEachQuad([&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        Vec3 n = normalize(cross(b - a, c - a));
        uint32_t col = n.y > 0.5f ? 0xFF403020u : 0xFF806040u;
        auto vtx = [&](Vec3 p) {
            ModelVertex v{};
            v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
            v.color = col;
            return v;
        };
        for (Vec3 p : {a, b, c, a, c, d}) tris.push_back(vtx(p));
    });
    provisionalMesh_ = renderer_.uploadMesh(tris, 0);
    player_.spawn(level_.spawnPoint(), 0);
}

void App::handleEvent(const SDL_Event& e) {
    input_.handleEvent(e);
    switch (e.type) {
    case SDL_QUIT: running_ = false; break;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) SDL_GL_GetDrawableSize(window_, &drawW_, &drawH_);
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !paused_ && !autotest_) {
            paused_ = true;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
        break;
    case SDL_KEYDOWN:
        if (e.key.repeat) break;
        if (e.key.keysym.sym == SDLK_F3) showOverlay_ = !showOverlay_;
        if (e.key.keysym.sym == SDLK_F2) {
            noclip_ = !noclip_;
            flyPos_ = flyPrev_ = player_.eye(1.0f);
            status_ = noclip_ ? "Free camera ON (F2)" : "Free camera OFF";
            statusTime_ = 2;
        }
        if (e.key.keysym.sym == SDLK_F11) {
            settings_.fullscreen = settings_.fullscreen == FullscreenMode::Windowed ? FullscreenMode::Borderless : FullscreenMode::Windowed;
            applyVideoMode();
        }
        if (e.key.keysym.sym == SDLK_F12) {
            char name[64];
            std::snprintf(name, sizeof(name), "screenshot_%lld.png", (long long)std::time(nullptr));
            takeScreenshot(name);
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (!paused_ && !autotest_) SDL_SetRelativeMouseMode(SDL_TRUE);
        break;
    default: break;
    }
}

void App::updateLook(float frameDt) {
    Vec2 m = input_.takeMouseDelta();
    if (autotest_) m = {0, 0};
    const float degPerCount = 0.022f; // same scale as common PC shooters, so sensitivities transfer
    float yawDeg = m.x * settings_.mouseSensX * degPerCount * (settings_.mouseInvertX ? -1.f : 1.f);
    float pitchDeg = -m.y * settings_.mouseSensY * degPerCount * (settings_.mouseInvertY ? -1.f : 1.f);
    Vec2 pad = input_.padLook();
    const float padRate = 180.0f; // deg/s at full deflection, scaled by sensitivity
    yawDeg += pad.x * padRate * settings_.padSensX * frameDt * (settings_.padInvertX ? -1.f : 1.f);
    pitchDeg += -pad.y * padRate * settings_.padSensY * frameDt * (settings_.padInvertY ? -1.f : 1.f);
    player_.yaw += degToRad(yawDeg);
    player_.pitch = clampf(player_.pitch + degToRad(pitchDeg), degToRad(-89), degToRad(89));
}

void App::autotestScript(int t, PlayerInput& in) {
    // Fall to the floor, walk forward, turn while strafing, then jump.
    if (t > 30 && t < 150) in.move.y = 1;
    if (t > 90 && t < 120) { in.move.x = 0.5f; player_.yaw += degToRad(1.0f); }
    if (t == 130) in.jump = true;
}

void App::tick(float dt) {
    ++tickCount_;
    const CollisionWorld& world = useOriginal_ ? original_.collision() : provisionalCollision_;
    if (noclip_) {
        flyPrev_ = flyPos_;
        Vec2 mv = input_.moveAxis();
        Vec3 dir = viewDir(player_.yaw, player_.pitch);
        Vec3 right{std::cos(player_.yaw), 0, std::sin(player_.yaw)};
        float speed = 12.0f;
        flyPos_ += (dir * mv.y + right * mv.x) * (speed * dt);
        if (input_.held(Action::Jump)) flyPos_.y += speed * dt;
        return;
    }
    PlayerInput in;
    in.move = input_.moveAxis();
    in.jump = input_.pressed(Action::Jump);
    in.aiming = input_.held(Action::Aim);
    if (autotest_) autotestScript(int(tickCount_), in);
    player_.tick(in, world, dt);
    if (input_.pressed(Action::Fire)) input_.rumble(0.3f, 0.6f, 80);
    // Respawn if the player falls out of the world (geometry is not assembled yet).
    if (player_.position().y < world.boundsMin().y - 20.0f) {
        player_.spawn(useOriginal_ ? original_.suggestedSpawn() : level_.spawnPoint(), player_.yaw);
        status_ = "Fell out of the world - respawned";
        statusTime_ = 2;
    }
}

Mat4 App::viewProj(float alpha, Vec3& eye) const {
    eye = noclip_ ? lerp(flyPrev_, flyPos_, alpha) : player_.eye(alpha);
    Vec3 dir = viewDir(player_.yaw, player_.pitch);
    float aspect = drawH_ > 0 ? float(drawW_) / float(drawH_) : 16.0f / 9.0f;
    // Hor+: the configured FOV is horizontal at 16:9; vertical FOV stays constant so
    // ultrawide screens see more to the sides instead of less above/below.
    float vfov = 2.0f * std::atan(std::tan(degToRad(settings_.fovHorizontal16x9) * 0.5f) / (16.0f / 9.0f));
    Mat4 proj = Mat4::perspective(vfov, aspect, 0.05f, 500.0f);
    return proj * Mat4::lookAt(eye, eye + dir, {0, 1, 0});
}

void App::render(float alpha) {
    renderer_.beginFrame(drawW_, drawH_, kClearColor);
    Vec3 eye;
    Mat4 vp = viewProj(alpha, eye);
    renderer_.setCamera(vp, eye);
    renderer_.setFog(kClearColor, 60.0f, 160.0f);
    if (useOriginal_) original_.draw(renderer_);
    else renderer_.drawMesh(provisionalMesh_, Mat4::identity(), kWhite);
    drawHud();
}

void App::drawHud() {
    renderer_.beginHud(drawW_, drawH_);
    float ui = float(drawH_) / 720.0f * settings_.hudScale;
    // On ultrawide screens keep HUD elements inside a centred 16:9 region.
    float safeW = settings_.hudClamp16x9 ? std::min(float(drawW_), float(drawH_) * 16.0f / 9.0f) : float(drawW_);
    float left = (float(drawW_) - safeW) * 0.5f;
    float cx = drawW_ * 0.5f, cy = drawH_ * 0.5f;

    if (!paused_) { // crosshair (placeholder until the original HUD layout is decoded)
        float l = 8 * ui, t = std::max(1.0f, 2 * ui), g = 4 * ui;
        renderer_.hudRect(cx - t / 2, cy - g - l, t, l, kCyan);
        renderer_.hudRect(cx - t / 2, cy + g, t, l, kCyan);
        renderer_.hudRect(cx - g - l, cy - t / 2, l, t, kCyan);
        renderer_.hudRect(cx + g, cy - t / 2, l, t, kCyan);
    }
    if (showOverlay_) {
        char buf[512];
        Vec3 p = player_.position();
        std::snprintf(buf, sizeof(buf),
                      "FPS %.0f (%.2f ms)  ticks %llu\npos %.2f %.2f %.2f  grounded %d  %s\nworld: %s  tris %zu\ninput: %s %s\nGL: %s",
                      fps_, frameMs_, (unsigned long long)tickCount_, p.x, p.y, p.z, player_.grounded() ? 1 : 0,
                      noclip_ ? "[free cam]" : "",
                      useOriginal_ ? ("ORIGINAL " + original_.description()).c_str() : "PROVISIONAL (no game image)",
                      useOriginal_ ? original_.triangleCount() : provisionalCollision_.triangleCount(),
                      input_.lastDevice() == InputDevice::Gamepad ? "gamepad" : "keyboard/mouse",
                      input_.hasGamepad() ? ("[" + input_.gamepadName() + "]").c_str() : "",
                      renderer_.glInfo().c_str());
        renderer_.hudRect(left + 8 * ui, 8 * ui, 520 * ui, 70 * ui, kPanel);
        renderer_.hudText(left + 14 * ui, 14 * ui, 1.6f * ui, buf, kWhite);
    }
    if (statusTime_ > 0) {
        float w = renderer_.hudTextWidth(status_, 2 * ui);
        renderer_.hudText(cx - w / 2, drawH_ - 60 * ui, 2 * ui, status_, kCyan);
    }
    if (paused_) drawMenu();
    renderer_.endHud();
}

static const char* kFsNames[] = {"Windowed", "Borderless", "Exclusive"};
static const char* kFilterNamesUi[] = {"Nearest (PSP-like)", "Bilinear", "Trilinear"};
static const int kFpsCaps[] = {0, 30, 60, 120, 144, 165, 240};

enum MenuItem { MResume, MFullscreen, MVsync, MFpsCap, MFov, MSens, MInvert, MFilter, MRumble, MVolume, MTileset, MQuit, MCount };

void App::drawMenu() {
    float ui = float(drawH_) / 720.0f * settings_.hudScale;
    float cx = drawW_ * 0.5f;
    float panelW = 560 * ui, rowH = 30 * ui;
    float top = drawH_ * 0.5f - (MCount * rowH) * 0.5f - 40 * ui;
    renderer_.hudRect(0, 0, float(drawW_), float(drawH_), kPanel);
    if (logoTex_) {
        float lw = 512 * ui, lh = lw * logoH_ / logoW_;
        renderer_.hudImage(logoTex_, cx - lw / 2, top - lh - 10 * ui, lw, lh, kWhite);
    }
    char v[128];
    for (int i = 0; i < MCount; ++i) {
        std::string label;
        switch (i) {
        case MResume: label = "Resume"; break;
        case MFullscreen: label = std::string("Display mode: ") + kFsNames[int(settings_.fullscreen)]; break;
        case MVsync: label = std::string("VSync: ") + (settings_.vsync ? "On" : "Off"); break;
        case MFpsCap: label = "Frame rate cap: " + (settings_.fpsCap ? std::to_string(settings_.fpsCap) : std::string("Unlimited")); break;
        case MFov: std::snprintf(v, sizeof(v), "Field of view (16:9): %.0f", settings_.fovHorizontal16x9); label = v; break;
        case MSens: std::snprintf(v, sizeof(v), "Mouse sensitivity: %.2f", settings_.mouseSensX); label = v; break;
        case MInvert: label = std::string("Invert look Y: ") + (settings_.mouseInvertY ? "On" : "Off"); break;
        case MFilter: label = std::string("Texture filter: ") + kFilterNamesUi[int(settings_.textureFilter)]; break;
        case MRumble: label = std::string("Controller vibration: ") + (settings_.rumble ? "On" : "Off"); break;
        case MVolume: std::snprintf(v, sizeof(v), "Master volume: %d%%", int(settings_.masterVolume * 100 + 0.5f)); label = v; break;
        case MTileset: label = "Level: " + (useOriginal_ ? original_.description() : std::string("(no game image)")); break;
        case MQuit: label = "Quit"; break;
        }
        float y = top + i * rowH;
        static const float kSel[4] = {kCyan[0] * 0.25f, kCyan[1] * 0.25f, kCyan[2] * 0.25f, 0.9f};
        if (i == menuIndex_) renderer_.hudRect(cx - panelW / 2, y - 4 * ui, panelW, rowH - 2 * ui, kSel);
        float w = renderer_.hudTextWidth(label, 2 * ui);
        renderer_.hudText(cx - w / 2, y, 2 * ui, label, i == menuIndex_ ? kCyan : kDim);
    }
    std::string hint = "Up/Down select - Left/Right change - Enter/A confirm - Esc/B back";
    float w = renderer_.hudTextWidth(hint, 1.5f * ui);
    renderer_.hudText(cx - w / 2, top + MCount * rowH + 16 * ui, 1.5f * ui, hint, kDim);
}

void App::menuInput() {
    // Mouse hover selects rows.
    int mx, my;
    Uint32 buttons = SDL_GetMouseState(&mx, &my);
    (void)buttons;
    float ui = float(drawH_) / 720.0f * settings_.hudScale;
    float rowH = 30 * ui, top = drawH_ * 0.5f - (MCount * rowH) * 0.5f - 40 * ui;
    int ww, wh;
    SDL_GetWindowSize(window_, &ww, &wh);
    float sx = ww > 0 ? float(drawW_) / ww : 1, sy = wh > 0 ? float(drawH_) / wh : 1;
    static int lastMx = -1, lastMy = -1;
    bool mouseMoved = mx != lastMx || my != lastMy;
    lastMx = mx; lastMy = my;
    int hovered = int(std::floor((my * sy - top + 4 * ui) / rowH));
    bool overRow = hovered >= 0 && hovered < MCount && std::fabs(mx * sx - drawW_ * 0.5f) < 280 * ui;
    if (mouseMoved && overRow) menuIndex_ = hovered;

    int delta = 0;
    bool accept = input_.pressed(Action::MenuAccept);
    if (input_.pressed(Action::Fire) && overRow) { accept = true; }
    if (input_.pressed(Action::MenuUp)) menuIndex_ = (menuIndex_ + MCount - 1) % MCount;
    if (input_.pressed(Action::MenuDown)) menuIndex_ = (menuIndex_ + 1) % MCount;
    if (input_.pressed(Action::MenuLeft)) delta = -1;
    if (input_.pressed(Action::MenuRight)) delta = 1;
    if (input_.pressed(Action::MenuBack) || input_.pressed(Action::Pause)) {
        paused_ = false;
        SDL_SetRelativeMouseMode(SDL_TRUE);
        input_.takeMouseDelta();
        saveSettings();
        return;
    }
    if (accept && delta == 0) delta = 1;
    if (delta == 0) return;

    switch (menuIndex_) {
    case MResume:
        if (accept) { paused_ = false; SDL_SetRelativeMouseMode(SDL_TRUE); input_.takeMouseDelta(); }
        return;
    case MFullscreen: settings_.fullscreen = FullscreenMode((int(settings_.fullscreen) + 3 + delta) % 3); applyVideoMode(); SDL_SetRelativeMouseMode(SDL_FALSE); break;
    case MVsync: settings_.vsync = !settings_.vsync; applyVideoMode(); SDL_SetRelativeMouseMode(SDL_FALSE); break;
    case MFpsCap: {
        int n = int(sizeof(kFpsCaps) / sizeof(kFpsCaps[0])), cur = 0;
        for (int i = 0; i < n; ++i) if (kFpsCaps[i] == settings_.fpsCap) cur = i;
        settings_.fpsCap = kFpsCaps[(cur + n + delta) % n];
        break;
    }
    case MFov: settings_.fovHorizontal16x9 += 5.0f * delta; break;
    case MSens: settings_.mouseSensX += 0.25f * delta; settings_.mouseSensY = settings_.mouseSensX; break;
    case MInvert: settings_.mouseInvertY = !settings_.mouseInvertY; settings_.padInvertY = settings_.mouseInvertY; break;
    case MFilter: settings_.textureFilter = TextureFilter((int(settings_.textureFilter) + 3 + delta) % 3); renderer_.applySettings(settings_); break;
    case MRumble: settings_.rumble = !settings_.rumble; input_.applySettings(settings_); if (settings_.rumble) input_.rumble(0.5f, 0.5f, 150); break;
    case MVolume: settings_.masterVolume += 0.1f * delta; break;
    case MTileset:
        if (useOriginal_ && !levels_.empty()) {
            int n = int(levels_.size());
            levelIndex_ = (levelIndex_ + n + delta) % n;
            loadLevel(levelIndex_);
        }
        break;
    case MQuit: if (accept) running_ = false; return;
    }
    settings_.clampValues();
}

void App::saveSettings() {
    Ini ini;
    ini.load(configPath_); // keep unknown sections such as [game]
    settings_.showOverlay = showOverlay_;
    settings_.toIni(ini);
    if (!data_.isoPath().empty()) ini.set("game", "iso", data_.isoPath());
    if (!ini.save(configPath_)) log::warn("Could not save %s", configPath_.c_str());
}

void App::takeScreenshot(const std::string& path) {
    if (renderer_.screenshot(path, drawW_, drawH_)) {
        log::info("Screenshot: %s", path.c_str());
        status_ = "Saved " + path;
        statusTime_ = 2;
    } else {
        log::warn("Screenshot failed: %s", path.c_str());
    }
}

} // namespace ca
