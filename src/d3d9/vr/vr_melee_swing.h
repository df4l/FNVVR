#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "vr_melee.h"
#include "vr_melee_object_hit.h"

namespace dxvk {

  /**
   * \brief What a swing did in a frame
   */
  struct VrMeleeHit {
    /// Number of actors hit
    uint32_t count = 0;
    /// Whether the swing's hits were power attacks
    bool     power = false;
  };

  /**
   * \brief A hand that may punch
   */
  struct VrFistInput {
    /// Pose of the hand in the game world, or \c nullptr when it is not
    /// tracked or busy (holding an object, at the Pip-Boy...)
    const VrGameCameraPose* hand = nullptr;
    /// The hand's fingers make a fist (vrIsFist)
    bool closed = false;
  };

  /**
   * \brief Hits what the blade of the melee weapon in the hand touches
   *
   * The player swings the weapon held in the hand instead of pressing the
   * attack control, see findings/melee.md §9. Each frame, the blade is
   * swept from where it was on the previous frame to where the hand holds
   * it now, against capsules around the skeleton bones of the loaded
   * actors. Both are taken relative to the player's body, so that only the
   * hand's own motion sweeps and makes speed: running into an actor or
   * turning with the weapon held still does not hit. When it touches one while its tip moves at least at the hit
   * speed, the game's Actor::CombatHit deals the hit as if the player's
   * swing animation had reached it: the game computes the damage,
   * criticals, sounds, reaction and crime. The limb it hits is the one the
   * blade touched, not the one the game would pick along the view, and the
   * blood and reaction follow the blade's contact and motion. A swing made
   * while a power attack is charged is a power attack. The blade held
   * still across the front of the head is a guard, which holds the game's
   * block control.
   *
   * Automatic melee weapons (chainsaw, thermic lance...) also run while the
   * attack control is held, as in the game: the control is passed to the
   * game, which plays the weapon's attack and decides when it hits, and
   * each of those hits lands on what the blade touches at that moment,
   * however slowly it moves, instead of the actor the game would pick in
   * front of the head. See findings/melee.md §11.
   *
   * Unarmed (no weapon or a hand-to-hand weapon drawn), each hand closed
   * in a fist punches what its knuckles touch the same way, with the
   * game's unarmed damage (findings/melee.md §13).
   *
   * Only used on the game's main thread.
   */
  class VrMeleeSwing {

  public:

    ~VrMeleeSwing();

    /**
     * \brief Finds the game's functions and data
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrMeleeSwing> install(const VrSwingConfig& config);

    /**
     * \brief Follows the blade for a frame and hits what it touches
     *
     * \param [in] hand Pose of the hand holding the weapon in the game
     *    world, or \c nullptr if it is not tracked or not in game
     * \param [in] blade Blade of the weapon in the hand, or \c nullptr if
     *    the hand holds no melee weapon
     * \param [in] automatic Whether that weapon is automatic
     * \param [in] power Whether the swing's hits are power attacks, see
     *    VrPowerCharge; the automatic weapon's hits never are
     * \param [in] head Head pose in the game world
     * \param [in] body Pose the eyes and hands are placed on in the game
     *    world, which moves and turns with the player
     * \param [in] dt Time since the previous frame, in seconds
     */
    VrMeleeHit update(
      const VrGameCameraPose*     hand,
      const VrBlade*              blade,
            bool                  automatic,
            bool                  power,
      const VrGameCameraPose&     head,
      const VrGameCameraPose&     body,
            float                 dt);

    /**
     * \brief Follows the fists for a frame and punches what they touch
     *
     * \param [in] fists Each hand
     * \param [in] out Whether the player fights unarmed: no weapon or a
     *    hand-to-hand weapon drawn
     * \param [in] body Pose the eyes and hands are placed on in the game
     *    world, which moves and turns with the player
     * \param [in] dt Time since the previous frame, in seconds
     * \returns Number of actors each hand hit
     */
    std::array<uint32_t, VrHandCount> updateFists(
      const std::array<VrFistInput, VrHandCount>& fists,
            bool                  out,
      const VrGameCameraPose&     body,
            float                 dt);

    /**
     * \brief Whether the game's attack control is held back
     *
     * A melee weapon swung with the hand, or fists punching, are not swung
     * by the game's own animation as well. Automatic melee weapons keep
     * the control, which runs them.
     */
    bool swingsWithHand() const {
      return (m_hasBlade && !m_automatic) || m_fistsOut;
    }

    /**
     * \brief Gives what hits world objects
     *
     * \param [in] objectHit Not owned, or \c nullptr: only actors are hit
     */
    void setObjectHit(VrMeleeObjectHit* objectHit) {
      m_objectHit = objectHit;
    }

    /**
     * \brief Whether holding the trigger charges a power attack
     *
     * Only for a melee weapon swung with the hand: the trigger closes the
     * index of a fist.
     */
    bool chargesWithTrigger() const {
      return m_hasBlade && !m_automatic;
    }

    /**
     * \brief Whether the game's block control should be held
     */
    bool wantsBlock() const {
      return m_guard.guarding();
    }

  private:

    VrMeleeSwing(const VrSwingConfig& config, bool motorFound)
    : m_swing(config), m_motorFound(motorFound),
      m_fistSwings({ VrSwingTracker(config), VrSwingTracker(config) }) { }

    /**
     * \brief Where the blade touched the actor being hit
     */
    struct Contact {
      uintptr_t  actor = 0;
      uintptr_t  bone  = 0;
      float      time  = 0.0f;
      float      distance = 0.0f;
      VrBladeHit hit;
    };

    VrSwingTracker m_swing;
    VrGuardTracker m_guard;

    // Whether the game's melee hits can be taken over, see meleeAttackHook
    bool           m_motorFound = false;

    /**
     * \brief What the game computed for the hit being dealt, for the log
     */
    struct Dealt {
      bool        seen         = false;
      bool        limbSet      = false;
      const char* limb         = nullptr;
      float       healthDamage = 0.0f;
      float       limbDamage   = 0.0f;
      uint32_t    flags        = 0;
    };

    // Contact of the hit being dealt, while Actor::CombatHit runs
    Contact        m_contact;
    Dealt          m_dealt;
    bool           m_dealing = false;

    // Blade on the previous frame, in the body's frame
    VrSegment      m_blade;
    bool           m_hasBlade = false;

    // An automatic melee weapon is in the hand, and the hits the game
    // decided for it since the last frame. The game may decide them on
    // another thread.
    std::atomic<bool>     m_automatic = { false };
    std::atomic<uint32_t> m_motorHits = { 0 };

    // Each fist on the previous frame, in the body's frame
    std::array<VrSwingTracker, VrHandCount> m_fistSwings;
    std::array<VrSegment, VrHandCount>      m_fists;
    std::array<bool, VrHandCount>           m_hasFist = { };
    bool           m_fistsOut = false;

    VrMeleeObjectHit* m_objectHit = nullptr;

    uint32_t       m_loggedHits = 0;
    uint32_t       m_loggedPunches = 0;
    uint32_t       m_loggedGuards = 0;
    uint32_t       m_loggedDamage = 0;
    uint32_t       m_loggedMotorHits = 0;

    void logDealt(uintptr_t actor);

    uint32_t hitActors(uint8_t* player, VrSwingTracker& swing, const VrSegment& from, const VrSegment& to,
      bool motor, bool power);

    uint32_t hitWithMotor(uint8_t* player, const VrSegment& blade);

    bool findContact(uintptr_t actor, const VrSegment& from, const VrSegment& to, Contact& contact) const;

    bool setTargetedLimb(uintptr_t hitData, void* projectile);

    int32_t limbOfContact(uintptr_t bodyParts) const;

    static void __fastcall setTargetedLimbHook(uintptr_t hitData, void* unused, void* projectile);

    static void __fastcall meleeAttackHook(uintptr_t actor, void* unused, uint32_t power, uint32_t inCombat);

  };

}
