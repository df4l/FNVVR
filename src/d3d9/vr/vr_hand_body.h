#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "vr_grab.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief A keyframed Havok sphere at each hand that pushes objects
   *
   * Each hand has a small sphere, made as the game makes bodies of its own
   * (findings/grab.md §18), in the world of the player's cell. It is
   * keyframed: Havok moves it at the velocity it is given, it pushes the
   * bodies it meets and nothing pushes it. It has the player's collision
   * group, so it neither pushes the player nor what the hands hold, and
   * the small debris layer, so it meets clutter, props, debris and corpses
   * but not projectiles, triggers, water, picks or living actors.
   *
   * The sphere is moved in the game's update of the player, with the hand
   * of the last VR frame, as the grab's springs are. While the player is
   * not in control, that update is not reached: the spheres are stopped
   * from the update's other branch (stop), else they would go on at their
   * last velocity. As the player moves to another cell with a world of
   * its own, the spheres leave the old world (leaveWorld); while the new
   * world loads, none is made.
   *
   * Only used on the game's main thread, while Havok does not step.
   */
  class VrHandBodies {

  public:

    /// The sphere's radius, in metres: a palm
    constexpr static float Radius = 0.045f;

    /// Fastest a hand moves, in metres per second
    constexpr static float MaxSpeed = 12.0f;

    /**
     * \brief Checks the game's functions
     *
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrHandBodies> create();

    /**
     * \brief Moves a hand's sphere for the next Havok step
     *
     * \param [in] player The player
     * \param [in] hand The hand
     * \param [in] position The hand's centre in the game world, or
     *    \c nullptr when it is not tracked
     * \param [in] free The hand is free and its grip open
     * \param [in] dt Time of the step, in seconds
     */
    void update(
            uint8_t*              player,
            VrHand                hand,
      const VrVector3*            position,
            bool                  free,
            float                 dt);

    /**
     * \brief Stops both spheres, which stop pushing
     */
    void stop();

    /**
     * \brief Takes the spheres out of any world but one, and lets them go
     *
     * Called as the player moves to another cell, before the game may
     * unload the old cell's world. The spheres are made again in the
     * player's world at the next update.
     *
     * \param [in] world The hkpWorld the spheres may stay in, or
     *    \c nullptr for none
     * \returns \c true if a sphere was let go of
     */
    bool leaveWorld(const uint8_t* world);

  private:

    struct Body {
      /// NiPointer<bhkShape> and NiPointer<bhkRigidBody>
      void*          shape  = nullptr;
      void*          object = nullptr;
      VrHandBodyGate gate;
      uint32_t       filter = 0;
    };

    VrHandBodies() = default;

    std::array<Body, VrHandCount> m_bodies;

    bool m_loggedCreated = false;
    bool m_loggedFailed  = false;

    bool make(Body& body, const VrVector3& position, uint32_t filter);

    void drop(Body& body);

    void setFilter(Body& body, uint32_t filter);

    void halt(Body& body);

  };

}
