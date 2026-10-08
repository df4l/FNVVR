#pragma once

#include <cstdint>
#include <mutex>

#include "vr_emulator_rig.h"

namespace dxvk {

  /**
   * \brief Gamepad state as reported by XInput, reduced to what the head needs
   */
  struct VrGamepadSample {
    uint16_t buttons       = 0;
    int16_t  thumbLeftX    = 0;
    int16_t  thumbLeftY    = 0;
    int16_t  thumbRightX   = 0;
    int16_t  thumbRightY   = 0;
    uint8_t  leftTrigger   = 0;
    uint8_t  rightTrigger  = 0;
  };

  /**
   * \brief Button bits that the head control uses, as in XINPUT_GAMEPAD
   */
  namespace VrGamepadHeadButton {
    constexpr uint16_t DpadUp        = 0x0001;
    constexpr uint16_t DpadDown      = 0x0002;
    constexpr uint16_t LeftShoulder  = 0x0100;
    constexpr uint16_t RightShoulder = 0x0200;
    constexpr uint16_t Y             = 0x8000;
  }

  /**
   * \brief Converts a stick axis to [-1, 1] with a dead zone
   *
   * The range outside the dead zone is rescaled so that the output starts
   * at zero at the edge of the dead zone.
   */
  float vrStickAxis(int16_t value);

  /**
   * \brief Adds two inputs, keeping the movement axes within [-1, 1]
   */
  VrEmulatorInput vrMergeInput(const VrEmulatorInput& a, const VrEmulatorInput& b);

  /**
   * \brief Drives the emulated head from a real gamepad
   *
   * The head is controlled while both shoulder buttons are held. In that
   * mode the left stick moves the head (right, forward), the right stick
   * turns it and looks up and down, the D-pad moves it up and down and Y puts
   * it back at the origin. The same sample is blanked for the game, so
   * the buttons and sticks do not act in the game at the same time. Without
   * the chord the sample is left untouched.
   *
   * \c apply is called from the game thread and \c input from the thread of
   * the emulator, so both are synchronised.
   */
  class VrGamepadHead {

  public:

    /// Mouse pixels per frame reported at full stick deflection
    constexpr static float TurnPixelsPerFrame = 6.0f;

    /**
     * \brief Records a gamepad sample
     *
     * \param [in,out] sample Sample the game polled. Cleared if the head
     *    is being controlled so that the game does not receive it.
     */
    void apply(VrGamepadSample& sample);

    /**
     * \brief Returns the head input of the latest sample
     */
    VrEmulatorInput input() const;

  private:

    mutable std::mutex m_mutex;
    VrEmulatorInput    m_input;

  };

}
