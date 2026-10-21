#pragma once
#include "core/Config.h"
#include "game/Level.h"
#include "game/OriginalWorld.h"
#include "game/Player.h"
#include "platform/Input.h"
#include "render/Renderer.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace ca {

struct LaunchOptions {
    std::string isoPath;          // empty: from config or auto-detect
    std::string tileset;          // e.g. DG_00
    std::string autotestDir;      // non-empty: scripted run that writes screenshots, then exits
    bool forceProvisional = false;
    float unitsPerMeter = 0;      // 0: default
};

class App {
public:
    int run(const LaunchOptions& opt);

private:
    bool initWindow();
    void applyVideoMode();
    void loadWorld();
    void buildProvisionalWorld();
    void handleEvent(const SDL_Event& e);
    void updateLook(float frameDt);
    void tick(float dt);
    void render(float alpha);
    void drawHud();
    void drawMenu();
    void menuInput();
    void saveSettings();
    void takeScreenshot(const std::string& path);
    void autotestScript(int tickIndex, PlayerInput& in);
    std::string findIso() const;
    Mat4 viewProj(float alpha, Vec3& eyeOut) const;

    LaunchOptions opt_;
    Settings settings_;
    std::string configPath_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int drawW_ = 1280, drawH_ = 720;
    Renderer renderer_;
    Input input_;

    // World: original geometry when the game image is available, provisional otherwise.
    GameData data_;
    OriginalWorld original_;
    bool useOriginal_ = false;
    Level level_;
    CollisionWorld provisionalCollision_;
    GpuMesh provisionalMesh_;
    std::vector<std::string> tilesets_;
    int tilesetIndex_ = 0;
    GLuint logoTex_ = 0;
    int logoW_ = 0, logoH_ = 0;

    Player player_;
    bool noclip_ = false;
    Vec3 flyPos_, flyPrev_;
    bool paused_ = false;
    int menuIndex_ = 0;
    bool running_ = true;
    bool showOverlay_ = false;
    std::string status_;
    float statusTime_ = 0;

    // Stats
    double fps_ = 0, frameMs_ = 0;
    uint64_t tickCount_ = 0;
    bool autotest_ = false;
};

} // namespace ca
