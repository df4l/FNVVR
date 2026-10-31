#pragma once

#include <array>
#include <memory>

#include "vr_melee.h"

namespace dxvk {

  /**
   * \brief Melee weapons and fists hit world objects too
   *
   * The game's swing hits clutter and world objects with Actor::ObjectHit
   * when no actor is in reach (findings/melee.md §14): a shape cast along
   * the view, an impulse on the body hit along the weapon's motion, the
   * impact sound and decal of the material, and the OnHit script blocks.
   * The swing with the hand never reaches it. Here, a striker (a blade or
   * a fist) moving at the hit speed that touches no actor is swept against
   * the world with rays; when they meet something that is not an actor,
   * the game's ObjectHit runs with the cast started just before the point
   * met, along the striker's motion, and with the striker's motion as the
   * weapon's, so the game does the rest.
   *
   * Havok steps on another thread while the frame is drawn, so the strikes
   * found by the VR frame are only cast in the next update of the player
   * (VrPhysicsGrab's hook, see run), where Havok is idle.
   *
   * Only used on the game's main thread.
   */
  class VrMeleeObjectHit {

  public:

    /// Strikers: the blade, then each fist
    constexpr static uint32_t StrikerCount = 3;

    ~VrMeleeObjectHit();

    /**
     * \brief Finds the game's functions and patches the two calls
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrMeleeObjectHit> install();

    /**
     * \brief Gives the sweep of a striker that hit no actor this frame
     *
     * \param [in] striker Index of the striker, below StrikerCount
     * \param [in] from Striker on the previous frame, in the game world
     * \param [in] to Striker in this frame, in the game world
     * \param [in] power Whether a hit is a power attack
     */
    void strike(uint32_t striker, const VrSegment& from, const VrSegment& to, bool power);

    /**
     * \brief Casts the strikes given since the last run
     *
     * Called from the player's update, while Havok is idle.
     *
     * \param [in] player The PlayerCharacter
     * \param [in] dt Time since the last update, in seconds
     */
    void run(uint8_t* player, float dt);

  private:

    struct Strike {
      VrSegment from;
      VrSegment to;
      bool      power   = false;
      bool      pending = false;
      /// Body hit last, and the time since, so a body is hit once per swing
      const void* lastBody = nullptr;
      float       lastAge  = 0.0f;
    };

    VrMeleeObjectHit() = default;

    std::array<Strike, StrikerCount> m_strikes;

    // The cast and the weapon's motion of the ObjectHit being run
    bool      m_running = false;
    VrVector3 m_eye;
    VrVector3 m_direction;

    uint32_t  m_loggedHits = 0;

    bool cast(uint8_t* player, Strike& strike);

    static void __fastcall eyeVectorHook(uint8_t* actor, void* unused,
      VrVector3* position, VrVector3* direction, uint32_t thirdPerson);

    static VrVector3* __fastcall weaponPositionHook(uint8_t* actor, void* unused,
      VrVector3* out, uint32_t unknown);

  };

}
