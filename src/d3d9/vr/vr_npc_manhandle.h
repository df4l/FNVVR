#pragma once

#include <array>
#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /// Action points of what the hands do to an actor. The numbers follow the
  /// stamina costs of the Skyrim VR mod PLANCK, action points standing in for
  /// stamina (findings/grab.md §19.10).
  constexpr float VrNpcYankCost         = 30.0f;
  constexpr float VrNpcYankFightingCost = 50.0f;
  constexpr float VrNpcShoveCost        = 40.0f;

  /// Seconds between the two hands' release for a yank of both
  constexpr float VrNpcYankWindow = 0.25f;

  /// Seconds before another shove
  constexpr float VrNpcShoveCooldown = 2.0f;

  /// What a yank, a shove and being knocked down add to an actor's anger,
  /// as seconds of being held
  constexpr float VrNpcKnockDownAnger = 10.0f;
  constexpr float VrNpcShoveAnger     = 2.5f;

  /**
   * \brief How fast a hand has moved lately, apart from the player's own move
   */
  class VrHandMotion {

  public:

    /**
     * \brief Adds a frame
     *
     * \param [in] step The hand's move in the frame, without the player's
     * \param [in] dt The frame's time, seconds
     */
    void update(const VrVector3& step, float dt);

    /// Forgets the hand's move: it is not tracked
    void reset();

    /// Velocity at the fastest of the last frames, smoothed around that frame
    VrVector3 peak() const;

    /// Mean velocity of the last frames
    VrVector3 average() const;

  private:

    constexpr static uint32_t Frames = 5;

    std::array<VrVector3, Frames> m_velocities = { };
    uint32_t m_next = 0;

    const VrVector3& recent(uint32_t age) const;

  };

  /**
   * \brief Whether a hand letting go at this velocity yanks the actor
   *
   * \param [in] velocity Game units per second
   */
  bool vrNpcYankIsFast(const VrVector3& velocity);

  /**
   * \brief Whether a free hand at this velocity shoves the actor it touches
   *
   * \param [in] velocity Game units per second
   */
  bool vrNpcShoveIsFast(const VrVector3& velocity);

  /**
   * \brief The game's knock down that throws an actor along a hand's move
   */
  struct VrNpcKnockPush {
    /// Where the blast comes from, game units: far enough behind the actor
    /// that all its limbs go the same way
    VrVector3 source;
    /// Speed added to its limbs, Havok units per second
    float     force = 0.0f;
  };

  /**
   * \param [in] velocity The hand's, game units per second
   * \param [in] centre About the actor's middle, game units
   * \param [in] havokScale Havok units per game unit
   */
  VrNpcKnockPush vrNpcYankPush(const VrVector3& velocity, const VrVector3& centre, float havokScale);

  /**
   * \brief Whether an actor is so small that a hand knocks it down at once
   *
   * \param [in] maxHealth Its base Health
   */
  bool vrNpcKnockedDownByGrip(float maxHealth);

  /**
   * \brief Action points per second of holding an actor
   *
   * \param [in] fighting The actor is in combat
   * \param [in] health Its share of its Health, 0 to 1
   */
  float vrNpcGripCost(bool fighting, float health);

  /**
   * \brief Fastest a grip moves an actor and lets the player walk, game units
   *    per second
   *
   * A weaker actor comes along faster, a companion does not resist much, and
   * a player out of breath goes more slowly.
   *
   * \param [in] margin How far the grip's Strength exceeds the actor's
   * \param [in] health The actor's share of its Health, 0 to 1
   * \param [in] actionPoints The player's share of action points, 0 to 1
   * \param [in] teammate The actor is a companion
   */
  float vrNpcGripSpeedLimit(float margin, float health, float actionPoints, bool teammate);

  /**
   * \brief Slows the player down who holds an actor
   *
   * The player cannot walk away faster than the actor comes along, and takes
   * a few seconds to be at full speed again after letting go.
   */
  class VrNpcPlayerBrake {

  public:

    /**
     * \param [in] holding A hand keeps an actor
     * \param [in] limit vrNpcGripSpeedLimit of the actor, while holding
     * \param [in] dt Seconds
     */
    void update(bool holding, float limit, float dt);

    float limit() const { return m_limit; }

    /// 1 while holding, down to 0 after letting go
    float strength() const { return m_strength; }

  private:

    float m_limit    = 0.0f;
    float m_strength = 0.0f;

  };

  /**
   * \brief Factor on the player's move
   *
   * \param [in] speed The player's speed, game units per second
   * \param [in] limit VrNpcPlayerBrake::limit
   * \param [in] strength VrNpcPlayerBrake::strength
   */
  float vrNpcPlayerMoveFactor(float speed, float limit, float strength);

  /**
   * \brief What an actor's anger at being handled makes it do now
   */
  enum class VrNpcAngerAction : uint32_t {
    None,
    /// Complains, as often as the game lets actors comment
    Complain,
    /// Calls it an assault, once
    Assault,
  };

  /**
   * \brief An actor's growing anger at being handled by the player
   *
   * Time held counts up, and things done to it add to the count. It complains
   * above half a second, and takes it for an assault above nine; the count
   * goes down again after ten seconds of being left alone. The numbers are
   * those of the Skyrim VR mod PLANCK.
   */
  class VrNpcAnger {

  public:

    /**
     * \param [in] held A hand holds the actor
     * \param [in] fighting The actor is already in combat
     * \param [in] dt Seconds
     */
    VrNpcAngerAction update(bool held, bool fighting, float dt);

    /// Something was done to the actor, seconds of being held it is worth
    void add(float anger);

    float anger() const { return m_anger; }

  private:

    float m_anger     = 0.0f;
    float m_added     = 0.0f;
    float m_leftAlone = 0.0f;
    bool  m_fought    = false;
    bool  m_assaulted = false;

  };

}
