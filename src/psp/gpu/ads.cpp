#include "psp/gpu/ads.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "psp/host.h"

namespace psp::gpu {

namespace {

constexpr float kZoom = 1.3f;       // field of view narrowed by this factor while aiming
constexpr float kTransitionS = 0.15f;
constexpr float kCloser = 0.8f;     // the weapon comes this much towards the eye

using Mat = float[16]; // column-major

void identity(float* m) { std::memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1; }
void mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = v;
        }
    std::memcpy(out, r, sizeof r);
}
void translate(float* m, float x, float y, float z) { identity(m); m[12] = x; m[13] = y; m[14] = z; }
void rotX(float* m, float a) { identity(m); m[5] = std::cos(a); m[6] = std::sin(a); m[9] = -std::sin(a); m[10] = std::cos(a); }
void rotY(float* m, float a) { identity(m); m[0] = std::cos(a); m[2] = -std::sin(a); m[8] = std::sin(a); m[10] = std::cos(a); }

void xform(const float* m12, const float* p, float* out) { // GE 4x3 (columns of 3)
    for (int r = 0; r < 3; ++r) out[r] = m12[r] * p[0] + m12[3 + r] * p[1] + m12[6 + r] * p[2] + m12[9 + r];
}

double nowUs() {
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

uint64_t AimDownSights::viewKey(const float view[12]) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 12; ++i) {
        uint32_t v;
        std::memcpy(&v, &view[i], 4);
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

// The weapon's camera: near the origin, axes along the view's (this game's is turned 180 degrees).
bool AimDownSights::isWeaponCamera(const float* v) {
    float c[3];
    for (int k = 0; k < 3; ++k) c[k] = -(v[3 * k] * v[9] + v[3 * k + 1] * v[10] + v[3 * k + 2] * v[11]);
    return std::fabs(c[0]) + std::fabs(c[1]) + std::fabs(c[2]) < 20 && std::fabs(v[0]) > 0.95f && v[4] > 0.95f && std::fabs(v[8]) > 0.95f;
}

void AimDownSights::observe(const float view[12], const float world[12], const std::vector<Vertex>& verts) {
    keys_.push_back(viewKey(view));
    static const bool dbg = std::getenv("CA_ADS_DEBUG") != nullptr;
    if (dbg) {
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = -(view[3 * k] * view[9] + view[3 * k + 1] * view[10] + view[3 * k + 2] * view[11]);
        if (std::fabs(c[0]) + std::fabs(c[1]) + std::fabs(c[2]) < 20)
            std::fprintf(stderr, "[ADS] near view %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f c %.1f %.1f %.1f\n", view[0], view[1],
                         view[2], view[3], view[4], view[5], view[6], view[7], view[8], c[0], c[1], c[2]);
    }
    if (!isWeaponCamera(view) || verts.empty()) return;
    std::vector<float> pts;
    pts.reserve(verts.size() * 3);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const Vertex& vx : verts) {
        float w[3], e[3];
        xform(world, vx.pos, w);
        xform(view, w, e);
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], e[k]); hi[k] = std::max(hi[k], e[k]); pts.push_back(e[k]); }
    }
    // The weapon: narrow, or wholly in front of the camera (the arms are wide and reach back).
    // Fallback for other shapes: everything of the weapon camera in front of the hands' reach.
    if (hi[0] - lo[0] < 1.2f || hi[2] < -2.0f) gun_.insert(gun_.end(), pts.begin(), pts.end());
    for (size_t i = 0; i < pts.size(); i += 3)
        if (pts[i + 2] < -2.0f) front_.insert(front_.end(), pts.begin() + (long)i, pts.begin() + (long)i + 3);
}

void AimDownSights::endFrame() {
    const double now = nowUs();
    const float dt = lastUs_ ? (float)std::min(0.1, (now - lastUs_) / 1e6) : 0.0f;
    lastUs_ = now;
    const bool want = host::g_ads.load();

    // The player's camera: the view of most 3D draws.
    std::unordered_map<uint64_t, int> counts;
    int best = 0;
    for (uint64_t k : keys_)
        if (++counts[k] > best) { best = counts[k]; mainView_ = k; }
    keys_.clear();

    // Measure the weapon when aiming starts (from hip), then keep that alignment; measure again
    // when a different weapon appears (its vertex count changes for a few frames).
    if (want && t_ == 0) aligned_ = false;
    if (gun_.size() < 3 * 12) gun_.swap(front_);
    front_.clear();
    const size_t gunVerts = gun_.size() / 3;
    if (want && aligned_ && gunVerts) {
        const bool other = gunVerts * 4 < alignedVerts_ * 3 || gunVerts * 3 > alignedVerts_ * 4;
        changed_ = other ? changed_ + 1 : 0;
        if (changed_ >= 5) { aligned_ = false; changed_ = 0; }
    }
    if (want && !aligned_ && gun_.size() >= 3 * 12) {
        float maxY = -1e9f, minY = 1e9f;
        for (size_t i = 0; i < gun_.size(); i += 3) { maxY = std::max(maxY, gun_[i + 1]); minY = std::min(minY, gun_[i + 1]); }
        const float edge = maxY - 0.12f * (maxY - minY);
        // Least squares over the top edge: x = a + b z, y = c + d z.
        double n = 0, sz = 0, szz = 0, sx = 0, sxz = 0, sy = 0, syz = 0;
        for (size_t i = 0; i < gun_.size(); i += 3) {
            if (gun_[i + 1] < edge) continue;
            const double x = gun_[i], y = gun_[i + 1], z = gun_[i + 2];
            n += 1; sz += z; szz += z * z; sx += x; sxz += x * z; sy += y; syz += y * z;
        }
        if (n >= 3) {
            const double den = n * szz - sz * sz;
            const double b = den > 1e-6 ? (n * sxz - sx * sz) / den : 0, d = den > 1e-6 ? (n * syz - sy * sz) / den : 0;
            sight_[0] = (float)(sx / n);
            sight_[1] = maxY;
            sight_[2] = (float)(sz / n);
            // Turn the barrel (b, d, 1) onto the z axis: yaw first, then pitch.
            yaw_ = (float)std::atan(-std::clamp(b, -0.5, 0.5));
            pitch_ = (float)std::atan2(std::clamp(d, -0.5, 0.5), std::sqrt(1 + b * b));
            aligned_ = true;
            alignedVerts_ = gunVerts;
        }
    }
    static const bool dbg = std::getenv("CA_ADS_DEBUG") != nullptr;
    if (dbg && want)
        std::fprintf(stderr, "[ADS] gun %zu aligned %d t %.2f sight %.2f,%.2f,%.2f yaw %.3f pitch %.3f main %d\n", gun_.size() / 3,
                     (int)aligned_, t_, sight_[0], sight_[1], sight_[2], yaw_, pitch_, best);
    gun_.clear();

    const float target = (want && aligned_) ? 1.0f : 0.0f;
    if (kTransitionS > 0 && dt > 0) t_ += (target > t_ ? 1 : -1) * dt / kTransitionS;
    t_ = std::clamp(t_, 0.0f, 1.0f);
    if (target == 0 && t_ == 0) aligned_ = false;

    // M(t) = T(S + (S' - S) t) * Rx(pitch t) * Ry(yaw t) * T(-S), S' = the sight on the axis.
    float a[16], b[16], c[16], d[16];
    const float s = t_ * t_ * (3 - 2 * t_); // smoothstep
    translate(a, sight_[0] * (1 - s), sight_[1] * (1 - s), sight_[2] + kCloser * s);
    rotX(b, pitch_ * s);
    rotY(c, yaw_ * s);
    translate(d, -sight_[0], -sight_[1], -sight_[2]);
    mul(a, b, adsMat_);
    mul(adsMat_, c, adsMat_);
    mul(adsMat_, d, adsMat_);
}

void AimDownSights::adjust(const float view[12], float view44[16], float proj44[16]) const {
    if (t_ <= 0) return;
    if (isWeaponCamera(view)) {
        mul(adsMat_, view44, view44);
    } else if (viewKey(view) == mainView_) {
        const float s = t_ * t_ * (3 - 2 * t_);
        const float z = 1 + (kZoom - 1) * s;
        proj44[0] *= z;
        proj44[5] *= z;
    }
}

bool AimDownSights::hide2D(const std::vector<Vertex>& verts) const {
    if (t_ < 0.25f || verts.empty()) return false;
    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
    for (const Vertex& v : verts)
        for (int k = 0; k < 2; ++k) { lo[k] = std::min(lo[k], v.pos[k]); hi[k] = std::max(hi[k], v.pos[k]); }
    // The crosshair: small and centred on the screen (480x272).
    const float cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2;
    return std::fabs(cx - 240) < 8 && std::fabs(cy - 136) < 8 && hi[0] - lo[0] < 80 && hi[1] - lo[1] < 80;
}

} // namespace psp::gpu
