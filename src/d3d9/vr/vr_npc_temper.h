#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief What an actor grabbed by the player does
   */
  enum class VrNpcGrabResponse : uint32_t {
    /// Nothing: it cannot speak now, or is still saying its last complaint
    None,
    /// Says the line the game plays when the player fires near it
    Complain,
    /// Goes into combat with the player
    Attack,
  };

  /**
   * \brief What decides the response of an actor grabbed by the player
   *
   * Read from the game when the grab starts, see findings/grab.md §19.3
   * and §19.8.
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
    /// Has a recorded line for a shot fired near it
    bool  voicedLine = false;
    /// The player is idolized by one of its factions
    bool  idolized   = false;
    /// Passes the game's own checks for commenting the player's action
    /// (awake, able to speak, not in combat, reacts to the player's actions)
    bool  canComment = false;
    /// Still saying the line of its last complaint
    bool  complaining = false;
  };

  /// ACTOR_AGGRESSION_AGGRESSIVE
  constexpr float VrNpcAggressive = 1.0f;
  /// ACTOR_CONFIDENCE_FOOLHARDY
  constexpr float VrNpcFoolhardy  = 4.0f;

  /**
   * \brief Whether the actor's temper alone can make it attack
   *
   * Foolhardy and at least aggressive, unless it is a companion or already
   * fighting. Tells whether voicedLine and idolized matter.
   */
  bool vrNpcHotTempered(const VrNpcTemper& temper);

  /**
   * \brief Response of an actor grabbed by the player
   *
   * A hot-tempered actor with nothing recorded to say attacks at once,
   * unless the player is idolized by one of its factions (user,
   * 2026-11-01). Others say at once the line the game plays when the player
   * fires near them, if the game's own conditions let them comment: a grab is
   * aimed at them, the timers that space out remarks do not apply (user,
   * 2026-11-01).
   */
  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper);

  /// Strength both hands on an actor add to the player's (user, 2026-11-01)
  constexpr float VrNpcTwoHandsStrength = 1.0f;

  /**
   * \brief How far the player's grip exceeds the actor's Strength
   *
   * Above zero, the hands keep the actor from walking away and move it where
   * they want it (user, 2026-11-01).
   *
   * \param [in] playerStrength Current Strength of the player
   * \param [in] actorStrength Current Strength of the actor
   * \param [in] hands How many hands hold the actor
   */
  float vrNpcGripMargin(float playerStrength, float actorStrength, uint32_t hands);

}
