#pragma once

#include "vr_controller_pad.h"
#include "vr_emulator_rig.h"

namespace dxvk {

  /**
   * \brief Presents a virtual gamepad to the game when no real one is connected
   *
   * Over Remote Desktop the game cannot use the mouse, because no relative
   * mouse device exists and the menu cursor jumps to a corner. With a gamepad
   * connected the game navigates its menus with the D-pad and the A and B
   * buttons instead. The hook replaces the game's polling call of XInputGetState
   * and fills in the state from the keyboard whenever the real call reports no
   * device. A real gamepad is passed through untouched.
   *
   * Keys, read with the global key state like the emulated head: I, K, J, L
   * are the D-pad (up, down, left, right), Enter is A, U is B, H is X, Y is Y,
   * P is Start and O is Back.
   *
   * With a real gamepad the same hook also reads its state to drive the
   * emulated head, see \ref VrGamepadHead.
   *
   * With VR controllers, the hook reports the gamepad state the controllers
   * produce (see \ref vrComputePadState) while they are in use, instead of
   * a real or virtual gamepad.
   *
   * It can also hide every gamepad from the game. The game switches its menus
   * to gamepad navigation and ignores the mouse cursor as soon as XInput
   * reports a device, and it has no setting to ignore XInput. The interface
   * checks for a gamepad on its own, so hiding also covers those calls.
   *
   * With VR controllers, the message boxes the game shows when a gamepad
   * is connected or disconnected are skipped: the controllers appear and
   * disappear with tracking.
   */
  class VrGamepadHook {

  public:

    struct Options {
      /// Report a virtual gamepad if none is connected
      bool virtualPad  = false;
      /// Drive the emulated head from a real gamepad
      bool headControl = false;
      /// Report no gamepad even if one is connected
      bool hidePad     = false;
      /// Report the state of the VR controllers while they are in use
      bool controllers = false;
    };

    /**
     * \brief Redirects the game's gamepad polling call
     *
     * \param [in] options What the hook reports to the game
     * \returns \c true if the call matched and was redirected
     */
    static bool install(const Options& options);

    /**
     * \brief Sets the gamepad state of the VR controllers
     *
     * Called once per frame, on the game's thread. Only has an effect if
     * the hook was installed with \c controllers.
     *
     * \param [in] active Whether the controllers are in use. While they
     *    are, the game is in gamepad mode, so keyboard and mouse controls
     *    are ignored in game.
     * \param [in] pad State to report
     */
    static void setControllerPad(bool active, const VrPadState& pad);

    /**
     * \brief Switches the menus to the mouse while the controllers are in use
     *
     * In cursor mode the interface's own gamepad check reports no gamepad,
     * so the interface shows the cursor and follows the mouse (see
     * VrMenuCursor), while the game itself still sees the controllers.
     * Called once per frame, on the game's thread.
     */
    static void setCursorMode(bool enabled);

    /**
     * \brief Returns the head input from the latest real gamepad poll
     */
    static VrEmulatorInput headInput();

  };

}
