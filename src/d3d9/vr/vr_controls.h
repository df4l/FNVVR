#pragma once

#include <memory>

#include "vr_action_filter.h"
#include "vr_backend.h"
#include "vr_game_state.h"
#include "vr_head_look.h"
#include "vr_menu_pointer.h"

namespace dxvk {

  /**
   * \brief Hands the controller input to the game each frame
   *
   * The actions, filtered by VrActionFilter, become a gamepad state for
   * the game (see VrGamepadHook), the head pose and the turning stick go
   * to VrHeadLook, and the backend
   * is told whether the next frame's actions are read for the game or for
   * a menu. In menus the controllers also point at the menu panel with
   * lasers, which drive the game's cursor (see VrMenuPointer).
   */
  class VrControls {

  public:

    explicit VrControls(IVRBackend& backend);

    /**
     * \brief Makes the head drive the player's look
     */
    void setHeadLook(std::unique_ptr<VrHeadLook> headLook);

    /**
     * \brief Processes the input sampled for a frame
     *
     * \param [in] input Input returned by the backend
     * \param [in] state What the game shows
     * \param [in] reference Recenter reference, or \c nullptr if none yet
     * \param [in] menuPanel Placement of the menu panel in tracking space,
     *    or \c nullptr if it is not shown
     * \param [in] dt Duration of a frame in seconds
     */
    void update(
      const VrInputState&         input,
            VrGameStateKind       state,
      const VrPose*               reference,
      const VrPanelPlacement*     menuPanel,
            float                 dt);

    /**
     * \brief Presses the game's aim control from the next frame on
     *
     * \param [in] aim Whether VrWeaponHand wants the game to aim
     */
    void setGameAim(bool aim) {
      m_gameAim = aim;
    }

    /**
     * \brief See VrHeadLook::adjustCamera
     */
    bool adjustCamera(VrGameCameraPose& pose) const;

  private:

    VrPointerState updatePointer(
      const VrInputState&         input,
            VrActionState&        actions,
      const VrPanelPlacement*     menuPanel,
            float                 dt);

    IVRBackend&                 m_backend;
    std::unique_ptr<VrHeadLook> m_headLook;
    VrActionFilter              m_filter;
    VrMenuPointer               m_pointer;

    // Context the backend reads the actions in, chosen one frame ahead
    VrInputContext              m_context = VrInputContext::Game;

    bool                        m_gameAim = false;

  };

}
