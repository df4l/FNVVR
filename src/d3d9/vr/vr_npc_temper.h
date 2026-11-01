#pragma once

#include <cstdint>
#include <unordered_map>

namespace dxvk {

  /**
   * \brief What an actor grabbed by the player does
   */
  enum class VrNpcGrabResponse : uint32_t {
    /// Nothing: it cannot speak now, or complained a moment ago
    None,
    /// Says a line the game plays when the player aims or fires near it
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
    /// Has a recorded line for a weapon aimed at it or a shot fired near it
    bool  voicedLine = false;
    /// The player is idolized by one of its factions
    bool  idolized   = false;
    /// Passes the game's own checks for commenting the player's action
    /// (awake, able to speak, not in combat, reacts to the player's actions)
    bool  canComment = false;
    /// Still saying the line of its last complaint
    bool  complaining = false;
    /// Complained too recently to complain again
    bool  coolingDown = false;
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
   * 2026-11-01). Others say at once a line the game plays when the player
   * aims or fires near them, if the game's own conditions let them comment
   * and they have not complained a moment ago (user, 2026-11-01): the timers
   * of the game that space out remarks are shared by all actors and do not
   * apply to a grab.
   */
  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper);

  /**
   * \brief Spaces out the complaints of each actor
   *
   * Per actor: touching one again stays silent for a while, another one
   * still answers at once (findings/grab.md §19.15).
   */
  class VrNpcComplaintCooldown {

  public:

    /**
     * \brief Whether an actor complained too recently to complain again
     *
     * \param [in] actor The actor's form ID
     * \param [in] now Current time, in seconds
     */
    bool coolingDown(uint32_t actor, double now) const;

    /**
     * \brief An actor just complained
     *
     * \param [in] actor The actor's form ID
     * \param [in] now Current time, in seconds
     * \param [in] seconds How long it stays silent
     */
    void start(uint32_t actor, double now, double seconds);

  private:

    std::unordered_map<uint32_t, double> m_silentUntil;

  };

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
