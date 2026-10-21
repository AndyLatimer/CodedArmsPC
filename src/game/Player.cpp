#include "game/Player.h"
#include <vector>

namespace ca {

void Player::spawn(const Vec3& feet, float yaw_) {
    pos_ = prevPos_ = feet;
    vel_ = {};
    yaw = yaw_;
    pitch = 0;
    grounded_ = true;
}

Aabb Player::bounds() const {
    return {{pos_.x - params.radius, pos_.y, pos_.z - params.radius},
            {pos_.x + params.radius, pos_.y + params.height, pos_.z + params.radius}};
}

void Player::tick(const PlayerInput& in, const Level& level, float dt) {
    prevPos_ = pos_;

    Vec3 fwd{std::sin(yaw), 0, -std::cos(yaw)};
    Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
    float speed = params.walkSpeed * (in.aiming ? params.aimSpeedScale : 1.0f);
    Vec3 wish = (fwd * in.move.y + right * in.move.x) * speed;

    // Accelerate horizontal velocity toward the wish velocity.
    float accel = (grounded_ ? params.groundAccel : params.airAccel) * dt;
    Vec3 dv{wish.x - vel_.x, 0, wish.z - vel_.z};
    float dl = length(dv);
    if (dl > accel) dv = dv * (accel / dl);
    vel_.x += dv.x;
    vel_.z += dv.z;

    if (grounded_ && in.jump) {
        vel_.y = params.jumpVelocity;
        grounded_ = false;
    }
    vel_.y -= params.gravity * dt;

    moveAxis(0, vel_.x * dt, level);
    moveAxis(2, vel_.z * dt, level);
    grounded_ = false;
    moveAxis(1, vel_.y * dt, level);
}

// Axis-separated sweep: move, then push out of any overlapping solid along that axis.
void Player::moveAxis(int axis, float delta, const Level& level) {
    if (delta == 0.0f && axis != 1) return;
    (&pos_.x)[axis] += delta;
    Aabb b = bounds();
    std::vector<Aabb> solids;
    Aabb query{{b.min.x - 0.1f, b.min.y - 0.1f, b.min.z - 0.1f}, {b.max.x + 0.1f, b.max.y + 0.1f, b.max.z + 0.1f}};
    level.solidsInRegion(query, solids);
    const float eps = 1e-4f;
    for (const Aabb& s : solids) {
        b = bounds();
        bool overlap = b.min.x < s.max.x && b.max.x > s.min.x && b.min.y < s.max.y && b.max.y > s.min.y &&
                       b.min.z < s.max.z && b.max.z > s.min.z;
        if (!overlap) continue;
        float& p = (&pos_.x)[axis];
        if (axis == 1) {
            if (delta <= 0) { p = s.max.y; grounded_ = true; }
            else p = s.min.y - params.height - eps;
            (&vel_.x)[1] = 0;
        } else {
            float half = params.radius;
            if (delta > 0) p = (&s.min.x)[axis] - half - eps;
            else p = (&s.max.x)[axis] + half + eps;
            (&vel_.x)[axis] = 0;
        }
    }
}

} // namespace ca
