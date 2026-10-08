#pragma once

#include "vr_emulator_rig.h"

namespace dxvk {

  /**
   * \brief Samples the keyboard to drive the emulated head
   *
   * The keys are chosen so that the game does not use them, which lets the
   * head be moved while the game keeps the focus:
   *
   *  - Arrow keys: move the head right, left, forward and backward
   *  - Page Up, Page Down: move the head up and down
   *  - Numpad 4, 6: turn the head left and right
   *  - Numpad 8, 2: look up and down
   *  - Numpad 0: put the head back at the origin
   *
   * Keys are read with the global key state, so the window that has the
   * focus does not matter.
   */
  class VrEmulatorKeyboard {

  public:

    /**
     * \brief Returns the input for one frame
     *
     * Turning is reported as mouse movement, in pixels per frame.
     */
    VrEmulatorInput sample() const;

  };

}
