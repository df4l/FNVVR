#include <algorithm>
#include <cmath>

#include "vr_controller_pad.h"
#include "vr_virtual_gamepad.h"

namespace dxvk {

  namespace {

    // Gamepad input codes of the game's bindings, see findings/input.md
    constexpr uint8_t InputLeftTrigger  = 0x10;
    constexpr uint8_t InputRightTrigger = 0x11;

    // Deflection of the navigation stick that presses a D-pad direction
    constexpr float NavigateThreshold = 0.5f;

    struct ActionControl {
      VrAction action;
      uint32_t control;
    };

    constexpr ActionControl GameControls[] = {
      { VrAction::Attack,   VrGameControl::Attack    },
      { VrAction::Activate, VrGameControl::Activate  },
      { VrAction::Jump,     VrGameControl::Jump      },
      { VrAction::Reload,   VrGameControl::ReadyItem },
      { VrAction::Sneak,    VrGameControl::Sneak     },
      { VrAction::Vats,     VrGameControl::Vats      },
      { VrAction::Grab,     VrGameControl::Grab      },
      { VrAction::Wait,     VrGameControl::SleepWait },
    };

    struct ActionButton {
      VrAction action;
      uint16_t button;
    };

    constexpr ActionButton MenuButtons[] = {
      { VrAction::MenuSelect,    VrGamepadButton::A             },
      { VrAction::MenuBack,      VrGamepadButton::B             },
      { VrAction::MenuAlternate, VrGamepadButton::X             },
      { VrAction::MenuOption,    VrGamepadButton::Y             },
      { VrAction::MenuPrevious,  VrGamepadButton::LeftShoulder  },
      { VrAction::MenuNext,      VrGamepadButton::RightShoulder },
    };

    uint16_t buttonForInput(uint8_t input) {
      switch (input) {
        case 0x01: return VrGamepadButton::DpadUp;
        case 0x02: return VrGamepadButton::DpadDown;
        case 0x04: return VrGamepadButton::DpadRight;
        case 0x05: return VrGamepadButton::DpadLeft;
        case 0x06: return VrGamepadButton::Start;
        case 0x07: return VrGamepadButton::Back;
        case 0x08: return VrGamepadButton::LeftThumb;
        case 0x09: return VrGamepadButton::RightThumb;
        case 0x0A: return VrGamepadButton::A;
        case 0x0B: return VrGamepadButton::B;
        case 0x0C: return VrGamepadButton::X;
        case 0x0D: return VrGamepadButton::Y;
        case 0x0E: return VrGamepadButton::RightShoulder;
        case 0x0F: return VrGamepadButton::LeftShoulder;
        default:   return 0;
      }
    }

    void pressInput(VrPadState& pad, uint8_t input) {
      if (input == InputLeftTrigger)
        pad.leftTrigger = 0xFF;
      else if (input == InputRightTrigger)
        pad.rightTrigger = 0xFF;
      else
        pad.buttons |= buttonForInput(input);
    }

    int16_t thumbValue(float value) {
      return int16_t(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
    }

    uint16_t navigateButtons(const VrVector2& stick) {
      if (std::max(std::abs(stick.x), std::abs(stick.y)) < NavigateThreshold)
        return 0;

      // Only the dominant direction, so that a diagonal does not move twice
      if (std::abs(stick.y) >= std::abs(stick.x))
        return stick.y > 0.0f ? VrGamepadButton::DpadUp : VrGamepadButton::DpadDown;

      return stick.x > 0.0f ? VrGamepadButton::DpadRight : VrGamepadButton::DpadLeft;
    }

    uint16_t hotKeyWheelButtons(const VrVector2& stick) {
      if (std::max(std::abs(stick.x), std::abs(stick.y)) < NavigateThreshold)
        return 0;

      // Eight sectors of 45 degrees: a diagonal presses both of its
      // directions at once, as the wheel needs them pressed together
      constexpr float Tilt = 0.41421356f; // tan(22.5 degrees)

      uint16_t buttons = 0;

      if (std::abs(stick.y) > Tilt * std::abs(stick.x))
        buttons |= stick.y > 0.0f ? VrGamepadButton::DpadUp : VrGamepadButton::DpadDown;

      if (std::abs(stick.x) > Tilt * std::abs(stick.y))
        buttons |= stick.x > 0.0f ? VrGamepadButton::DpadRight : VrGamepadButton::DpadLeft;

      return buttons;
    }

  }


  VrPadState vrComputePadState(
    const VrActionState&        actions,
          VrInputContext        context,
    const uint8_t*              bindings,
          VrMenuNavigation      navigation) {
    VrPadState pad;

    if (context == VrInputContext::Menu) {
      for (const ActionButton& entry : MenuButtons) {
        if (actions.isPressed(entry.action))
          pad.buttons |= entry.button;
      }

      // V.A.T.S. queues an attack on the selected body part with RT
      if (actions.isPressed(VrAction::MenuLeftTrigger))
        pad.leftTrigger = 0xFF;

      if (actions.isPressed(VrAction::MenuRightTrigger))
        pad.rightTrigger = 0xFF;

      pad.thumbRightX = thumbValue(actions.turn.x);
      pad.thumbRightY = thumbValue(actions.turn.y);

      if (navigation == VrMenuNavigation::HotKeyWheel) {
        pad.buttons |= hotKeyWheelButtons(actions.move);
        return pad;
      }

      // The sticks are passed on as well, for menus that read them directly
      // (lockpicking: the left stick turns the pin, the right one the
      // screwdriver). The interface treats the left stick and the D-pad as
      // one direction, so a list does not move twice.
      pad.buttons    |= navigateButtons(actions.move);
      pad.thumbLeftX  = thumbValue(actions.move.x);
      pad.thumbLeftY  = thumbValue(actions.move.y);
      return pad;
    }

    for (const ActionControl& entry : GameControls) {
      if (actions.isPressed(entry.action))
        vrPressGameControl(pad, entry.control, bindings);
    }

    if (actions.isPressed(VrAction::Pause))
      pad.buttons |= VrGamepadButton::Start;

    pad.thumbLeftX = thumbValue(actions.move.x);
    pad.thumbLeftY = thumbValue(actions.move.y);
    return pad;
  }


  void vrPressGameControl(
          VrPadState&           pad,
          uint32_t              control,
    const uint8_t*              bindings) {
    if (bindings && control < VrGameControl::Count)
      pressInput(pad, bindings[control]);
  }


  uint32_t vrButtonIconForInput(uint8_t input) {
    // HUDMainMenu::SetInfoForRef handles the thumbsticks' buttons, the
    // face buttons, the shoulder buttons and the triggers
    switch (input) {
      case 0x08: return 17;
      case 0x09: return 18;
      case 0x0A: return 9;
      case 0x0B: return 10;
      case 0x0C: return 11;
      case 0x0D: return 12;
      case 0x0E: return 16;
      case 0x0F: return 15;
      case 0x10: return 13;
      case 0x11: return 14;
      default:   return 0;
    }
  }


  void vrApplyMinigamePad(VrPadState& pad, const VrMinigamePad& minigame) {
    if (minigame.noShoulders)
      pad.buttons &= ~uint16_t(VrGamepadButton::LeftShoulder | VrGamepadButton::RightShoulder);

    pad.buttons |= minigame.buttons;

    if (minigame.driveLeftX)
      pad.thumbLeftX = thumbValue(minigame.leftX);

    if (minigame.driveRightX)
      pad.thumbRightX = thumbValue(minigame.rightX);

    if (minigame.noTriggers) {
      pad.leftTrigger  = 0;
      pad.rightTrigger = 0;
    }
  }

}
