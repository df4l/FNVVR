#include "vr_virtual_gamepad.h"

namespace dxvk {

  uint16_t vrGamepadButtons(const VrGamepadKeys& keys) {
    uint16_t buttons = 0;

    if (keys.dpadUp != keys.dpadDown)
      buttons |= keys.dpadUp ? VrGamepadButton::DpadUp : VrGamepadButton::DpadDown;

    if (keys.dpadLeft != keys.dpadRight)
      buttons |= keys.dpadLeft ? VrGamepadButton::DpadLeft : VrGamepadButton::DpadRight;

    if (keys.start) buttons |= VrGamepadButton::Start;
    if (keys.back)  buttons |= VrGamepadButton::Back;
    if (keys.a)     buttons |= VrGamepadButton::A;
    if (keys.b)     buttons |= VrGamepadButton::B;
    if (keys.x)     buttons |= VrGamepadButton::X;
    if (keys.y)     buttons |= VrGamepadButton::Y;

    return buttons;
  }

}
