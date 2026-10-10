#include <cstring>

#include "test.h"

#include "../vr_controller_pad.h"
#include "../vr_virtual_gamepad.h"

using namespace dxvk;

namespace {

  // The vanilla Xbox layout: RT attacks, LT aims, A activates, Y jumps,
  // X reloads, B opens the Pip-Boy
  struct Bindings {
    uint8_t codes[VrGameControl::Count];

    Bindings() {
      std::memset(codes, 0xFF, sizeof(codes));
      codes[VrGameControl::Attack]    = 0x11;
      codes[VrGameControl::Aim]       = 0x10;
      codes[VrGameControl::Activate]  = 0x0A;
      codes[VrGameControl::Jump]      = 0x0D;
      codes[VrGameControl::ReadyItem] = 0x0C;
      codes[VrGameControl::MenuMode]  = 0x0B;
      codes[VrGameControl::Sneak]     = 0x08;
      codes[VrGameControl::Vats]      = 0x0F;
    }
  };

}


TEST_CASE(game_actions_press_the_bound_inputs) {
  Bindings bindings;
  VrActionState actions;
  actions.setPressed(VrAction::Attack, true);
  actions.setPressed(VrAction::Jump, true);
  actions.setPressed(VrAction::Sneak, true);
  actions.setPressed(VrAction::Vats, true);

  VrPadState pad = vrComputePadState(actions, VrInputContext::Game, bindings.codes);
  CHECK(pad.rightTrigger == 0xFF);
  CHECK(pad.leftTrigger == 0);
  CHECK(pad.buttons == (VrGamepadButton::Y | VrGamepadButton::LeftThumb | VrGamepadButton::LeftShoulder));
}


TEST_CASE(two_hand_grip_does_not_aim) {
  Bindings bindings;
  VrActionState actions;
  actions.setPressed(VrAction::TwoHandGrip, true);

  VrPadState pad = vrComputePadState(actions, VrInputContext::Game, bindings.codes);
  CHECK(pad.buttons == 0);
  CHECK(pad.leftTrigger == 0 && pad.rightTrigger == 0);
}


TEST_CASE(unbound_controls_press_nothing) {
  Bindings bindings;
  VrActionState actions;
  actions.setPressed(VrAction::Grab, true);

  VrPadState pad = vrComputePadState(actions, VrInputContext::Game, bindings.codes);
  CHECK(pad.buttons == 0);
  CHECK(pad.leftTrigger == 0 && pad.rightTrigger == 0);
}


TEST_CASE(pause_is_start_and_move_is_the_left_stick) {
  VrActionState actions;
  actions.setPressed(VrAction::Pause, true);
  actions.move = { -1.0f, 0.5f };
  actions.turn = { 1.0f, 0.0f };

  VrPadState pad = vrComputePadState(actions, VrInputContext::Game, nullptr);
  CHECK(pad.buttons == VrGamepadButton::Start);
  CHECK(pad.thumbLeftX == -32767);
  CHECK(pad.thumbLeftY == 16384);
  CHECK(pad.thumbRightX == 0);
}


TEST_CASE(menu_actions_press_the_menu_buttons) {
  VrActionState actions;
  actions.setPressed(VrAction::MenuSelect, true);
  actions.setPressed(VrAction::MenuNext, true);

  // Game actions are ignored in menus
  actions.setPressed(VrAction::Attack, true);

  VrPadState pad = vrComputePadState(actions, VrInputContext::Menu, nullptr);
  CHECK(pad.buttons == (VrGamepadButton::A | VrGamepadButton::RightShoulder));
  CHECK(pad.rightTrigger == 0);
}


TEST_CASE(menu_triggers_press_the_gamepad_triggers) {
  VrActionState actions;
  actions.setPressed(VrAction::MenuRightTrigger, true);

  VrPadState pad = vrComputePadState(actions, VrInputContext::Menu, nullptr);
  CHECK(pad.rightTrigger == 0xFF);
  CHECK(pad.leftTrigger == 0);
  CHECK(pad.buttons == 0);

  actions.setPressed(VrAction::MenuLeftTrigger, true);
  CHECK(vrComputePadState(actions, VrInputContext::Menu, nullptr).leftTrigger == 0xFF);
}


TEST_CASE(menu_navigation_presses_the_dominant_dpad_direction) {
  VrActionState actions;

  actions.move = { 0.2f, 0.3f };
  CHECK(vrComputePadState(actions, VrInputContext::Menu, nullptr).buttons == 0);

  actions.move = { 0.6f, -0.8f };
  CHECK(vrComputePadState(actions, VrInputContext::Menu, nullptr).buttons == VrGamepadButton::DpadDown);

  actions.move = { -0.9f, 0.4f };
  VrPadState pad = vrComputePadState(actions, VrInputContext::Menu, nullptr);
  CHECK(pad.buttons == VrGamepadButton::DpadLeft);
}


TEST_CASE(menu_sticks_are_passed_on) {
  VrActionState actions;
  actions.move = { -1.0f, 0.5f };
  actions.turn = { 0.5f, -1.0f };

  VrPadState pad = vrComputePadState(actions, VrInputContext::Menu, nullptr);
  CHECK(pad.thumbLeftX == -32767);
  CHECK(pad.thumbLeftY == 16384);
  CHECK(pad.thumbRightX == 16384);
  CHECK(pad.thumbRightY == -32767);
}
