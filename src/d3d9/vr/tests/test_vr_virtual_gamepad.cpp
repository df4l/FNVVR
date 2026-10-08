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
