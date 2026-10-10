#pragma once

#include <array>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Buttons of the virtual gamepad that are currently pressed
   */
  struct VrGamepadKeys {
    bool dpadUp    = false;
    bool dpadDown  = false;
    bool dpadLeft  = false;
    bool dpadRight = false;
    bool start     = false;
    bool back      = false;
    bool a         = false;
    bool b         = false;
    bool x         = false;
    bool y         = false;
  };

  /**
   * \brief Button bits of the XInput gamepad state, as in XINPUT_GAMEPAD
   */
  namespace VrGamepadButton {
    constexpr uint16_t DpadUp    = 0x0001;
    constexpr uint16_t DpadDown  = 0x0002;
    constexpr uint16_t DpadLeft  = 0x0004;
    constexpr uint16_t DpadRight = 0x0008;
    constexpr uint16_t Start     = 0x0010;
    constexpr uint16_t Back      = 0x0020;
    constexpr uint16_t LeftThumb = 0x0040;
    constexpr uint16_t RightThumb = 0x0080;
    constexpr uint16_t LeftShoulder = 0x0100;
    constexpr uint16_t RightShoulder = 0x0200;
    constexpr uint16_t A         = 0x1000;
    constexpr uint16_t B         = 0x2000;
    constexpr uint16_t X         = 0x4000;
    constexpr uint16_t Y         = 0x8000;
  }

  /**
   * \brief Converts pressed virtual gamepad buttons to an XInput button mask
   *
   * Opposite directions of the D-pad cancel out, as they do on a real pad.
   */
  uint16_t vrGamepadButtons(const VrGamepadKeys& keys);


  /**
   * \brief Keeps a button reported as pressed for a minimum number of polls
   *
   * A key tapped by an automation tool is down for a few milliseconds, so the
   * game would see it in a single poll at best. The game's menus need a button
   * to stay down for a few frames, so every press is stretched to
   * \ref MinPolls reports.
   */
  class VrGamepadHold {

  public:

    constexpr static uint32_t MinPolls = 4;

    /**
     * \brief Records one poll of the game
     *
     * \param [in] pressed Button mask sampled from the keys
     * \returns Button mask to report to the game
     */
    uint16_t update(uint16_t pressed);

  private:

    std::array<uint32_t, 16> m_remaining = { };

  };

}
