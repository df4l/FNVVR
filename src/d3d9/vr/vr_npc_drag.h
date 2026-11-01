#pragma once

#include <cstdint>

namespace dxvk {

  /// ActorMover::MovementFlags that a dragged actor's animation is picked from
  constexpr uint32_t VrNpcMoveForward  = 0x001;
  constexpr uint32_t VrNpcMoveBackward = 0x002;
  constexpr uint32_t VrNpcMoveLeft     = 0x004;
  constexpr uint32_t VrNpcMoveRight    = 0x008;
  constexpr uint32_t VrNpcMoveWalking  = 0x100;
  constexpr uint32_t VrNpcMoveRunning  = 0x200;

  /// Flags a drag leaves as the game set them: sneaking, swimming, jumping,
  /// flying, falling and sliding
  constexpr uint32_t VrNpcMoveKeptFlags = 0xFC00;

  /**
   * \brief A vector of the ground plane, game units
   *
   * X east, Y north. An actor's heading is clockwise from north.
   */
  struct VrNpcGroundVector {
    float x = 0.0f;
    float y = 0.0f;
  };

  /**
   * \brief A vector of the actor's frame in the world
   *
   * \param [in] local X to the actor's right, Y ahead of it
   * \param [in] heading The actor's heading, radians
   */
  VrNpcGroundVector vrNpcToWorld(VrNpcGroundVector local, float heading);

  /**
   * \brief A vector of the world in the actor's frame
   */
  VrNpcGroundVector vrNpcToActor(VrNpcGroundVector world, float heading);

  /**
   * \brief Heading of a direction of the ground plane, radians
   */
  float vrNpcHeadingOf(VrNpcGroundVector direction);

  /**
   * \brief Shortest turn from one heading to another, radians
   */
  float vrNpcTurnBetween(float from, float to);

  /**
   * \brief Fastest a grip drags an actor, game units per second
   *
   * \param [in] margin How far the grip's Strength exceeds the actor's
   */
  float vrNpcDragMaxSpeed(float margin);

  /**
   * \brief What moves a dragged actor in one update
   */
  struct VrNpcDragInput {
    /// From where the point held belongs on the actor to the hand, mean of
    /// the hands, game units
    VrNpcGroundVector pull;
    /// The player's velocity, game units per second
    VrNpcGroundVector player;
    /// Turn that brings the actor's grip on both hands in line with them,
    /// radians, clockwise; 0 with one hand
    float twist = 0.0f;
    float heading  = 0.0f;
    float maxSpeed = 0.0f;
    float dt       = 0.0f;
  };

  /**
   * \brief Walks an actor where the hands holding it take it
   *
   * The actor starts to move once a hand has taken the point held far enough
   * from its place on the actor or the player moves, and stops once the hand
   * is back near it (a dead band, so that a still hand gives a still actor),
   * follows the player's own move at once, and turns its front or its back
   * towards the move, whichever is nearer, so that a shake does not spin it.
   * Both hands also twist it. The game's walk or run animation is picked from
   * the move mode. The behaviour follows the grabbed actor movement of the
   * Skyrim VR mod PLANCK (findings/grab.md §19.9).
   */
  class VrNpcDrag {

  public:

    void update(const VrNpcDragInput& input);

    /// Velocity to give the actor, game units per second
    VrNpcGroundVector velocity() const { return m_velocity; }

    /// Heading to give the actor, radians
    float heading() const { return m_heading; }

    /// Whether this update chose the heading rather than left it as given
    bool turned() const { return m_turned; }

    /// Direction and pace of ActorMover::MovementFlags, 0 when standing
    uint32_t moveMode() const { return m_moveMode; }

    bool translating() const { return m_translating; }

  private:

    VrNpcGroundVector m_velocity;
    float    m_heading     = 0.0f;
    uint32_t m_moveMode    = 0;
    bool     m_turned      = false;
    bool     m_translating = false;
    bool     m_twisting    = false;

  };

}
