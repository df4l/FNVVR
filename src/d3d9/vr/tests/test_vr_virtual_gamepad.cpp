#include "test.h"

#include "../vr_virtual_gamepad.h"

using namespace dxvk;


TEST_CASE(no_keys_give_no_buttons) {
  CHECK(vrGamepadButtons(VrGamepadKeys()) == 0);
}


TEST_CASE(keys_map_to_xinput_bits) {
  VrGamepadKeys keys;
  keys.dpadUp = true;
  keys.a      = true;
  keys.start  = true;

  CHECK(vrGamepadButtons(keys) == (0x0001 | 0x1000 | 0x0010));
}


TEST_CASE(opposite_directions_cancel) {
  VrGamepadKeys keys;
  keys.dpadLeft  = true;
  keys.dpadRight = true;
  keys.dpadDown  = true;

  CHECK(vrGamepadButtons(keys) == VrGamepadButton::DpadDown);
}


TEST_CASE(a_tap_is_held_for_several_polls) {
  VrGamepadHold hold;

  CHECK(hold.update(VrGamepadButton::A) == VrGamepadButton::A);

  for (uint32_t i = 1; i < VrGamepadHold::MinPolls; i++)
    CHECK(hold.update(0) == VrGamepadButton::A);

  CHECK(hold.update(0) == 0);
}


TEST_CASE(a_held_key_stays_down_and_buttons_are_independent) {
  VrGamepadHold hold;

  for (uint32_t i = 0; i < 10; i++)
    CHECK(hold.update(VrGamepadButton::DpadDown) == VrGamepadButton::DpadDown);

  CHECK(hold.update(VrGamepadButton::A) == (VrGamepadButton::A | VrGamepadButton::DpadDown));
}
