#pragma once

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Turns the raw actions into the actions the game acts on
   *
   * Two rules apply:
   *
   * - The Pip-Boy action has a tap and a hold. A tap opens the Pip-Boy when
   *   the button is released, holding it for \ref HoldTime opens the pause
   *   menu. Both menu buttons of the Quest controllers are taken by SteamVR
   *   and the headset's system, so the pause menu has no button of its own.
   *   The separate Pause action still works if the user binds it.
   *
   * - When the context changes, actions that are already held are ignored
   *   until they are released. The same button means something else in a
   *   menu, so holding it across the change would trigger the menu action
   *   (for example the pause menu's Y right after the long press opened it).
   */
  class VrActionFilter {

  public:

    /// How long the Pip-Boy action is held to open the pause menu, in seconds
    constexpr static float HoldTime  = 0.6f;
    /// How long a tap or a hold is reported, in seconds, at least one frame
    constexpr static float PulseTime = 0.1f;

    /**
     * \brief Filters the actions of a frame
     *
     * \param [in] actions Actions read by the backend
     * \param [in] context Context the actions were read in
     * \param [in] dt Time since the previous frame, in seconds
     * \returns Actions to pass to the game
     */
    VrActionState update(
      const VrActionState&        actions,
            VrInputContext        context,
            float                 dt);

  private:

    VrInputContext m_context = VrInputContext::Game;
    uint32_t       m_blocked = 0;

    bool  m_pipBoyDown  = false;
    bool  m_holdFired   = false;
    float m_heldTime    = 0.0f;
    float m_tapPulse    = 0.0f;
    float m_holdPulse   = 0.0f;

    void updatePipBoy(bool down, float dt);

  };

}
