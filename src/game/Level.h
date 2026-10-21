#pragma once
// Tile-grid level and its collision queries.
//
// PROVISIONAL: Coded Arms generates its levels at random each time they are entered
// (verified: see TECHNICAL_NOTES.md), but its generator, tile set and map format have
// NOT been reverse engineered yet. This generator is our own rooms-and-corridors
// stand-in so the movement/collision code can be built and tested. It will be replaced
// by a reimplementation of the original algorithm once its data is analysed.
#include "core/MathUtil.h"
#include "game/Collision.h"
#include <functional>
#include <vector>

namespace ca {

enum class Cell : unsigned char { Wall = 0, Floor = 1, Block = 2 };

struct Aabb {
    Vec3 min, max;
};

class Level {
public:
    static constexpr float kCellSize = 4.0f;    // metres per tile (provisional)
    static constexpr float kWallHeight = 4.0f;
    static constexpr float kBlockHeight = 1.0f; // low cover block, jumpable

    void generate(unsigned seed, int width = 32, int depth = 32);

    int width() const { return w_; }
    int depth() const { return d_; }
    unsigned seed() const { return seed_; }
    Cell at(int x, int z) const;
    void set(int x, int z, Cell c);

    Vec3 spawnPoint() const { return spawn_; }
    Vec3 goalPoint() const { return goal_; }

    // Emits every visible surface (floor, wall sides, block tops/sides) as triangles.
    void buildCollision(CollisionWorld& world) const;
    void forEachQuad(const std::function<void(Vec3, Vec3, Vec3, Vec3)>& emit) const;

    // Solid boxes overlapping the given region (walls and blocks).
    void solidsInRegion(const Aabb& region, std::vector<Aabb>& out) const;
    // First solid hit along a ray, used by the hitscan test weapon.
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3& hitPoint, Vec3& hitNormal) const;

private:
    int w_ = 0, d_ = 0;
    unsigned seed_ = 0;
    std::vector<Cell> cells_;
    Vec3 spawn_, goal_;
};

} // namespace ca
