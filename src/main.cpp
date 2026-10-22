// Coded Arms PC port - entry point.
#include "App.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void usage() {
    std::printf(
        "CodedArms [options]\n"
        "  --iso PATH          your own Coded Arms UMD image (ISO)\n"
        "  --tileset NAME      level snapshot (DG00_L_00P...) or tile set to inspect (DG_00...)\n"
        "  --scale N           original units per metre (default 16, provisional)\n"
        "  --provisional       ignore the game image and use the placeholder level\n"
        "  --autotest DIR      scripted run: writes screenshots to DIR and exits\n");
}

int main(int argc, char** argv) {
    ca::LaunchOptions opt;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--iso")) opt.isoPath = next();
        else if (!std::strcmp(argv[i], "--tileset")) opt.tileset = next();
        else if (!std::strcmp(argv[i], "--scale")) opt.unitsPerMeter = float(std::atof(next()));
        else if (!std::strcmp(argv[i], "--provisional")) opt.forceProvisional = true;
        else if (!std::strcmp(argv[i], "--autotest")) opt.autotestDir = next();
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
    }
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_AUDIO) != 0) {
        // Audio/haptics are optional: retry with video + controllers only.
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
    }
    ca::App app;
    int rc = app.run(opt);
    SDL_Quit();
    return rc;
}
