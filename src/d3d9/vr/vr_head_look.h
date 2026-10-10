#pragma once

#include <cstdint>
#include <memory>

#include "vr_game_space.h"
#include "vr_turn_input.h"

namespace dxvk {

  /**
   * \brief Makes the player look where the head looks
   *
   * The game aims, activates and moves along the player's heading and look
   * angle. Right after the game's own look handler has run in the player
   * update, the heading is set to the body heading plus the head's yaw, and
   * the look angle to the head's pitch. Movement then follows the head, and
   * so do shots and the crosshair.
   *
   * The body heading is what the turning stick changes. Any other change of
   * the heading, such as the mouse or a script, is kept as a body turn too.
   *
   * The game builds its camera from that heading and pitch, so the head's
   * rotation is already in it. The eyes are placed on a level camera facing
   * the body heading instead, see \ref adjustCamera.
   *
   * Dialogues do not turn the player towards the speaker while the head
   * is tracked: the user looks at the speaker by turning their head.
   *
   * The game keeps control of the heading while VATS runs, while the player
   * sits, is dead or has the look control disabled.
   */
  class VrHeadLook {

  public:

    ~VrHeadLook();

    /**
     * \brief Hooks the player's look handler
     *
     * \param [in] turn How the turning stick turns
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrHeadLook> install(const VrTurnConfig& turn);

    /**
     * \brief Takes the latest head pose and turning stick
     *
     * Called once per frame, before the next player update.
     *
     * \param [in] headInReference Head pose relative to the recenter
     *    reference, or \c nullptr when the head is not tracked
     * \param [in] turnStick Horizontal deflection of the turning stick
     * \param [in] dt Duration of a frame in seconds
     */
    void update(const VrPose* headInReference, float turnStick, float dt);

    /**
     * \brief Replaces the game's camera rotation by the body's
     *
     * Only while the heading and look angle are still the ones written
     * from the head. The position is kept.
     *
     * \param [in,out] pose Camera pose the game computed
     * \returns \c true if the pose was changed
     */
    bool adjustCamera(VrGameCameraPose& pose) const;

  private:

    explicit VrHeadLook(const VrTurnConfig& turn);

    VrTurnInput  m_turn;
    VrHeadAngles m_head;
    bool         m_hasHead     = false;
    float        m_pendingTurn = 0.0f;

    // What the last player update wrote, while the head drives the look
    bool         m_active         = false;
    float        m_writtenHeading = 0.0f;
    float        m_writtenPitch   = 0.0f;
    float        m_bodyHeading    = 0.0f;

    bool         m_loggedActive   = false;

    static uint32_t __fastcall lookHook(
            uint8_t*              player,
            void*                 unused,
            float                 dt,
            uint32_t              arg1,
            void*                 moveFlags,
            uint32_t              arg3);

    static void __fastcall focusHook(
            uint8_t*              player,
            void*                 unused,
            void*                 actor,
            float                 blend,
            uint32_t              skipTurn);

    void applyLook(uint8_t* player);

    static bool headControlsLook(uint8_t* player);

  };

}
