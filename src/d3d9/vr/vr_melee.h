#pragma once

#include <cstdint>
#include <vector>

#include "vr_game_space.h"
#include "vr_power_attack.h"

namespace dxvk {

  /**
   * \brief Segment between two points in the game world
   */
  struct VrSegment {
    VrVector3 a;
    VrVector3 b;
  };

  /**
   * \brief Blade of a melee weapon in the frame of the hand holding it
   *
   * Each point is given by its distances along the hand's forward, up and
   * right axes from the hand's position, in game units.
   */
  struct VrBlade {
    VrVector3 base;
    VrVector3 tip;
  };

  /**
   * \brief Speed that makes a swing hit, see findings/melee.md §9
   *
   * The speed never makes a power attack (findings/melee.md §9.7); a swing made
   * while a power attack is charged with the trigger does (§12).
   */
  struct VrSwingConfig {
    /// Blade tip speed above which a contact hits, in metres per second
    float hitSpeed = 2.0f;
    /// Action points of a power attack, see VrPowerAttackRules
    VrPowerAttackCost powerCost;
  };

  /**
   * \brief Turns a point from a hand's frame to the game world
   *
   * Works for the frame of any pose, such as the player's body.
   *
   * \param [in] hand Hand pose in the game world
   * \param [in] local Distances along the hand's forward, up and right axes
   */
  VrVector3 vrPointFromHand(const VrGameCameraPose& hand, const VrVector3& local);

  /**
   * \brief Turns a point from the game world to a hand's frame
   */
  VrVector3 vrPointToHand(const VrGameCameraPose& hand, const VrVector3& world);

  /**
   * \brief Measures the blade of the weapon held in a hand
   *
   * The blade goes from the hand towards the centre of the weapon's bounding
   * sphere, up to the far side of the sphere. This needs no table of weapons:
   * the bound of a sword is centred on its blade, the bound of an axe or a
   * hammer leans towards its head.
   *
   * \param [in] hand Pose of the hand holding the weapon
   * \param [in] centre Centre of the weapon's bounding sphere
   * \param [in] radius Radius of the weapon's bounding sphere
   * \param [out] blade The blade in the hand's frame
   * \returns \c false if the bound is too small or too large for a weapon
   */
  bool vrMeasureBlade(
    const VrGameCameraPose&     hand,
    const VrVector3&            centre,
          float                 radius,
          VrBlade&              blade);

  /**
   * \brief Places a blade held in a hand in the game world
   */
  VrSegment vrBladeInWorld(const VrGameCameraPose& hand, const VrBlade& blade);

  /**
   * \brief The part of a fist that strikes, as a blade in the hand's frame
   *
   * From the grip's centre forwards past the knuckles; the capsules around
   * the bones it is tested against give it its thickness.
   */
  VrBlade vrFistBlade();

  /**
   * \brief Whether a hand's fingers make a fist
   *
   * The index, middle, ring and little fingers are closed; the thumb may
   * rest anywhere (findings/melee.md §13).
   *
   * \param [in] curls Finger curls, 0 straight to 1 closed
   */
  bool vrIsFist(const VrFingerCurls& curls);

  /**
   * \brief Closest points of two segments
   */
  struct VrSegmentClosest {
    /// Distance between the closest points
    float distance = 0.0f;
    /// Fraction along the first segment of its closest point
    float s = 0.0f;
    /// Fraction along the second segment of its closest point
    float t = 0.0f;
  };

  /**
   * \brief Finds the closest points of two segments
   */
  VrSegmentClosest vrSegmentClosest(const VrSegment& s, const VrSegment& t);

  /**
   * \brief Shortest distance between two segments
   */
  float vrSegmentDistance(const VrSegment& s, const VrSegment& t);

  /**
   * \brief Where a moving segment first comes near a fixed one
   */
  struct VrSweepContact {
    /// Whether the moving segment comes within the distance at all
    bool      touches = false;
    /// Fraction of the sweep where it does first, 0 at \c from, 1 at \c to
    float     time = 0.0f;
    /// Distance between the segments there
    float     distance = 0.0f;
    /// Point of the fixed segment closest to the moving one there
    VrVector3 fixedPoint;
    /// Point of the moving segment closest to the fixed one there
    VrVector3 movingPoint;
    /// How far that point of the moving segment goes over the whole sweep
    VrVector3 motion;
  };

  /**
   * \brief Finds where a moving segment first comes within a distance of a fixed one
   *
   * The moving segment's ends go in straight lines from \c from to \c to, and
   * it is compared with \c fixed at \c steps evenly spaced positions after
   * \c from, \c to included, so that a fast swing cannot pass through a
   * thin bone between two frames. The first position within \c radius is
   * the contact.
   */
  VrSweepContact vrSweptSegmentContact(
    const VrSegment&            from,
    const VrSegment&            to,
    const VrSegment&            fixed,
          uint32_t              steps,
          float                 radius);

  /**
   * \brief Where and in which direction a blade hits a limb
   */
  struct VrBladeHit {
    /// Point on the surface of the limb's capsule, towards the blade
    VrVector3 location;
    /// Unit direction of the blade's motion at the contact
    VrVector3 direction;
  };

  /**
   * \brief Places the hit of a blade on a bone's capsule
   *
   * \param [in] contact Contact of the blade with the bone, see vrSweptSegmentContact
   * \param [in] radius Radius of the capsule around the bone
   * \returns The hit; its direction is the blade's motion, or from the blade
   *    to the bone if the touching point of the blade did not move
   */
  VrBladeHit vrComputeBladeHit(const VrSweepContact& contact, float radius);

  /**
   * \brief Follows the swings of a blade, frame after frame
   *
   * A swing lasts while the blade's tip moves at least as fast as the hit
   * speed. Each target is hit once per swing, and not again before
   * \ref RehitTime, so that shaking the blade against a target does not
   * hit it at every shake.
   *
   * The tip is followed in the player's frame, not in the world, so that
   * walking, running or turning with a still hand is no swing.
   */
  class VrSwingTracker {

  public:

    /// Shortest time between two hits on the same target, in seconds (at
    /// most three hits per second)
    constexpr static float RehitTime = 1.0f / 3.0f;

    explicit VrSwingTracker(const VrSwingConfig& config)
    : m_config(config) { }

    /**
     * \brief Forgets the blade, when it is not held any more
     */
    void reset();

    /**
     * \brief Moves the tip to where it is in this frame
     *
     * \param [in] tip Tip position in the player's frame, in game units
     * \param [in] dt Time since the previous frame, in seconds
     */
    void update(const VrVector3& tip, float dt);

    /**
     * \brief Speed of the tip in this frame, in metres per second
     */
    float speed() const {
      return m_speed;
    }

    /**
     * \brief Whether the blade moves fast enough to hit
     */
    bool swinging() const {
      return m_hasTip && m_speed >= m_config.hitSpeed;
    }

    /**
     * \brief Whether a target cannot be hit yet
     *
     * It was hit during the current swing, or less than \ref RehitTime ago.
     *
     * \param [in] target Compared, never dereferenced
     */
    bool hasHit(const void* target) const;

    /**
     * \brief Records that a target was hit during the current swing
     */
    void addHit(const void* target);

  private:

    struct Hit {
      const void* target;
      uint32_t    swing;
      float       age;
    };

    VrSwingConfig m_config;

    VrVector3 m_tip;
    bool      m_hasTip = false;
    float     m_speed  = 0.0f;

    // Counts the swings, to tell the hits of the current one
    uint32_t  m_swing = 0;

    std::vector<Hit> m_hits;

    bool blocks(const Hit& hit) const;

  };

  /**
   * \brief Whether a blade is held as a guard in front of the head
   *
   * The blade lies close to horizontal, across the front of the head
   * rather than pointing away from it, its middle in front of the face and
   * within reach, between the chest and slightly above the head.
   *
   * \param [in] head Head pose in the game world
   * \param [in] blade Blade in the game world
   */
  bool vrIsGuardPose(const VrGameCameraPose& head, const VrSegment& blade);

  /**
   * \brief Holds the guard while the blade stays in the guard pose
   *
   * The guard starts as soon as the pose is taken and the blade is slow,
   * and ends once the pose has been left for \ref ReleaseTime, so that a
   * shaking hand at the edge of the pose does not drop the guard.
   */
  class VrGuardTracker {

  public:

    /// How long the pose must be left to end the guard, in seconds
    constexpr static float ReleaseTime = 0.15f;

    /**
     * \brief Updates the guard for a frame
     *
     * \param [in] pose Whether the blade is in the guard pose
     * \param [in] swinging Whether the blade moves fast enough to hit
     * \param [in] dt Time since the previous frame, in seconds
     */
    void update(bool pose, bool swinging, float dt);

    /**
     * \brief Ends the guard at once
     */
    void reset();

    /**
     * \brief Whether the guard is held
     */
    bool guarding() const {
      return m_guarding;
    }

  private:

    bool  m_guarding = false;
    float m_outTime  = 0.0f;

  };

}
