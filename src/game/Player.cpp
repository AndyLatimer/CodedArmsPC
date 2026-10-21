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

void Player::tick(const PlayerInput& in, const CollisionWorld& world, float dt) {
    prevPos_ = pos_;
    const MoveParams& P = params;

    Vec3 fwd{std::sin(yaw), 0, -std::cos(yaw)};
    Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
    float speed = P.walkSpeed * (in.aiming ? P.aimSpeedScale : 1.0f);
    Vec3 wish = (fwd * in.move.y + right * in.move.x) * speed;

    // Accelerate horizontal velocity toward the wish velocity.
    float accel = (grounded_ ? P.groundAccel : P.airAccel) * dt;
    Vec3 dv{wish.x - vel_.x, 0, wish.z - vel_.z};
    float dl = length(dv);
    if (dl > accel) dv = dv * (accel / dl);
    vel_.x += dv.x;
    vel_.z += dv.z;

    if (grounded_ && in.jump) {
        vel_.y = P.jumpVelocity;
        grounded_ = false;
    }
    vel_.y -= P.gravity * dt;

    // Horizontal: move, then push the body spheres (knee above step height, and head)
    // out of geometry, keeping only the horizontal part of the correction.
    pos_.x += vel_.x * dt;
    pos_.z += vel_.z * dt;
    const float sphereY[2] = {P.stepHeight + P.radius, P.height - P.radius};
    for (int iter = 0; iter < 3; ++iter) {
        bool any = false;
        for (float sy : sphereY) {
            Vec3 c = pos_ + Vec3{0, sy, 0};
            Vec3 before = c;
            Vec3 n;
            if (world.pushOutSphere(c, P.radius, &n)) {
                Vec3 corr{c.x - before.x, 0, c.z - before.z};
                if (length(corr) > 1e-6f) {
                    pos_.x += corr.x;
                    pos_.z += corr.z;
                    Vec3 hn = normalize(corr);
                    float into = vel_.x * hn.x + vel_.z * hn.z;
                    if (into < 0) { vel_.x -= hn.x * into; vel_.z -= hn.z * into; }
                    any = true;
                }
            }
        }
        if (!any) break;
    }

    // Vertical: ceiling, then ground probe from step height (snaps down small ledges
    // while grounded so walking down stairs does not become falling).
    float dy = vel_.y * dt;
    if (dy > 0) {
        float t;
        Vec3 n;
        Vec3 head = pos_ + Vec3{0, P.height, 0};
        if (world.raycast(head, {0, 1, 0}, dy + 0.01f, t, n)) {
            dy = std::max(0.0f, t - 0.01f);
            vel_.y = 0;
        }
    }
    bool wasGrounded = grounded_;
    grounded_ = false;
    if (dy <= 0) {
        float probe = P.stepHeight + (-dy) + (wasGrounded ? P.stepHeight : 0.0f);
        float t;
        Vec3 n;
        Vec3 from = pos_ + Vec3{0, P.stepHeight, 0};
        if (world.raycast(from, {0, -1, 0}, probe, t, n) && n.y > 0.5f) {
            float groundY = from.y - t;
            if (pos_.y + dy <= groundY + 1e-4f || wasGrounded) {
                pos_.y = groundY;
                vel_.y = 0;
                grounded_ = true;
                return;
            }
        }
    }
    pos_.y += dy;
}

} // namespace ca
