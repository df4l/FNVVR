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
   *  - Home, End: turn the head left and right
   *  - Insert, Delete: look up and down
   *  - Backspace: put the head back at the origin
   *
   * Keys are read with the global key state, so the window that has the
   * focus does not matter. A tap shorter than a frame still counts as one
   * frame of input.
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
