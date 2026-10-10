#include <cmath>

#include "test.h"

#include "../vr_math.h"
#include "../vr_menu_pointer.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  // A 2 x 1 m panel, 2 m in front of the origin, facing it
  VrPanelPlacement makePanel() {
    VrPanelPlacement panel;
    panel.pose.position = { 0.0f, 1.5f, -2.0f };
    panel.width  = 2.0f;
    panel.height = 1.0f;
    return panel;
  }

  // A controller at the origin, at head height, pointing at a spot
  VrControllerState pointingAt(float x, float y) {
    VrControllerState controller;
    controller.isActive = true;
    controller.aimPose.position = { 0.0f, 1.5f, 0.0f };

    VrVector3 target = { x, y, -2.0f };
    VrVector3 direction = target - controller.aimPose.position;
    float yaw   = std::atan2(-direction.x, -direction.z);
    float pitch = std::atan2(direction.y, std::hypot(direction.x, direction.z));

    controller.aimPose.orientation =
      vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yaw) *
      vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, pitch);
    return controller;
  }

  VrControllerState pointingAway() {
    VrControllerState controller = pointingAt(0.0f, 1.5f);
    controller.aimPose.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f);
    return controller;
  }

  VrPointerInput frame(const VrPanelPlacement& panel, const VrControllerState& left, const VrControllerState& right) {
    VrPointerInput input;
    input.panel = &panel;
    input.controllers[uint32_t(VrHand::Left)]  = left;
    input.controllers[uint32_t(VrHand::Right)] = right;
    input.dt = Frame;
    return input;
  }

}


TEST_CASE(ray_hits_the_panel_centre_and_corners) {
  VrPanelPlacement panel = makePanel();

  VrPanelHit centre = vrIntersectPanel(pointingAt(0.0f, 1.5f).aimPose, panel);
  CHECK(centre.hit);
  CHECK_NEAR(centre.uv.x, 0.5f, 1e-4);
  CHECK_NEAR(centre.uv.y, 0.5f, 1e-4);
  CHECK_NEAR(centre.distance, 2.0f, 1e-4);

  VrPanelHit topLeft = vrIntersectPanel(pointingAt(-0.9f, 1.9f).aimPose, panel);
  CHECK(topLeft.hit);
  CHECK_NEAR(topLeft.uv.x, 0.05f, 1e-4);
  CHECK_NEAR(topLeft.uv.y, 0.1f, 1e-4);
}


TEST_CASE(ray_misses_outside_and_behind_the_panel) {
  VrPanelPlacement panel = makePanel();

  CHECK(!vrIntersectPanel(pointingAt(1.2f, 1.5f).aimPose, panel).hit);
  CHECK(!vrIntersectPanel(pointingAway().aimPose, panel).hit);

  // From behind the panel, looking at its back
  VrPose behind;
  behind.position = { 0.0f, 1.5f, -3.0f };
  behind.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f);
  CHECK(!vrIntersectPanel(behind, panel).hit);
}


TEST_CASE(pointer_drives_the_cursor_while_pointing_at_the_panel) {
  VrPanelPlacement panel = makePanel();
  VrMenuPointer pointer;

  VrPointerState state = pointer.update(frame(panel, pointingAway(), pointingAt(0.5f, 1.5f)));
  CHECK(state.cursorActive);
  CHECK(state.triggersTaken);
  CHECK_NEAR(state.cursor.x, 0.75f, 1e-4);
  CHECK(state.beams[uint32_t(VrHand::Right)].visible);
  CHECK(state.beams[uint32_t(VrHand::Right)].active);
  CHECK(!state.beams[uint32_t(VrHand::Left)].visible);

  VrPointerInput input = frame(panel, pointingAway(), pointingAt(0.5f, 1.5f));
  input.triggers[uint32_t(VrHand::Right)] = true;
  CHECK(pointer.update(input).button);

  state = pointer.update(frame(panel, pointingAway(), pointingAway()));
  CHECK(!state.cursorActive);
  CHECK(!state.triggersTaken);
  CHECK(!state.beams[uint32_t(VrHand::Right)].visible);
}


TEST_CASE(pointer_switches_hands_on_a_trigger_press) {
  VrPanelPlacement panel = makePanel();
  VrMenuPointer pointer;

  VrPointerState state = pointer.update(frame(panel, pointingAt(-0.5f, 1.5f), pointingAt(0.5f, 1.5f)));
  CHECK(state.beams[uint32_t(VrHand::Right)].active);
  CHECK(!state.beams[uint32_t(VrHand::Left)].active);
  CHECK(state.beams[uint32_t(VrHand::Left)].visible);

  VrPointerInput input = frame(panel, pointingAt(-0.5f, 1.5f), pointingAt(0.5f, 1.5f));
  input.triggers[uint32_t(VrHand::Left)] = true;
  state = pointer.update(input);
  CHECK(state.beams[uint32_t(VrHand::Left)].active);
  CHECK(state.button);
  CHECK_NEAR(state.cursor.x, 0.25f, 1e-4);

  // The other hand takes over when the active one leaves the panel
  state = pointer.update(frame(panel, pointingAway(), pointingAt(0.5f, 1.5f)));
  CHECK(state.beams[uint32_t(VrHand::Right)].active);
}


TEST_CASE(pointer_gives_way_to_the_gamepad_until_the_laser_moves) {
  VrPanelPlacement panel = makePanel();
  VrMenuPointer pointer;

  CHECK(pointer.update(frame(panel, pointingAway(), pointingAt(0.0f, 1.5f))).cursorActive);

  VrPointerInput input = frame(panel, pointingAway(), pointingAt(0.0f, 1.5f));
  input.padUsed = true;
  VrPointerState state = pointer.update(input);
  CHECK(!state.cursorActive);
  CHECK(!state.triggersTaken);
  CHECK(state.beams[uint32_t(VrHand::Right)].visible);

  // A small movement does not take the menus back, a larger one does
  CHECK(!pointer.update(frame(panel, pointingAway(), pointingAt(0.02f, 1.5f))).cursorActive);
  CHECK(pointer.update(frame(panel, pointingAway(), pointingAt(0.1f, 1.5f))).cursorActive);

  // A trigger press takes them back without movement
  input.padUsed = true;
  pointer.update(input);
  input.padUsed = false;
  input.triggers[uint32_t(VrHand::Right)] = true;
  CHECK(pointer.update(input).cursorActive);
}


TEST_CASE(pointer_wheel_scrolls_one_notch_then_repeats) {
  VrPanelPlacement panel = makePanel();
  VrMenuPointer pointer;

  VrPointerInput input = frame(panel, pointingAway(), pointingAt(0.0f, 1.5f));
  input.wheel = -1.0f;

  int32_t total = 0;
  uint32_t notches = 0;

  for (float t = 0.0f; t < 0.35f; t += Frame) {
    int32_t wheel = pointer.update(input).wheel;
    total += wheel;
    notches += wheel ? 1 : 0;
  }

  CHECK(notches == 3);
  CHECK(total == -3 * VrMenuPointer::WheelNotch);

  input.wheel = 0.2f;
  CHECK(pointer.update(input).wheel == 0);
}


TEST_CASE(pointer_is_off_without_a_panel) {
  VrMenuPointer pointer;

  VrPointerInput input;
  input.controllers[uint32_t(VrHand::Right)] = pointingAt(0.0f, 1.5f);
  input.triggers[uint32_t(VrHand::Right)] = true;

  VrPointerState state = pointer.update(input);
  CHECK(!state.cursorActive);
  CHECK(!state.button);
  CHECK(!state.beams[uint32_t(VrHand::Right)].visible);
}
