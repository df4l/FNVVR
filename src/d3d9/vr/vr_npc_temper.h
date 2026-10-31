#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief What an actor grabbed by the player does
   */
  enum class VrNpcGrabResponse : uint32_t {
    /// Nothing: it cannot speak now, or another actor commented a moment ago
    None,
    /// Says the line the game plays when the player aims a weapon at it
    Complain,
    /// Goes into combat with the player
    Attack,
  };

  /**
   * \brief What decides the response of an actor grabbed by the player
   *
   * Read from the game when the grab starts, see findings/grab.md §19.3.
   */
  struct VrNpcTemper {
    /// Current Aggression actor value (0 Unaggressive to 3 Frenzied)
    float aggression = 0.0f;
    /// Current Confidence actor value (0 Cowardly to 4 Foolhardy)
    float confidence = 0.0f;
    /// A companion of the player
    bool  teammate   = false;
    /// Already in combat
    bool  inCombat   = false;
    /// Passes the game's own checks for commenting the player's action
    /// (awake, able to speak, not in combat, reacts to the player's actions)
    bool  canComment = false;
    /// Seconds left on the game's timer shared by every comment on the
    /// player's actions
    float commentTimer = 0.0f;
  };

  /// ACTOR_AGGRESSION_AGGRESSIVE
  constexpr float VrNpcAggressive = 1.0f;
  /// ACTOR_CONFIDENCE_FOOLHARDY
  constexpr float VrNpcFoolhardy  = 4.0f;

  /**
   * \brief Response of an actor grabbed by the player
   *
   * A foolhardy actor that is at least aggressive attacks at once (user,
   * 2026-10-31), unless it is a companion or already fighting. Others say
   * the line the game plays when the player aims at them, under the
   * game's own conditions and timer.
   */
  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper);

}
