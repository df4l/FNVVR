#pragma once

#include <cstdint>
#include <memory>

#include "vr_game_space.h"
#include "vr_roomscale_rules.h"

namespace dxvk {

  /**
   * \brief Keeps the player's capsule under the head
   *
   * The player walking in the room used to move the eyes away from the
   * game's capsule: the head went through walls, and the capsule, left
   * behind, caught on door frames. Here the head's move goes into the move
   * of the frame, so the character controller treats it as the stick's:
   * walls stop it, slopes and stairs lift it, a ledge drops it. The
   * reference then moves by what the capsule did, which leaves the eyes
   * where they are in the world.
   *
   * What the capsule could not do stays between the head and the capsule,
   * up to the head room, where the view stops at the obstacle. See
   * findings/roomscale.md.
   *
   * Only used on the game's main thread.
   */
  class VrRoomscale {

  public:

    ~VrRoomscale();

    /**
     * \brief Hooks the player's move
     *
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrRoomscale> install();

    /**
     * \brief Turns the capsule's following on or off
     *
     * \param [in] enabled Off, the capsule only moves with the stick
     */
    void setEnabled(bool enabled) {
      m_enabled = enabled;
    }

    /**
     * \brief Takes what the capsule did and sets its next move
     *
     * Called once per frame, once the game's camera is known.
     *
     * \param [in,out] reference The recenter reference, moved by the capsule
     * \param [in] base The game's camera for this frame, turned with the body
     * \param [in] head Head pose in tracking space, or \c nullptr if unknown
     * \param [in] follows The player walks freely and the camera follows the head
     */
    void update(
            VrPose&               reference,
      const VrGameCameraPose&     base,
      const VrPose*               head,
            bool                  follows);

  private:

    VrRoomscale() = default;

    bool      m_enabled      = false;
    bool      m_loggedActive = false;

    // In the world's ground plane, in game units
    VrVector2 m_wanted;
    VrVector2 m_achieved;

    float     m_followTime   = 0.0f;
    float     m_blockedTime  = 0.0f;

    void* movePlayer(uint8_t* player, float dt, VrVector3* move, uint8_t flags);

    static void* __fastcall moveHook(
            uint8_t*              mobile,
            void*                 unused,
            float                 dt,
            VrVector3*            move,
            uint8_t               flags);

  };

}
