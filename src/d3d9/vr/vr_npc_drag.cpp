#include <algorithm>
#include <cmath>

#include "vr_game_space.h"
#include "vr_npc_drag.h"

namespace dxvk {

  namespace {

    constexpr float Pi = 3.14159265f;

    // The hand's lead on the point held that starts the actor moving, and
    // the one that stops it
    constexpr float StartPull = 0.15f * VrGameUnitsPerMetre;
    constexpr float StopPull  = 0.04f * VrGameUnitsPerMetre;

    // Speed per unit of lead, per second: closes it in about 1/8 s
    constexpr float PullGain = 8.0f;

    // Share of the player's move the actor follows at once, the rest
    // through the hand's lead; a player slower than this does not count
    constexpr float PlayerShare    = 0.8f;
    constexpr float PlayerMinSpeed = 0.15f * VrGameUnitsPerMetre;

    // Time constant of the actor's speed changes, seconds
    constexpr float SpeedSmoothing = 0.1f;

    // Slower than this the legs stand still and the heading is left alone
    constexpr float MinMoveSpeed = 0.3f * VrGameUnitsPerMetre;

    // Faster than this the actor runs rather than walks
    constexpr float RunSpeed = 1.6f * VrGameUnitsPerMetre;

    // Radians per second of the turn to the move or the hands' twist
    constexpr float TurnSpeed = 2.0f;

    // A move further than this from the front turns the actor's back to it
    constexpr float FrontAngle = 2.0f;

    // The twist of both hands that starts a turn, and the one that stops it
    constexpr float StartTwist = 0.7f;
    constexpr float StopTwist  = 0.1f;

    constexpr float MinGripSpeed = 0.7f * VrGameUnitsPerMetre;
    constexpr float GripSpeedPerPoint = 0.7f * VrGameUnitsPerMetre;
    constexpr float MaxGripSpeed = 4.0f * VrGameUnitsPerMetre;

    float length(VrNpcGroundVector v) {
      return std::sqrt(v.x * v.x + v.y * v.y);
    }

    float wrapAngle(float angle) {
      angle = std::fmod(angle + Pi, 2.0f * Pi);

      if (angle < 0.0f)
        angle += 2.0f * Pi;

      return angle - Pi;
    }

    float turnTowards(float heading, float change, float maxStep) {
      return wrapAngle(heading + std::clamp(change, -maxStep, maxStep));
    }

  }


  VrNpcGroundVector vrNpcToWorld(VrNpcGroundVector local, float heading) {
    float s = std::sin(heading);
    float c = std::cos(heading);
    return { local.x * c + local.y * s, local.y * c - local.x * s };
  }


  VrNpcGroundVector vrNpcToActor(VrNpcGroundVector world, float heading) {
    float s = std::sin(heading);
    float c = std::cos(heading);
    return { world.x * c - world.y * s, world.x * s + world.y * c };
  }


  float vrNpcHeadingOf(VrNpcGroundVector direction) {
    return std::atan2(direction.x, direction.y);
  }


  float vrNpcTurnBetween(float from, float to) {
    return wrapAngle(to - from);
  }


  float vrNpcDragMaxSpeed(float margin) {
    return std::min(MinGripSpeed + GripSpeedPerPoint * std::max(margin, 0.0f), MaxGripSpeed);
  }


  void VrNpcDrag::update(const VrNpcDragInput& input) {
    float pull = length(input.pull);
    bool followsPlayer = length(input.player) >= PlayerMinSpeed;

    if (!m_translating && (pull >= StartPull || followsPlayer))
      m_translating = true;
    else if (m_translating && pull < StopPull && !followsPlayer)
      m_translating = false;

    VrNpcGroundVector wanted;

    if (m_translating) {
      wanted.x = input.pull.x * PullGain + input.player.x * PlayerShare;
      wanted.y = input.pull.y * PullGain + input.player.y * PlayerShare;

      float speed = length(wanted);

      if (speed > input.maxSpeed) {
        wanted.x *= input.maxSpeed / speed;
        wanted.y *= input.maxSpeed / speed;
      }
    }

    float blend = 1.0f - std::exp(-input.dt / SpeedSmoothing);
    m_velocity.x += (wanted.x - m_velocity.x) * blend;
    m_velocity.y += (wanted.y - m_velocity.y) * blend;

    float speed = length(m_velocity);
    bool moving = speed >= MinMoveSpeed;

    if (!m_translating && !moving)
      m_velocity = VrNpcGroundVector();

    if (!m_twisting && std::fabs(input.twist) >= StartTwist)
      m_twisting = true;
    else if (m_twisting && std::fabs(input.twist) < StopTwist)
      m_twisting = false;

    float maxStep = TurnSpeed * input.dt;
    m_heading = input.heading;
    m_turned  = false;

    if (m_twisting) {
      m_heading = turnTowards(input.heading, input.twist, maxStep);
      m_turned  = true;
    } else if (moving) {
      float change = wrapAngle(vrNpcHeadingOf(m_velocity) - input.heading);

      if (std::fabs(change) >= FrontAngle)
        change = wrapAngle(change + Pi);

      m_heading = turnTowards(input.heading, change, maxStep);
      m_turned  = true;
    }

    m_moveMode = 0;

    if (moving) {
      VrNpcGroundVector local = vrNpcToActor(m_velocity, m_heading);

      m_moveMode = std::fabs(local.y) >= std::fabs(local.x)
        ? (local.y > 0.0f ? VrNpcMoveForward : VrNpcMoveBackward)
        : (local.x > 0.0f ? VrNpcMoveRight : VrNpcMoveLeft);
      m_moveMode |= speed >= RunSpeed ? VrNpcMoveRunning : VrNpcMoveWalking;
    }
  }

}
