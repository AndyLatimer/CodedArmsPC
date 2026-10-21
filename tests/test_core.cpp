// Unit tests for engine code that does not need a window or GPU.
#include "core/Config.h"
#include "game/Level.h"
#include "game/Player.h"
#include "platform/Input.h"
#include <SDL.h>
#include <cmath>
#include <cstdio>

using namespace ca;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static void testIniRoundTrip() {
    Settings a;
    a.width = 2560; a.height = 1080; a.fullscreen = FullscreenMode::Borderless;
    a.mouseSensX = 1.5f; a.mouseInvertY = true; a.textureFilter = TextureFilter::Nearest;
    a.bindings["fire"] = "mouse:left, pad:righttrigger";
    Ini ini;
    a.toIni(ini);
    Ini parsed;
    parsed.parse(ini.serialize());
    Settings b;
    b.fromIni(parsed);
    CHECK(b.width == 2560 && b.height == 1080);
    CHECK(b.fullscreen == FullscreenMode::Borderless);
    CHECK(std::fabs(b.mouseSensX - 1.5f) < 1e-3f);
    CHECK(b.mouseInvertY);
    CHECK(b.textureFilter == TextureFilter::Nearest);
    CHECK(b.bindings["fire"] == "mouse:left, pad:righttrigger");
}

static void testClamp() {
    Ini ini;
    ini.parse("[video]\nfov = 400\nmsaa = 3\n[input]\ndeadzone_left = -1\n");
    Settings s;
    s.fromIni(ini);
    CHECK(s.fovHorizontal16x9 == 120.0f);
    CHECK(s.msaa == 0);
    CHECK(s.deadzoneLeft == 0.0f);
}

static void testBindings() {
    Binding b;
    CHECK(parseBinding("key:W", b) && b.type == Binding::Type::Key && b.code == SDL_SCANCODE_W);
    CHECK(parseBinding("key: Space", b) && b.code == SDL_SCANCODE_SPACE);
    CHECK(parseBinding("mouse:right", b) && b.type == Binding::Type::MouseButton && b.code == SDL_BUTTON_RIGHT);
    CHECK(parseBinding("mouse:wheelup", b) && b.type == Binding::Type::MouseWheelUp);
    CHECK(parseBinding("pad:a", b) && b.type == Binding::Type::PadButton && b.code == SDL_CONTROLLER_BUTTON_A);
    CHECK(parseBinding("pad:righttrigger", b) && b.type == Binding::Type::PadTrigger);
    CHECK(!parseBinding("key:NotAKey", b));
    CHECK(!parseBinding("garbage", b));
    CHECK(parseBinding(describeBinding(Binding{Binding::Type::PadButton, SDL_CONTROLLER_BUTTON_Y}), b) &&
          b.code == SDL_CONTROLLER_BUTTON_Y);
}

static void testDeadzone() {
    Vec2 v = applyRadialDeadzone(0.1f, 0.1f, 0.2f);
    CHECK(v.x == 0 && v.y == 0);
    v = applyRadialDeadzone(1.0f, 0.0f, 0.2f);
    CHECK(std::fabs(v.x - 1.0f) < 1e-4f && v.y == 0);
    v = applyRadialDeadzone(0.21f, 0.0f, 0.2f);
    CHECK(v.x > 0 && v.x < 0.05f); // continuous just outside the dead zone
    v = applyRadialDeadzone(0.0f, -0.59f, 0.2f);
    CHECK(v.y < -0.49f && v.y > -0.51f); // rescaled: (0.59-0.2)/(0.98-0.2) = 0.5
}

static void testLevelDeterministic() {
    Level a, b;
    a.generate(1234);
    b.generate(1234);
    bool same = true;
    for (int z = 0; z < a.depth(); ++z)
        for (int x = 0; x < a.width(); ++x) same = same && a.at(x, z) == b.at(x, z);
    CHECK(same);
    int sx = int(a.spawnPoint().x / Level::kCellSize), sz = int(a.spawnPoint().z / Level::kCellSize);
    CHECK(a.at(sx, sz) == Cell::Floor);
    CHECK(a.at(-1, 0) == Cell::Wall);
}

// Open 5x5 room bounded by walls, used for deterministic physics checks.
static Level makeRoom() {
    Level l;
    l.generate(1, 7, 7);
    for (int z = 0; z < 7; ++z)
        for (int x = 0; x < 7; ++x)
            l.set(x, z, (x == 0 || z == 0 || x == 6 || z == 6) ? Cell::Wall : Cell::Floor);
    return l;
}

static void testWalkAndWall() {
    Level l = makeRoom();
    CollisionWorld w;
    l.buildCollision(w);
    Player p;
    p.spawn({14, 0, 14}, 0); // centre, facing -Z
    PlayerInput in;
    in.move = {0, 1};
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) p.tick(in, w, dt);
    // 1 s of walking at 5 m/s, minus a short acceleration phase.
    float moved = 14 - p.position().z;
    CHECK(moved > 4.5f && moved < 5.01f);
    CHECK(p.grounded());
    for (int i = 0; i < 600; ++i) p.tick(in, w, dt);
    // Wall row z=0 spans [0,4]; the body must stop at 4 + radius.
    CHECK(std::fabs(p.position().z - (4.0f + p.params.radius)) < 0.01f);
}

static void testJumpOntoBlock() {
    Level l = makeRoom();
    l.set(3, 2, Cell::Block); // block spanning z in [8,12], x in [12,16]
    CollisionWorld w;
    l.buildCollision(w);
    Player p;
    p.spawn({14, 0, 14}, 0);
    PlayerInput in;
    const float dt = 1.0f / 60.0f;
    // Walking into the block without jumping: blocked at z = 12 + radius.
    in.move = {0, 1};
    for (int i = 0; i < 120; ++i) p.tick(in, w, dt);
    CHECK(std::fabs(p.position().z - (12.0f + p.params.radius)) < 0.01f);
    CHECK(p.position().y == 0.0f);
    // Jump while pushing forward: lands on top (y = 1).
    in.jump = true;
    p.tick(in, w, dt);
    in.jump = false;
    for (int i = 0; i < 60; ++i) p.tick(in, w, dt);
    CHECK(std::fabs(p.position().y - Level::kBlockHeight) < 1e-4f);
    CHECK(p.grounded());
}

static void testFixedStepIndependence() {
    // The same 2 s of input yields the same result regardless of render frame rate,
    // because simulation always advances in fixed 1/60 s ticks.
    Level l = makeRoom();
    CollisionWorld w;
    l.buildCollision(w);
    auto run = [&](double frameDt) {
        Player p;
        p.spawn({14, 0, 20}, 0);
        PlayerInput in;
        in.move = {0.3f, 1};
        double acc = 0, t = 0;
        while (t < 2.0) {
            acc += frameDt;
            t += frameDt;
            while (acc >= 1.0 / 60.0) { p.tick(in, w, 1.0f / 60.0f); acc -= 1.0 / 60.0; }
        }
        return p.position();
    };
    Vec3 a = run(1.0 / 30.0), b = run(1.0 / 240.0);
    CHECK(std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.z - b.z) < 1e-4f);
}

static void testRaycast() {
    Level l = makeRoom();
    Vec3 hit, n;
    CHECK(l.raycast({14, 1.5f, 14}, {0, 0, -1}, 100, hit, n));
    CHECK(std::fabs(hit.z - 4.0f) < 1e-3f && n.z == 1.0f);
    CHECK(l.raycast({14, 1.5f, 14}, normalize({0, -1, -1}), 100, hit, n));
    CHECK(std::fabs(hit.y) < 1e-3f && n.y == 1.0f); // hits the floor first
}

int main(int, char**) {
    testIniRoundTrip();
    testClamp();
    testBindings();
    testDeadzone();
    testLevelDeterministic();
    testWalkAndWall();
    testJumpOntoBlock();
    testFixedStepIndependence();
    testRaycast();
    if (g_failures == 0) std::printf("All core tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
