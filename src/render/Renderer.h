#pragma once
// OpenGL 3.3 renderer: textured world meshes (original PSP vertex data), HUD text and
// shapes, screenshots. Rendering is deliberately close to the PSP GE pipeline
// (texture * vertex colour, alpha test) so the original look is preserved.
#include "assets/Formats.h"
#include "core/Config.h"
#include "core/MathUtil.h"
#include "render/GL.h"
#include <string>
#include <vector>

namespace ca {

struct GpuMesh {
    GLuint vao = 0, vbo = 0;
    GLsizei count = 0;
    GLuint texture = 0; // 0 = untextured
};

class Renderer {
public:
    bool init(const Settings& s);
    void shutdown();
    void applySettings(const Settings& s);
    std::string glInfo() const { return glInfo_; }

    GLuint uploadTexture(const Image& img);
    GpuMesh uploadMesh(const std::vector<ModelVertex>& tris, GLuint texture);
    void destroyMesh(GpuMesh& m);

    void beginFrame(int width, int height, const Vec3& clearColor);
    void setCamera(const Mat4& viewProj, const Vec3& eye);
    void setFog(const Vec3& color, float start, float end);
    void drawMesh(const GpuMesh& m, const Mat4& model, const float tint[4]);

    // HUD in pixel coordinates, origin top-left.
    void beginHud(int width, int height);
    void hudRect(float x, float y, float w, float h, const float rgba[4]);
    void hudText(float x, float y, float scale, const std::string& text, const float rgba[4]);
    float hudTextWidth(const std::string& text, float scale) const;
    void hudImage(GLuint tex, float x, float y, float w, float h, const float rgba[4]);
    void endHud();

    bool screenshot(const std::string& pngPath, int width, int height);

private:
    GLuint compile(const char* vs, const char* fs);
    void applyFilter(GLuint tex);

    GLuint worldProg_ = 0, hudProg_ = 0;
    GLint uVP_ = -1, uModel_ = -1, uTint_ = -1, uUseTex_ = -1, uFogColor_ = -1, uFogRange_ = -1, uEye_ = -1;
    GLint uHudProj_ = -1, uHudUseTex_ = -1;
    GLuint whiteTex_ = 0, hudVao_ = 0, hudVbo_ = 0;
    std::vector<float> hudVerts_; // x, y, u, v, r, g, b, a
    std::vector<GLuint> textures_;
    TextureFilter filter_ = TextureFilter::Trilinear;
    int anisotropy_ = 1;
    float maxAniso_ = 1.0f;
    std::string glInfo_;
    int hudW_ = 0, hudH_ = 0;
};

} // namespace ca
