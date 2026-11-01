#include <algorithm>
#include <cmath>

#include "vr_game_space.h"
#include "vr_math.h"
#include "vr_npc_drag.h"
#include "vr_npc_manhandle.h"

namespace dxvk {

  namespace {

    constexpr float YankSpeed  = 4.0f * VrGameUnitsPerMetre;
    constexpr float ShoveSpeed = 2.5f * VrGameUnitsPerMetre;

    // Share of the hand's velocity the actor's limbs take
    constexpr float YankHorizontal = 0.45f;
    constexpr float YankVertical   = 0.35f;

    // Far enough for the limbs' directions from it to look parallel
    constexpr float YankSourceDistance = 100.0f * VrGameUnitsPerMetre;

    constexpr float SmallMaxHealth = 6.0f;

    constexpr float GripCostCalm     = 3.0f;
    constexpr float GripCostFighting = 10.0f;
    constexpr float GripCostHealthShare = 0.8f;

    // Share of the slow-down an actor's Health decides, and of the speed
    // the player's action points decide
    constexpr float LimitHealthShare = 0.5f;
    constexpr float LimitActionShare = 0.5f;
    constexpr float TeammateSpeedFactor = 2.5f;
    constexpr float MaxSpeedLimit = 6.0f * VrGameUnitsPerMetre;

    constexpr float BrakeFadeTime = 5.0f;

    constexpr float AngerComplain = 0.5f;
    constexpr float AngerAssault  = 9.0f;
    constexpr float AngerMaximum  = 20.0f;
    constexpr float AngerForgetAfter = 10.0f;

  }


  void VrHandMotion::update(const VrVector3& step, float dt) {
    if (!(dt > 0.0f))
      return;

    m_velocities[m_next] = step * (1.0f / dt);
    m_next = (m_next + 1) % Frames;
  }


  void VrHandMotion::reset() {
    m_velocities = { };
  }


  const VrVector3& VrHandMotion::recent(uint32_t age) const {
    return m_velocities[(m_next + Frames - 1 - age) % Frames];
  }


  VrVector3 VrHandMotion::peak() const {
    uint32_t fastest = 0;

    for (uint32_t i = 1; i < Frames; i++) {
      if (vrLength(recent(i)) > vrLength(recent(fastest)))
        fastest = i;
    }

    if (fastest == 0 || fastest == Frames - 1)
      return recent(fastest);

    return (recent(fastest - 1) + recent(fastest) + recent(fastest + 1)) * (1.0f / 3.0f);
  }


  VrVector3 VrHandMotion::average() const {
    VrVector3 sum;

    for (const VrVector3& velocity : m_velocities)
      sum = sum + velocity;

    return sum * (1.0f / float(Frames));
  }


  bool vrNpcYankIsFast(const VrVector3& velocity) {
    return vrLength(velocity) > YankSpeed;
  }


  bool vrNpcShoveIsFast(const VrVector3& velocity) {
    return vrLength(velocity) > ShoveSpeed;
  }


  VrNpcKnockPush vrNpcYankPush(const VrVector3& velocity, const VrVector3& centre, float havokScale) {
    VrVector3 given = { velocity.x * YankHorizontal, velocity.y * YankHorizontal, velocity.z * YankVertical };
    float speed = vrLength(given);

    VrNpcKnockPush push;
    push.source = centre;

    if (speed > 0.0f) {
      push.source = centre - given * (YankSourceDistance / speed);
      push.force  = speed * havokScale;
    }

    return push;
  }


  bool vrNpcKnockedDownByGrip(float maxHealth) {
    return maxHealth > 0.0f && maxHealth < SmallMaxHealth;
  }


  float vrNpcGripCost(bool fighting, float health) {
    float cost = fighting ? GripCostFighting : GripCostCalm;
    return cost * (std::clamp(health, 0.0f, 1.0f) * GripCostHealthShare + (1.0f - GripCostHealthShare));
  }


  float vrNpcGripSpeedLimit(float margin, float health, float actionPoints, bool teammate) {
    float resistance = std::clamp(health, 0.0f, 1.0f) * LimitHealthShare + (1.0f - LimitHealthShare);
    float breath     = std::clamp(actionPoints, 0.0f, 1.0f) * LimitActionShare + (1.0f - LimitActionShare);
    float limit      = vrNpcDragMaxSpeed(margin) / resistance * breath;

    return std::min(teammate ? limit * TeammateSpeedFactor : limit, MaxSpeedLimit);
  }


  void VrNpcPlayerBrake::update(bool holding, float limit, float dt) {
    if (holding) {
      m_limit    = limit;
      m_strength = 1.0f;
    } else {
      m_strength = std::max(m_strength - dt / BrakeFadeTime, 0.0f);
    }
  }


  float vrNpcPlayerMoveFactor(float speed, float limit, float strength) {
    if (!(strength > 0.0f) || speed <= limit || !(speed > 0.0f))
      return 1.0f;

    return 1.0f - (1.0f - limit / speed) * strength;
  }


  void VrNpcAnger::add(float anger) {
    m_added += anger;
  }


  VrNpcAngerAction VrNpcAnger::update(bool held, bool fighting, float dt) {
    bool handled = held || m_added > 0.0f;

    m_anger += m_added + (held ? dt : 0.0f);
    m_added  = 0.0f;

    if (handled) {
      m_leftAlone = 0.0f;
    } else {
      m_leftAlone += dt;

      if (m_leftAlone >= AngerForgetAfter)
        m_anger -= dt;
    }

    m_anger = std::clamp(m_anger, 0.0f, AngerMaximum);

    // Already fighting there is nothing more to it; when the fight is over
    // the player starts afresh
    if (fighting) {
      m_fought = true;
      return VrNpcAngerAction::None;
    }

    if (m_fought) {
      m_fought    = false;
      m_assaulted = false;
      m_anger     = 0.0f;
      return VrNpcAngerAction::None;
    }

    if (m_anger <= AngerAssault)
      m_assaulted = false;
    else if (!m_assaulted) {
      m_assaulted = true;
      return VrNpcAngerAction::Assault;
    }

    return held && m_anger > AngerComplain ? VrNpcAngerAction::Complain : VrNpcAngerAction::None;
  }

}
