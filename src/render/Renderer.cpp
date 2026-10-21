#include "render/Renderer.h"
#include "core/Log.h"
#include <cstring>

#include "stb_easy_font.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace ca {

static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aColor;
uniform mat4 uVP;
uniform mat4 uModel;
out vec2 vUV;
out vec4 vColor;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vUV = aUV;
    vColor = aColor;
    gl_Position = uVP * w;
}
)";

static const char* kWorldFS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in vec3 vWorld;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec4 uTint;
uniform vec3 uFogColor;
uniform vec3 uFogRange;
uniform vec3 uEye;
out vec4 oColor;
void main() {
    vec4 t = uUseTex != 0 ? texture(uTex, vUV) : vec4(1.0);
    // GE "modulate": texture * vertex colour. Vertex alpha is not applied yet (its use
    // in the original data is unconfirmed, see TECHNICAL_NOTES.md); texture alpha is
    // used for the alpha test like the PSP's GE.
    vec4 c = vec4(t.rgb * vColor.rgb, t.a) * uTint;
    if (c.a < 0.5) discard;
    float d = distance(vWorld, uEye);
    float f = clamp((d - uFogRange.x) / max(0.001, uFogRange.y - uFogRange.x), 0.0, 1.0);
    oColor = vec4(mix(c.rgb, uFogColor, f), 1.0);
}
)";

static const char* kHudVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main() { vUV = aUV; vColor = aColor; gl_Position = uProj * vec4(aPos, 0.0, 1.0); }
)";

static const char* kHudFS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
uniform int uUseTex;
out vec4 oColor;
void main() { oColor = (uUseTex != 0 ? texture(uTex, vUV) : vec4(1.0)) * vColor; }
)";

GLuint Renderer::compile(const char* vs, const char* fs) {
    auto stage = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char logBuf[2048];
            glGetShaderInfoLog(s, sizeof(logBuf), nullptr, logBuf);
            log::error("Shader compile failed: %s", logBuf);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };
    GLuint v = stage(GL_VERTEX_SHADER, vs), f = stage(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char logBuf[2048];
        glGetProgramInfoLog(p, sizeof(logBuf), nullptr, logBuf);
        log::error("Program link failed: %s", logBuf);
        return 0;
    }
    return p;
}

bool Renderer::init(const Settings& s) {
    const char* missing = nullptr;
    if (!loadGL(&missing)) {
        log::error("OpenGL function missing: %s", missing ? missing : "?");
        return false;
    }
    glInfo_ = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / " +
              reinterpret_cast<const char*>(glGetString(GL_VERSION));
    log::info("OpenGL: %s", glInfo_.c_str());

    worldProg_ = compile(kWorldVS, kWorldFS);
    hudProg_ = compile(kHudVS, kHudFS);
    if (!worldProg_ || !hudProg_) return false;
    uVP_ = glGetUniformLocation(worldProg_, "uVP");
    uModel_ = glGetUniformLocation(worldProg_, "uModel");
    uTint_ = glGetUniformLocation(worldProg_, "uTint");
    uUseTex_ = glGetUniformLocation(worldProg_, "uUseTex");
    uFogColor_ = glGetUniformLocation(worldProg_, "uFogColor");
    uFogRange_ = glGetUniformLocation(worldProg_, "uFogRange");
    uEye_ = glGetUniformLocation(worldProg_, "uEye");
    uHudProj_ = glGetUniformLocation(hudProg_, "uProj");
    uHudUseTex_ = glGetUniformLocation(hudProg_, "uUseTex");
    glUseProgram(worldProg_);
    glUniform1i(glGetUniformLocation(worldProg_, "uTex"), 0);
    glUseProgram(hudProg_);
    glUniform1i(glGetUniformLocation(hudProg_, "uTex"), 0);

    if (hasGLExtension("GL_EXT_texture_filter_anisotropic") || hasGLExtension("GL_ARB_texture_filter_anisotropic"))
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso_);

    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(8));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(16));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    applySettings(s);
    Image white{1, 1, {255, 255, 255, 255}};
    whiteTex_ = uploadTexture(white);
    return true;
}

void Renderer::shutdown() {
    if (!textures_.empty()) glDeleteTextures(GLsizei(textures_.size()), textures_.data());
    textures_.clear();
    if (hudVbo_) glDeleteBuffers(1, &hudVbo_);
    if (hudVao_) glDeleteVertexArrays(1, &hudVao_);
    if (worldProg_) glDeleteProgram(worldProg_);
    if (hudProg_) glDeleteProgram(hudProg_);
    hudVbo_ = hudVao_ = worldProg_ = hudProg_ = 0;
}

void Renderer::applyFilter(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint minF = GL_NEAREST_MIPMAP_NEAREST, magF = GL_NEAREST;
    if (filter_ == TextureFilter::Bilinear) { minF = GL_LINEAR_MIPMAP_NEAREST; magF = GL_LINEAR; }
    if (filter_ == TextureFilter::Trilinear) { minF = GL_LINEAR_MIPMAP_LINEAR; magF = GL_LINEAR; }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
    if (maxAniso_ > 1.0f)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                        filter_ == TextureFilter::Nearest ? 1.0f : std::min(float(anisotropy_), maxAniso_));
}

void Renderer::applySettings(const Settings& s) {
    filter_ = s.textureFilter;
    anisotropy_ = s.anisotropy;
    for (GLuint t : textures_) applyFilter(t);
}

GLuint Renderer::uploadTexture(const Image& img) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    textures_.push_back(t);
    applyFilter(t);
    return t;
}

GpuMesh Renderer::uploadMesh(const std::vector<ModelVertex>& tris, GLuint texture) {
    static_assert(sizeof(ModelVertex) == 36, "vertex layout");
    GpuMesh m;
    m.count = GLsizei(tris.size());
    m.texture = texture;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(tris.size() * sizeof(ModelVertex)), tris.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(12));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(24));
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 36, reinterpret_cast<void*>(32));
    for (GLuint i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
    glBindVertexArray(0);
    return m;
}

void Renderer::destroyMesh(GpuMesh& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = GpuMesh{};
}

void Renderer::beginFrame(int width, int height, const Vec3& clear) {
    glViewport(0, 0, width, height);
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); // original strips have mixed winding; culling is off like many PSP titles
    glUseProgram(worldProg_);
    setFog(clear, 1000.0f, 2000.0f);
}

void Renderer::setCamera(const Mat4& viewProj, const Vec3& eye) {
    glUseProgram(worldProg_);
    glUniformMatrix4fv(uVP_, 1, GL_FALSE, viewProj.m);
    glUniform3f(uEye_, eye.x, eye.y, eye.z);
}

void Renderer::setFog(const Vec3& color, float start, float end) {
    glUseProgram(worldProg_);
    glUniform3f(uFogColor_, color.x, color.y, color.z);
    glUniform4f(uTint_, 1, 1, 1, 1);
    glUniform3f(uFogRange_, start, end, 0.0f);
}

void Renderer::drawMesh(const GpuMesh& m, const Mat4& model, const float tint[4]) {
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
    glUniform4f(uTint_, tint[0], tint[1], tint[2], tint[3]);
    glUniform1i(uUseTex_, m.texture ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m.texture ? m.texture : whiteTex_);
    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, 0, m.count);
}

void Renderer::beginHud(int width, int height) {
    hudW_ = width;
    hudH_ = height;
    hudVerts_.clear();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hudProg_);
    Mat4 proj = Mat4::ortho(0, float(width), float(height), 0, -1, 1);
    glUniformMatrix4fv(uHudProj_, 1, GL_FALSE, proj.m);
}

static void pushQuad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                     float v1, const float c[4]) {
    const float q[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1},
                           {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
    for (auto& p : q) v.insert(v.end(), {p[0], p[1], p[2], p[3], c[0], c[1], c[2], c[3]});
}

void Renderer::hudRect(float x, float y, float w, float h, const float rgba[4]) {
    pushQuad(hudVerts_, x, y, x + w, y + h, 0, 0, 1, 1, rgba);
}

void Renderer::hudText(float x, float y, float scale, const std::string& text, const float rgba[4]) {
    static char buf[60000];
    int quads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr, buf, sizeof(buf));
    const float* f = reinterpret_cast<const float*>(buf);
    for (int q = 0; q < quads; ++q) {
        const float* v = f + q * 16; // 4 vertices of x,y,z,color(4 bytes)
        float x0 = x + v[0] * scale, y0 = y + v[1] * scale, x1 = x + v[8] * scale, y1 = y + v[9] * scale;
        pushQuad(hudVerts_, x0, y0, x1, y1, 0, 0, 1, 1, rgba);
    }
}

float Renderer::hudTextWidth(const std::string& text, float scale) const {
    return float(stb_easy_font_width(const_cast<char*>(text.c_str()))) * scale;
}

void Renderer::hudImage(GLuint tex, float x, float y, float w, float h, const float rgba[4]) {
    // Flush pending untextured geometry, draw this image, continue.
    endHud();
    std::vector<float> v;
    pushQuad(v, x, y, x + w, y + h, 0, 0, 1, 1, rgba);
    glUniform1i(uHudUseTex_, 1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 8));
}

void Renderer::endHud() {
    if (hudVerts_.empty()) return;
    glUniform1i(uHudUseTex_, 0);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(hudVerts_.size() * sizeof(float)), hudVerts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(hudVerts_.size() / 8));
    hudVerts_.clear();
}

bool Renderer::screenshot(const std::string& pngPath, int width, int height) {
    std::vector<uint8_t> px(size_t(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(pngPath.c_str(), width, height, 3, px.data(), width * 3) != 0;
}

} // namespace ca
