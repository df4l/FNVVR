#pragma once

#include <memory>

#include "vr_action_filter.h"
#include "vr_backend.h"
#include "vr_controller_pad.h"
#include "vr_game_state.h"
#include "vr_head_look.h"
#include "vr_menu_pointer.h"
#include "vr_sneak.h"

namespace dxvk {

  /**
   * \brief Hands the controller input to the game each frame
   *
   * The actions, filtered by VrActionFilter, become a gamepad state for
   * the game (see VrGamepadHook), the head pose and the turning stick go
   * to VrHeadLook, the head's height to VrSneak, and the backend
   * is told whether the next frame's actions are read for the game or for
   * a menu. In menus the controllers also point at the menu panel with
   * lasers, which drive the game's cursor (see VrMenuPointer), or at the
   * console's terminal while it is shown (see VrConsoleTerminal).
   */
  class VrControls {

  public:

    explicit VrControls(IVRBackend& backend);

    /**
     * \brief Makes the head drive the player's look
     */
    void setHeadLook(std::unique_ptr<VrHeadLook> headLook);

    /**
     * \brief Changes how the turning stick turns
     */
    void setTurnConfig(const VrTurnConfig& turn);

    /**
     * \brief Makes the player sneak by crouching when chosen
     */
    void setSneak(std::unique_ptr<VrSneak> sneak);

    /**
     * \brief Changes how the player sneaks
     */
    void setSneakConfig(const VrSneakConfig& config);

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
     * \brief Keeps the attack control from the game from the next frame on
     *
     * \param [in] hand Whether the melee weapon in the hand is swung with
     *    the hand (see VrMeleeSwing), or the weapon in it thrown with the
     *    hand (see VrHandThrow), so that the game does not swing or throw
     *    it with its own animation as well
     */
    void setSwingsWithHand(bool hand) {
      m_swingsWithHand = hand;
    }

    /**
     * \brief Keeps the attack control from the game while the hands are free
     *
     * \param [in] free Whether the weapon is holstered by the player, so
     *    that the trigger does not draw it (the game draws the weapon on
     *    an attack)
     */
    void setHandsFree(bool free) {
      m_handsFree = free;
    }

    /**
     * \brief Keeps the left hand's reload button from the game
     *
     * \param [in] taken Whether the left hand holds ammunition whose type
     *    the button changes, see VrActionFilter
     */
    void setReloadTaken(bool taken) {
      m_filter.setReloadTaken(taken);
    }

    /**
     * \brief Keeps the right stick from turning and from scrolling menus
     *
     * \param [in] taken Whether the quick wheel holds it
     */
    void setRightStickTaken(bool taken) {
      m_rightStickTaken = taken;
    }

    /**
     * \brief Adds a mini-game's physical controls to the virtual gamepad
     *
     * Applied in menus from the next update on.
     */
    void setMinigamePad(const VrMinigamePad& pad) {
      m_minigamePad = pad;
    }

    /**
     * \brief Sets the surface of a 3D scene the lasers can point at
     *
     * \param [in] surface Stays valid until the next call, or \c nullptr
     */
    void setPointerSurface(const VrPanelPlacement* surface) {
      m_pointerSurface = surface;
    }

    /**
     * \brief Where the lasers point on that surface in the last update
     */
    const VrPointerSurfaceState& pointerSurfaceState() const {
      return m_pointerSurfaceState;
    }

    /**
     * \brief Makes the navigation stick pick a hotkey on the game's wheel
     *
     * \param [in] shown Whether the Pip-Boy's inventory shows its hotkey
     *    wheel, see vrComputePadState
     */
    void setHotKeyWheelShown(bool shown) {
      m_hotKeyWheelShown = shown;
    }

    /**
     * \brief Makes the right grip holster or draw the weapon
     *
     * \param [in] atHolster Whether the right hand is at the holster of the
     *    equipped weapon, see VrActionFilter
     */
    void setRightHandAtHolster(bool atHolster) {
      m_filter.setGrabAtHolster(atHolster);
    }

    /**
     * \brief Keeps the grip from pressing the game's grab control
     *
     * \param [in] byHands Whether the hands hold objects themselves, see
     *    VrActionFilter
     */
    void setGrabByHands(bool byHands) {
      m_filter.setGrabByHands(byHands);
    }

    /**
     * \brief Clicks the menu's back button from the next frame on
     *
     * Closes the Pip-Boy the way the player's back button does: the game
     * reads it while it updates its interface, so the Pip-Boy closes as
     * it would with the gamepad. The button is pressed and released on
     * alternate frames while this is set, only in menus.
     *
     * \param [in] back Whether VrPipBoyGaze wants the Pip-Boy closed
     */
    void setMenuBack(bool back) {
      m_menuBack = back;
    }

    /**
     * \brief Makes the left hand's reload button activate
     *
     * \param [in] left Whether the interaction prompt is at the left hand,
     *    see VrActionFilter
     */
    void setPromptAtLeftHand(bool left) {
      m_filter.setReloadActivates(left);
    }

    /**
     * \brief Makes the right grip switch the Pip-Boy light
     *
     * \param [in] holdTime How long the game's Pip-Boy control is held to
     *    switch the light, or 0 while the right hand is not at the Pip-Boy,
     *    see VrActionFilter
     */
    void setRightHandAtPipBoy(float holdTime) {
      m_filter.setGrabLightHoldTime(holdTime);
    }

    /**
     * \brief Whether the Pip-Boy control is held to switch the light
     */
    bool switchesPipBoyLight() const {
      return m_filter.holdsPipBoyControl();
    }

    /**
     * \brief Points the lasers at the console's terminal from the next update on
     *
     * While the terminal is shown, the lasers point at it instead of the
     * menu panel and no longer drive the game's cursor, and the controllers
     * press nothing in the game's menus: the console has the input. What
     * the lasers do on the terminal is returned by terminalPointer.
     *
     * \param [in] panel Placement of the terminal in tracking space, or
     *    \c nullptr while it is hidden; it must stay valid until update
     */
    void setTerminalPanel(const VrPanelPlacement* panel) {
      m_terminalPanel = panel;
    }

    /**
     * \brief What the lasers did on the terminal in the last update
     */
    const VrPointerState& terminalPointer() const {
      return m_terminalPointer;
    }

    /**
     * \brief Whether the menu's back button was pressed on the terminal in the last update
     */
    bool terminalBack() const {
      return m_terminalBack;
    }

    /**
     * \brief Adjusts the camera the game computed for this frame
     *
     * See VrHeadLook::adjustCamera; while the head drives the camera, it
     * is also raised by what the game's sneaking lowered, if the player
     * sneaks by crouching (see VrSneak). Called once per frame, before
     * any call to adjustCamera.
     *
     * \param [in,out] pose Camera pose the game computed
     * \param [in] dt Duration of a frame in seconds
     * \returns \c true if the pose was changed
     */
    bool updateCamera(VrGameCameraPose& pose, float dt);

    /**
     * \brief Adjusts a camera pose as the last updateCamera did
     */
    bool adjustCamera(VrGameCameraPose& pose) const;

    /**
     * \brief See VrHeadLook::takeDialogueSpeaker
     */
    void* takeDialogueSpeaker();

  private:

    void updateGameInput(
      const VrInputState&         input,
            VrActionState&        actions,
      const VrPanelPlacement*     menuPanel,
            float                 dt);

    void updateTerminal(
      const VrInputState&         input,
            VrActionState&        actions,
            float                 dt);

    VrPointerState updatePointer(
      const VrInputState&         input,
            VrActionState&        actions,
      const VrPanelPlacement*     panel,
      const VrPanelPlacement*     surface,
            bool                  padUsed,
            float                 dt);

    IVRBackend&                 m_backend;
    std::unique_ptr<VrHeadLook> m_headLook;
    std::unique_ptr<VrSneak>    m_sneak;
    VrActionFilter              m_filter;
    VrMenuPointer               m_pointer;

    // Context the backend reads the actions in, chosen one frame ahead
    VrInputContext              m_context = VrInputContext::Game;

    bool                        m_gameAim = false;
    bool                        m_swingsWithHand = false;
    bool                        m_handsFree      = false;
    bool                        m_rightStickTaken = false;
    VrMinigamePad               m_minigamePad;
    const VrPanelPlacement*     m_pointerSurface = nullptr;
    VrPointerSurfaceState       m_pointerSurfaceState;
    bool                        m_hotKeyWheelShown = false;
    bool                        m_menuBack = false;
    bool                        m_menuBackDown = false;

    const VrPanelPlacement*     m_terminalPanel = nullptr;
    VrPointerState              m_terminalPointer;
    bool                        m_terminalBack = false;
    bool                        m_terminalBackDown = false;

  };

}
