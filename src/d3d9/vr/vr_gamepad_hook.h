#pragma once

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
   */
  class VrGamepadHook {

  public:

    /**
     * \brief Redirects the game's gamepad polling call
     * \returns \c true if the call matched and was redirected
     */
    static bool install();

  };

}
