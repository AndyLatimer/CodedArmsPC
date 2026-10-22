#pragma once
// Hardware renderer for the PSP Graphics Engine.
//
// The GE list walker (psp/ge.cpp) runs on guest threads and hands every drawing command to
// the recorder here, which keeps the GE state, decodes vertices and textures from guest
// memory right away (the game may overwrite them afterwards) and records a list of GL
// operations per displayed frame. The render thread (the one owning the GL context) executes
// those frames with one shader program driven by a uniform block. PSP framebuffers in VRAM
// become GL framebuffer objects rendered at a configurable multiple of the native resolution.
#include <cstdint>
#include <vector>

namespace psp::gpu {

struct Settings {
    int scale = 2;            // internal resolution multiple of 480x272
    bool forceLinear = false; // bilinear filtering on every texture
    float wideAspect = 0;     // > 0: display buffers widened to this aspect (Hor+), e.g. 21/9
};

// Guest side ----------------------------------------------------------------------------
void install(const Settings& s);   // becomes the GE renderer
bool installed();
void setDisplay(uint32_t topaddr, uint32_t stride, uint32_t pixelFormat); // sceDisplaySetFrameBuf
void vblank();                                                           // once per vblank
// Called by the guest thread after a flip: waits (as a kernel wait, other guest threads keep
// running) while the renderer is more than two frames behind, like a game waiting for the GPU.
void throttle();
// The CPU wrote guest memory (e.g. a decoded movie frame into VRAM): framebuffers overlapping
// it are refreshed from memory before they are next used.
void notifyMemoryWrite(uint32_t addr, uint32_t size);

// Render thread -------------------------------------------------------------------------
bool renderInit(); // GL context must be current
struct PresentInfo {
    unsigned texture = 0;      // GL texture holding the displayed framebuffer (row 0 = top)
    int width = 0, height = 0; // texture size in texels
    float u1 = 1, v1 = 1;      // visible 480x272 region within it
    uint64_t frame = 0;
};
// Executes queued frames (waiting up to timeoutMs for one). Returns true when a new frame was
// presented; `out` then describes it.
bool renderPump(PresentInfo& out, int timeoutMs);
// Late camera rotation: draws the last game frame again with the camera turned by the aim the
// game has not rendered yet (host::g_aim). Returns true when it presented (i.e. the camera moved
// since the last call and the frame has a 3D camera).
bool renderReplay(PresentInfo& out);
bool renderReplayDelta(float dyaw, float dpitch, PresentInfo& out); // debug: explicit rotation
void setReplaySigns(float yaw, float pitch);                           // debug: sign calibration
// Reads the visible region of the last presented frame as RGBA8888, top row first.
bool readPresented(const PresentInfo& p, std::vector<uint32_t>& rgba, int& w, int& h);
void renderShutdown();
void dumpNextFrames(int n); // debug: log every draw of the next n frames
void setMaxDraws(int n);                    // debug: execute only the first n draws of each frame (-1 = all)
void setSkipFramebufferTextures(bool skip); // debug: skip draws that sample framebuffers

} // namespace psp::gpu
