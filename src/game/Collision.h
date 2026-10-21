#pragma once
// Triangle-soup collision world with a uniform XZ grid for broad phase. Used both for
// original level geometry and for the provisional tile level.
#include "core/MathUtil.h"
#include <vector>

namespace ca {

struct Triangle {
    Vec3 a, b, c;
};

class CollisionWorld {
public:
    void clear();
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c);
    void build(float cellSize = 2.0f); // call after adding triangles

    // Closest hit along a ray within maxDist.
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist, float& t, Vec3& normal) const;
    // Pushes a sphere out of every triangle it penetrates. Returns true when moved.
    bool pushOutSphere(Vec3& center, float radius, Vec3* lastNormal = nullptr) const;

    size_t triangleCount() const { return tris_.size(); }
    Vec3 boundsMin() const { return min_; }
    Vec3 boundsMax() const { return max_; }

private:
    void cellRange(float x0, float z0, float x1, float z1, int& cx0, int& cz0, int& cx1, int& cz1) const;
    template <class F> void forEachCandidate(float x0, float z0, float x1, float z1, F&& f) const;

    std::vector<Triangle> tris_;
    std::vector<std::vector<unsigned>> cells_;
    mutable std::vector<unsigned> stamp_;
    mutable unsigned stampId_ = 0;
    Vec3 min_, max_;
    float cell_ = 2.0f;
    int nx_ = 0, nz_ = 0;
};

Vec3 closestPointOnTriangle(const Vec3& p, const Triangle& t);

} // namespace ca
