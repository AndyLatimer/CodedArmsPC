#pragma once
// First-person player body: movement, gravity, jumping and collision against the level.
// Runs at the fixed simulation rate; view angles are updated per rendered frame.
//
// All tuning constants are PROVISIONAL (not yet measured from the original game).
// They are grouped in MoveParams so they can be replaced by verified values.
#include "core/MathUtil.h"
#include "game/Level.h"

namespace ca {

struct MoveParams {
    float walkSpeed = 5.0f;       // m/s
    float aimSpeedScale = 0.5f;   // speed multiplier while aiming
    float groundAccel = 45.0f;    // m/s^2
    float airAccel = 12.0f;
    float gravity = 20.0f;
    float jumpVelocity = 7.0f;    // apex ~1.2 m: clears a 1 m block
    float radius = 0.4f;
    float height = 1.7f;
    float eyeHeight = 1.55f;
};

struct PlayerInput {
    Vec2 move;          // x strafe, y forward, |move| <= 1
    bool jump = false;  // edge
    bool aiming = false;
};

class Player {
public:
    void spawn(const Vec3& feet, float yaw);
    void tick(const PlayerInput& in, const Level& level, float dt);

    Vec3 position() const { return pos_; }          // feet
    Vec3 previousPosition() const { return prevPos_; }
    Vec3 velocity() const { return vel_; }
    bool grounded() const { return grounded_; }
    Vec3 eye(float alpha) const { return lerp(prevPos_, pos_, alpha) + Vec3{0, params.eyeHeight, 0}; }

    float yaw = 0, pitch = 0; // radians
    MoveParams params;

private:
    void moveAxis(int axis, float delta, const Level& level);
    Aabb bounds() const;

    Vec3 pos_, prevPos_, vel_;
    bool grounded_ = false;
};

} // namespace ca
