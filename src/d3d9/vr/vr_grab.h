#pragma once

#include <array>
#include <cstdint>

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief A ray of the contact probe, in game units
   */
  struct VrGrabRay {
    VrVector3 from;
    VrVector3 to;
  };

  /**
   * \brief Number of rays of the contact probe
   */
  constexpr uint32_t VrGrabProbeRayCount = 14;

  /**
   * \brief Rays that find what a hand touches
   *
   * Each ray starts on a sphere around the hand and ends at its centre:
   * along the six axes and towards the eight corners of a cube. A ray
   * meets a surface that crosses the sphere on its way, and the outside of
   * a body that holds the centre, which a ray starting inside the body
   * would not see.
   *
   * \param [in] centre Centre of the hand, in the game world
   * \param [in] radius How far the hand reaches, in game units
   */
  std::array<VrGrabRay, VrGrabProbeRayCount> vrGrabProbeRays(
    const VrVector3&            centre,
          float                 radius);

  /**
   * \brief Whether a released body is far enough from the player to
   *    collide with the player again
   *
   * Only the horizontal distance counts: the player's capsule stands
   * upright on the player's position.
   *
   * \param [in] body Centre of the body, in the game world
   * \param [in] player The player's position
   * \param [in] distance Horizontal distance needed, in game units
   */
  bool vrGrabClearOfPlayer(
    const VrVector3&            body,
    const VrVector3&            player,
          float                 distance);

  /**
   * \brief Constants of a hand's mouse spring (hkpMouseSpringAction)
   */
  struct VrGrabSpringConstants {
    /// Share of the held point's velocity the spring takes off each step
    float damping       = 0.0f;
    /// Share of the distance to the target the spring closes each step
    float elasticity    = 0.0f;
    /// Factor on the body's own velocities each step
    float objectDamping = 0.0f;
  };

  /**
   * \brief How far a mouse spring's point trails behind a target moving
   *    at a steady speed, in steps of the target's motion
   *
   * Each Havok step, hkpMouseSpringAction::applyAction scales the body's
   * velocities by the object damping, then changes the point's velocity
   * by its elasticity times the distance to the target per step, less its
   * damping times the point's velocity (findings/grab.md §10). At a steady
   * speed both cancel only with the point this many steps behind.
   */
  float vrGrabSpringLagSteps(const VrGrabSpringConstants& spring);

  /**
   * \brief Heaviest body that hands move as fast as the game's grab moves
   *    any body, per hand, in kilograms
   */
  constexpr float VrGrabLightMass = 10.0f;

  /**
   * \brief Strongest acceleration hands may give a held body
   *
   * The game's spring accelerates every body alike: its maximum force is
   * relative to the body's mass. A hand instead pushes with a fixed force,
   * enough for the game's acceleration up to VrGrabLightMass and shared by
   * the hands holding the same body, so heavier bodies follow more slowly.
   * It stays above gravity with a margin, so that a body the hand lifted
   * does not slip out of it.
   *
   * \param [in] mass The body's mass, in kilograms
   * \param [in] hands Hands holding the body
   * \param [in] fullAcceleration The game's maximum relative force
   * \param [in] gravity Strength of gravity, in the same unit
   */
  float vrGrabMaxAcceleration(
          float                 mass,
          uint32_t              hands,
          float                 fullAcceleration,
          float                 gravity);

  /**
   * \brief How far ahead of the hand a held point is aimed while the
   *    player moves
   *
   * The spring's target is set from the hand of the last frame drawn,
   * before the step that moves the player, and the spring trails behind a
   * moving target: without a lead, a held object falls behind the hand
   * while the player walks. The lead is the player's last move times the
   * frames the point trails, limited in length; a move longer than a walk
   * can make in a frame (a teleport, a load door) gives none.
   *
   * \param [in] playerStep The player's move in the last frame, in game units
   * \param [in] frames Frames of the player's motion the point trails
   */
  VrVector3 vrGrabLead(const VrVector3& playerStep, float frames);

  /**
   * \brief A rotation, as the directions of its three axes
   *
   * Havok's rotation matrices store the same three columns. Right-handed:
   * x × y = z.
   */
  struct VrGrabFrame {
    VrVector3 x = { 1.0f, 0.0f, 0.0f };
    VrVector3 y = { 0.0f, 1.0f, 0.0f };
    VrVector3 z = { 0.0f, 0.0f, 1.0f };
  };

  /**
   * \brief A hand's rotation: forward, up and right, the axes of
   *    vrPointToHand
   */
  VrGrabFrame vrGrabHandFrame(const VrGameCameraPose& hand);

  /**
   * \brief A rotation expressed in a parent's frame
   */
  VrGrabFrame vrGrabFrameToLocal(const VrGrabFrame& parent, const VrGrabFrame& frame);

  /**
   * \brief A rotation given in a parent's frame, back in the world
   */
  VrGrabFrame vrGrabFrameFromLocal(const VrGrabFrame& parent, const VrGrabFrame& local);

  /**
   * \brief The shortest rotation that turns one frame into another
   *
   * \returns Rotation axis times angle in radians, in the world
   */
  VrVector3 vrGrabRotationBetween(const VrGrabFrame& from, const VrGrabFrame& to);

  /**
   * \brief Change of a held body's angular velocity in a physics step
   *
   * Turns the body towards the hand's rotation as the mouse spring moves
   * its point towards the hand: the same damping and elasticity, applied
   * to the angle left instead of the distance, after the spring's object
   * damping. The change is limited to what the hands can give the body in
   * the step.
   *
   * \param [in] spring The spring's constants
   * \param [in] angularVelocity The body's angular velocity, radians per second
   * \param [in] error Rotation left to the hand's, axis times angle
   * \param [in] dt The step's time, in seconds
   * \param [in] maxAcceleration Strongest angular acceleration, radians
   *    per second squared
   */
  VrVector3 vrGrabTurnStep(
    const VrGrabSpringConstants& spring,
    const VrVector3&            angularVelocity,
    const VrVector3&            error,
          float                 dt,
          float                 maxAcceleration);

  /**
   * \brief A hand's rotation in the frame of what it holds, from the held
   *    body's rotation in the hand's frame
   */
  VrGrabFrame vrGrabFrameInverse(const VrGrabFrame& local);

  /**
   * \brief How far the drawn hand is moved from the controller to stay on
   *    what it holds
   */
  struct VrGrabCorrection {
    /// Turn of the hand's axes, axis times angle in radians, in the world
    VrVector3 rotation;
    /// Move of the hand, after the turn, in game units
    VrVector3 translation;
  };

  /**
   * \brief Where a hand is drawn so that it stays on what it holds
   *
   * The held point and, when the hand turns the body, the body's rotation
   * keep their place in the hand: the hand is turned by the rotation from
   * its own to the one it has on the body, then moved so that the held
   * point is where it is on the body. The correction is taken from the
   * physics update, a frame before the hand is drawn, so a little of it is
   * left out: small corrections are springs settling and the hand moving,
   * only what goes beyond them (a blocked or slow object) moves the hand.
   *
   * \param [in] hand The hand in the game world
   * \param [in] offset The held point in the hand's frame (vrPointToHand)
   * \param [in] point The held point on the body, in the game world
   * \param [in] onBody The hand's rotation on the body, or \c nullptr to
   *    keep the hand's own
   */
  VrGrabCorrection vrGrabHandCorrection(
    const VrGameCameraPose&     hand,
    const VrVector3&            offset,
    const VrVector3&            point,
    const VrGrabFrame*          onBody);

  /**
   * \brief A hand moved and turned by a correction
   */
  VrGameCameraPose vrGrabCorrectHand(const VrGameCameraPose& hand, const VrGrabCorrection& correction);

  /**
   * \brief Brings the drawn hand onto what it holds and back to the
   *    controller smoothly
   *
   * The correction follows the physics update's while the hand holds, and
   * blends over a short time when the hand starts or stops being kept on
   * an object, so that the hand does not jump.
   */
  class VrGrabHandGlue {

  public:

    /// Time to blend the hand onto or off an object, in seconds
    constexpr static float BlendTime = 0.12f;

    /**
     * \param [in] target The correction that keeps the hand on what it
     *    holds, or \c nullptr when it holds nothing
     * \param [in] dt Time since the previous frame, in seconds
     * \returns The correction to draw the hand with
     */
    VrGrabCorrection update(const VrGrabCorrection* target, float dt);

  private:

    VrGrabCorrection m_from;
    VrGrabCorrection m_last;
    float            m_blend   = 1.0f;
    bool             m_holding = false;

  };

  /**
   * \brief What a hand does with its grip in a frame
   */
  struct VrGrabHandInput {
    /// The hand in the game world, or \c nullptr when it is not tracked or
    /// the player is not in game
    const VrGameCameraPose* hand = nullptr;
    /// The hand's grip is held
    bool  grip = false;
    /// The hand may hold an object: not holding the weapon, nor busy with
    /// something else
    bool  free = false;
    /// Time since the previous frame, in seconds
    float dt   = 0.0f;
  };

  enum class VrGrabRequest {
    None,
    /// Look for something to hold at the hand
    Grab,
    /// Let go of what the hand holds
    Release,
  };

  /**
   * \brief Grabbing and letting go with one hand's grip
   *
   * A press of the grip while the hand is free grabs what the hand touches.
   * The hand keeps looking for a short time after the press, so that a
   * touch a moment late still counts, but a grip closed for longer grabs
   * nothing until it is pressed again. What the hand holds is let go when
   * the grip is released, when the hand stops being free and when it is
   * lost.
   *
   * The held point keeps its place in the hand's frame, so that the object
   * follows the hand's moves and turns. A point pulled from a distance
   * comes to the hand's centre at a speed set by its weight (see
   * vrGrabPullSpeed), along the line it had from the hand in the hand's
   * frame, and is then held there.
   */
  class VrGrabHand {

  public:

    /// How long a press keeps looking for something to hold, in seconds
    constexpr static float GrabWindow = 0.25f;

    /// How fast a pulled point of a light body comes to the hand, in game
    /// units per second
    constexpr static float PullSpeed = 4.0f * VrGameUnitsPerMetre;

    /**
     * \brief Decides what the hand does in a frame
     *
     * After a Release, the caller lets go and calls \ref drop. After a
     * Grab, it calls \ref hold or \ref pull if it found something. While
     * the hand holds, moves a pulled point closer to the hand.
     */
    VrGrabRequest update(const VrGrabHandInput& input);

    /**
     * \brief Starts holding a point
     *
     * \param [in] hand The hand in the game world
     * \param [in] point Point held, in the game world
     */
    void hold(const VrGameCameraPose& hand, const VrVector3& point);

    /**
     * \brief Starts pulling a point to the hand's centre
     *
     * \param [in] hand The hand in the game world
     * \param [in] point Point pulled, in the game world
     * \param [in] speed How fast it comes, in game units per second
     */
    void pull(const VrGameCameraPose& hand, const VrVector3& point, float speed);

    /**
     * \brief Stops holding
     *
     * Also when the object was lost. The grip has to be pressed again to
     * grab.
     */
    void drop();

    bool holding() const {
      return m_holding;
    }

    /**
     * \brief Whether the held point is still on its way to the hand
     */
    bool pulling() const {
      return m_holding && m_pulled;
    }

    /**
     * \brief Where the held point belongs for a hand pose
     */
    VrVector3 target(const VrGameCameraPose& hand) const;

    /**
     * \brief The held point in the hand's frame
     */
    const VrVector3& offset() const {
      return m_offset;
    }

  private:

    bool      m_holding  = false;
    bool      m_gripDown = false;
    bool      m_pulled   = false;
    float     m_window   = 0.0f;
    float     m_pullSpeed = 0.0f;
    VrVector3 m_offset;

    void advancePull(float dt);

  };

  /**
   * \brief How fast a body is pulled to the hand
   *
   * VrGrabHand::PullSpeed up to VrGrabLightMass, slower for heavier bodies
   * (with the square root of the mass ratio, down to a third or so).
   *
   * \param [in] mass The body's mass, in kilograms
   * \returns Game units per second
   */
  float vrGrabPullSpeed(float mass);

}
