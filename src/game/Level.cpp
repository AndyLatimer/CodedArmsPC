#include "game/Level.h"
#include <cmath>

namespace ca {

Cell Level::at(int x, int z) const {
    if (x < 0 || z < 0 || x >= w_ || z >= d_) return Cell::Wall;
    return cells_[size_t(z * w_ + x)];
}

void Level::set(int x, int z, Cell c) {
    if (x < 0 || z < 0 || x >= w_ || z >= d_) return;
    cells_[size_t(z * w_ + x)] = c;
}

static Vec3 cellCenter(int x, int z) {
    return {(x + 0.5f) * Level::kCellSize, 0.0f, (z + 0.5f) * Level::kCellSize};
}

void Level::generate(unsigned seed, int width, int depth) {
    seed_ = seed;
    w_ = width;
    d_ = depth;
    cells_.assign(size_t(w_ * d_), Cell::Wall);
    Rng rng(seed);

    struct Room { int x, z, w, d; };
    std::vector<Room> rooms;
    for (int attempt = 0; attempt < 80 && rooms.size() < 9 && w_ >= 12 && d_ >= 12; ++attempt) {
        Room r{0, 0, rng.range(3, 6), rng.range(3, 6)};
        r.x = rng.range(1, w_ - r.w - 2);
        r.z = rng.range(1, d_ - r.d - 2);
        bool overlap = false;
        for (const Room& o : rooms)
            if (r.x - 1 < o.x + o.w && r.x + r.w + 1 > o.x && r.z - 1 < o.z + o.d && r.z + r.d + 1 > o.z) overlap = true;
        if (overlap) continue;
        for (int z = r.z; z < r.z + r.d; ++z)
            for (int x = r.x; x < r.x + r.w; ++x) set(x, z, Cell::Floor);
        rooms.push_back(r);
    }
    // L-shaped corridors between consecutive rooms keep the level fully connected.
    for (size_t i = 1; i < rooms.size(); ++i) {
        int ax = rooms[i - 1].x + rooms[i - 1].w / 2, az = rooms[i - 1].z + rooms[i - 1].d / 2;
        int bx = rooms[i].x + rooms[i].w / 2, bz = rooms[i].z + rooms[i].d / 2;
        bool horizontalFirst = rng.next() & 1;
        int cx = horizontalFirst ? bx : ax;
        for (int x = std::min(ax, cx); x <= std::max(ax, cx); ++x) set(x, az, Cell::Floor);
        for (int z = std::min(az, bz); z <= std::max(az, bz); ++z) set(cx, z, Cell::Floor);
        for (int x = std::min(cx, bx); x <= std::max(cx, bx); ++x) set(x, bz, Cell::Floor);
    }
    // A few low blocks inside rooms (never on the room centre, which corridors use).
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& r = rooms[i];
        if (r.w < 4 || r.d < 4) continue;
        int bx = r.x + 1 + int(rng.next() % unsigned(r.w - 2));
        int bz = r.z + 1 + int(rng.next() % unsigned(r.d - 2));
        if (bx != r.x + r.w / 2 && bz != r.z + r.d / 2) set(bx, bz, Cell::Block);
    }
    if (rooms.empty()) { // degenerate size: single open room
        for (int z = 1; z < d_ - 1; ++z)
            for (int x = 1; x < w_ - 1; ++x) set(x, z, Cell::Floor);
        rooms.push_back({1, 1, w_ - 2, d_ - 2});
    }
    spawn_ = cellCenter(rooms.front().x + rooms.front().w / 2, rooms.front().z + rooms.front().d / 2);
    goal_ = cellCenter(rooms.back().x + rooms.back().w / 2, rooms.back().z + rooms.back().d / 2);
}

void Level::solidsInRegion(const Aabb& region, std::vector<Aabb>& out) const {
    int x0 = int(std::floor(region.min.x / kCellSize)), x1 = int(std::floor(region.max.x / kCellSize));
    int z0 = int(std::floor(region.min.z / kCellSize)), z1 = int(std::floor(region.max.z / kCellSize));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            Cell c = at(x, z);
            if (c == Cell::Floor) continue;
            float h = c == Cell::Wall ? kWallHeight * 4 : kBlockHeight; // walls are effectively unbounded
            out.push_back({{x * kCellSize, c == Cell::Wall ? -100.0f : 0.0f, z * kCellSize},
                           {(x + 1) * kCellSize, h, (z + 1) * kCellSize}});
        }
    // Floor slab
    out.push_back({{region.min.x - 1, -10.0f, region.min.z - 1}, {region.max.x + 1, 0.0f, region.max.z + 1}});
}

void Level::buildCollision(CollisionWorld& world) const {
    world.clear();
    forEachQuad([&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        world.addTriangle(a, b, c);
        world.addTriangle(a, c, d);
    });
    world.build();
}

void Level::forEachQuad(const std::function<void(Vec3, Vec3, Vec3, Vec3)>& quad) const {
    const float s = kCellSize;
    for (int z = 0; z < d_; ++z)
        for (int x = 0; x < w_; ++x) {
            Cell c = at(x, z);
            if (c == Cell::Wall) continue;
            float x0 = x * s, x1 = x0 + s, z0 = z * s, z1 = z0 + s;
            float top = c == Cell::Block ? kBlockHeight : 0.0f;
            quad({x0, top, z0}, {x0, top, z1}, {x1, top, z1}, {x1, top, z0});
            // Side faces toward this open cell from taller neighbours.
            auto side = [&](int nx, int nz, Vec3 a, Vec3 b) {
                Cell n = at(nx, nz);
                float h = n == Cell::Wall ? kWallHeight : (n == Cell::Block ? kBlockHeight : 0.0f);
                if (h <= top) return;
                quad({a.x, top, a.z}, {b.x, top, b.z}, {b.x, h, b.z}, {a.x, h, a.z});
            };
            side(x, z - 1, {x0, 0, z0}, {x1, 0, z0});
            side(x, z + 1, {x1, 0, z1}, {x0, 0, z1});
            side(x - 1, z, {x0, 0, z1}, {x0, 0, z0});
            side(x + 1, z, {x1, 0, z0}, {x1, 0, z1});
        }
}

static bool rayAabb(const Vec3& o, const Vec3& d, const Aabb& b, float& tHit, Vec3& n) {
    float tmin = 0, tmax = 1e30f;
    Vec3 nmin;
    const float* oo = &o.x; const float* dd = &d.x; const float* bmin = &b.min.x; const float* bmax = &b.max.x;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-8f) {
            if (oo[i] < bmin[i] || oo[i] > bmax[i]) return false;
            continue;
        }
        float inv = 1.0f / dd[i];
        float t1 = (bmin[i] - oo[i]) * inv, t2 = (bmax[i] - oo[i]) * inv;
        float sign = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
        if (t1 > tmin) {
            tmin = t1;
            nmin = {};
            (&nmin.x)[i] = sign;
        }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    tHit = tmin;
    n = nmin;
    return true;
}

bool Level::raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3& hitPoint, Vec3& hitNormal) const {
    Vec3 end = origin + dir * maxDist;
    Aabb region{{std::min(origin.x, end.x), 0, std::min(origin.z, end.z)},
                {std::max(origin.x, end.x), 0, std::max(origin.z, end.z)}};
    std::vector<Aabb> solids;
    solidsInRegion(region, solids);
    float best = maxDist;
    bool hit = false;
    for (const Aabb& b : solids) {
        float t;
        Vec3 n;
        if (rayAabb(origin, dir, b, t, n) && t < best && t > 0) {
            best = t;
            hitNormal = n;
            hit = true;
        }
    }
    if (hit) hitPoint = origin + dir * best;
    return hit;
}

} // namespace ca
