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
      { VrAction::Aim,      VrGameControl::Aim       },
      { VrAction::Activate, VrGameControl::Activate  },
      { VrAction::Jump,     VrGameControl::Jump      },
      { VrAction::Reload,   VrGameControl::ReadyItem },
      { VrAction::Sneak,    VrGameControl::Sneak     },
      { VrAction::PipBoy,   VrGameControl::MenuMode  },
      { VrAction::Vats,     VrGameControl::Vats      },
      { VrAction::Grab,     VrGameControl::Grab      },
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

  }


  VrPadState vrComputePadState(
    const VrActionState&        actions,
          VrInputContext        context,
    const uint8_t*              bindings) {
    VrPadState pad;

    if (context == VrInputContext::Menu) {
      for (const ActionButton& entry : MenuButtons) {
        if (actions.isPressed(entry.action))
          pad.buttons |= entry.button;
      }

      // The sticks are passed on as well, for menus that read them directly
      // (lockpicking: the left stick turns the pin, the right one the
      // screwdriver). The interface treats the left stick and the D-pad as
      // one direction, so a list does not move twice.
      pad.buttons    |= navigateButtons(actions.move);
      pad.thumbLeftX  = thumbValue(actions.move.x);
      pad.thumbLeftY  = thumbValue(actions.move.y);
      pad.thumbRightX = thumbValue(actions.turn.x);
      pad.thumbRightY = thumbValue(actions.turn.y);
      return pad;
    }

    if (bindings) {
      for (const ActionControl& entry : GameControls) {
        if (actions.isPressed(entry.action))
          pressInput(pad, bindings[entry.control]);
      }
    }

    if (actions.isPressed(VrAction::Pause))
      pad.buttons |= VrGamepadButton::Start;

    pad.thumbLeftX = thumbValue(actions.move.x);
    pad.thumbLeftY = thumbValue(actions.move.y);
    return pad;
  }

}
