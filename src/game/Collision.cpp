#include "game/Collision.h"
#include <cfloat>
#include <cmath>

namespace ca {

void CollisionWorld::clear() {
    tris_.clear();
    cells_.clear();
    nx_ = nz_ = 0;
}

void CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    tris_.push_back({a, b, c});
}

void CollisionWorld::build(float cellSize) {
    cell_ = cellSize;
    min_ = {FLT_MAX, FLT_MAX, FLT_MAX};
    max_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const Triangle& t : tris_)
        for (const Vec3* v : {&t.a, &t.b, &t.c}) {
            min_ = {std::min(min_.x, v->x), std::min(min_.y, v->y), std::min(min_.z, v->z)};
            max_ = {std::max(max_.x, v->x), std::max(max_.y, v->y), std::max(max_.z, v->z)};
        }
    if (tris_.empty()) { min_ = max_ = {}; }
    nx_ = std::max(1, int(std::ceil((max_.x - min_.x) / cell_)) + 1);
    nz_ = std::max(1, int(std::ceil((max_.z - min_.z) / cell_)) + 1);
    cells_.assign(size_t(nx_) * size_t(nz_), {});
    for (unsigned i = 0; i < tris_.size(); ++i) {
        const Triangle& t = tris_[i];
        int cx0, cz0, cx1, cz1;
        cellRange(std::min({t.a.x, t.b.x, t.c.x}), std::min({t.a.z, t.b.z, t.c.z}),
                  std::max({t.a.x, t.b.x, t.c.x}), std::max({t.a.z, t.b.z, t.c.z}), cx0, cz0, cx1, cz1);
        for (int z = cz0; z <= cz1; ++z)
            for (int x = cx0; x <= cx1; ++x) cells_[size_t(z) * nx_ + x].push_back(i);
    }
    stamp_.assign(tris_.size(), 0);
}

void CollisionWorld::cellRange(float x0, float z0, float x1, float z1, int& cx0, int& cz0, int& cx1, int& cz1) const {
    auto cl = [](int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); };
    cx0 = cl(int(std::floor((x0 - min_.x) / cell_)), nx_);
    cx1 = cl(int(std::floor((x1 - min_.x) / cell_)), nx_);
    cz0 = cl(int(std::floor((z0 - min_.z) / cell_)), nz_);
    cz1 = cl(int(std::floor((z1 - min_.z) / cell_)), nz_);
}

template <class F>
void CollisionWorld::forEachCandidate(float x0, float z0, float x1, float z1, F&& f) const {
    if (cells_.empty()) return;
    if (++stampId_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0); stampId_ = 1; }
    int cx0, cz0, cx1, cz1;
    cellRange(x0, z0, x1, z1, cx0, cz0, cx1, cz1);
    for (int z = cz0; z <= cz1; ++z)
        for (int x = cx0; x <= cx1; ++x)
            for (unsigned i : cells_[size_t(z) * nx_ + x]) {
                if (stamp_[i] == stampId_) continue;
                stamp_[i] = stampId_;
                f(tris_[i]);
            }
}

static bool rayTriangle(const Vec3& o, const Vec3& d, const Triangle& t, float& outT) {
    Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
    Vec3 p = cross(d, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    Vec3 s = o - t.a;
    float u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    outT = dot(e2, q) * inv;
    return outT >= 0;
}

bool CollisionWorld::raycast(const Vec3& origin, const Vec3& dir, float maxDist, float& t, Vec3& normal) const {
    Vec3 end = origin + dir * maxDist;
    bool hit = false;
    t = maxDist;
    forEachCandidate(std::min(origin.x, end.x), std::min(origin.z, end.z), std::max(origin.x, end.x),
                     std::max(origin.z, end.z), [&](const Triangle& tri) {
                         float th;
                         if (rayTriangle(origin, dir, tri, th) && th <= t) {
                             t = th;
                             normal = normalize(cross(tri.b - tri.a, tri.c - tri.a));
                             if (dot(normal, dir) > 0) normal = normal * -1.0f;
                             hit = true;
                         }
                     });
    return hit;
}

// Real-Time Collision Detection (Ericson), 5.1.5.
Vec3 closestPointOnTriangle(const Vec3& p, const Triangle& t) {
    const Vec3 &a = t.a, &b = t.b, &c = t.c;
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

bool CollisionWorld::pushOutSphere(Vec3& center, float radius, Vec3* lastNormal) const {
    bool moved = false;
    forEachCandidate(center.x - radius, center.z - radius, center.x + radius, center.z + radius,
                     [&](const Triangle& tri) {
                         Vec3 cp = closestPointOnTriangle(center, tri);
                         Vec3 d = center - cp;
                         float dist2 = dot(d, d);
                         if (dist2 >= radius * radius) return;
                         float dist = std::sqrt(dist2);
                         Vec3 n = dist > 1e-6f ? d * (1.0f / dist) : normalize(cross(tri.b - tri.a, tri.c - tri.a));
                         center += n * (radius - dist + 1e-4f);
                         if (lastNormal) *lastNormal = n;
                         moved = true;
                     });
    return moved;
}

} // namespace ca
