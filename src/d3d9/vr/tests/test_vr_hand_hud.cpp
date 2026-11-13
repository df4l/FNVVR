#include <cmath>

#include "test.h"

#include "../vr_hand_hud.h"
#include "../vr_math.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  // The game's layout for a 4:3 screen without safe zone: health bar and, prompt at the bottom centre
  // compass in the bottom left corner, action points and ammunition in the
  // bottom right corner (reaching past the screen's edge), prompt at the
  // bottom centre
  const VrInterfaceRect HitPoints    = {  10.0f, 833.0f, 379.0f, 960.0f };
  const VrInterfaceRect ActionPoints = { 924.0f, 833.0f, 1310.0f, 960.0f };
  const VrInterfaceRect Info         = { 480.0f, 850.0f, 800.0f, 930.0f };
  const VrVector2       Screen    = { 1280.0f, 960.0f };

  VrControllerState controllerAt(const VrVector3& position) {
    VrControllerState controller;
    controller.isActive          = true;
    controller.gripPose.position = position;
    controller.aimPose.position  = position;
    return controller;
  }

  bool overlaps(const VrInterfaceRect& a, const VrInterfaceRect& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
  }

}


TEST_CASE(interface_size_keeps_the_short_side) {
  VrVector2 headset = vrComputeInterfaceSize(2560, 1920);
  CHECK_NEAR(headset.x, 1280.0f, 1e-3f);
  CHECK_NEAR(headset.y,  960.0f, 1e-3f);

  VrVector2 wide = vrComputeInterfaceSize(1920, 1080);
  CHECK_NEAR(wide.x, 960.0f * 16.0f / 9.0f, 1e-2f);
  CHECK_NEAR(wide.y, 960.0f, 1e-3f);

  VrVector2 portrait = vrComputeInterfaceSize(1080, 1920);
  CHECK_NEAR(portrait.x, 1280.0f, 1e-3f);
  CHECK_NEAR(portrait.y, 1280.0f * 16.0f / 9.0f, 1e-2f);

  // A Quest 2 eye drawn at its own aspect
  VrVector2 eye = vrComputeInterfaceSize(2244, 2352);
  CHECK_NEAR(eye.x, 1280.0f, 1e-3f);
  CHECK_NEAR(eye.y, 1280.0f * 2352.0f / 2244.0f, 1e-2f);

  // The game's rule for a square screen
  VrVector2 square = vrComputeInterfaceSize(2000, 2000);
  CHECK_NEAR(square.x, 1280.0f, 1e-3f);
  CHECK_NEAR(square.y,  960.0f, 1e-3f);
}


TEST_CASE(hand_hud_crops_hold_each_element) {
  VrHandHudCrops crops = vrComputeHandHudCrops(HitPoints, ActionPoints, Info, Screen);

  // The health bar's group with a margin, inside the screen
  CHECK_NEAR(crops.wrist.left,     0.0f, 1e-3f);
  CHECK_NEAR(crops.wrist.top,    821.0f, 1e-3f);
  CHECK_NEAR(crops.wrist.right,  391.0f, 1e-3f);
  CHECK_NEAR(crops.wrist.bottom, 960.0f, 1e-3f);

  // The action points' group with a wider margin on the left, inside the screen
  CHECK_NEAR(crops.actionPoints.left,    874.0f, 1e-3f);
  CHECK_NEAR(crops.actionPoints.top,     821.0f, 1e-3f);
  CHECK_NEAR(crops.actionPoints.right,  1280.0f, 1e-3f);
  CHECK_NEAR(crops.actionPoints.bottom,  960.0f, 1e-3f);

  // Centred on the prompt, from above its group to the bottom of the screen
  CHECK_NEAR(crops.prompt.left,   440.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.right,  840.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.top,    770.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.bottom, 960.0f, 1e-3f);

  CHECK(!overlaps(crops.wrist, crops.prompt));
  CHECK(!overlaps(crops.actionPoints, crops.prompt));
  CHECK(!overlaps(crops.wrist, crops.actionPoints));
}


TEST_CASE(hand_hud_prompt_crop_avoids_the_corners) {
  // A wide prompt group would reach into both corners
  VrInterfaceRect wideInfo = { 300.0f, 850.0f, 980.0f, 930.0f };
  VrHandHudCrops crops = vrComputeHandHudCrops(HitPoints, ActionPoints, wideInfo, Screen);

  CHECK(!overlaps(crops.wrist, crops.prompt));
  CHECK(!overlaps(crops.actionPoints, crops.prompt));
  CHECK_NEAR(crops.prompt.left,  406.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.right, 874.0f, 1e-3f);
  CHECK(!crops.prompt.isEmpty());

  // Closer to the action points' corner, which alone narrows it
  VrInterfaceRect rightInfo = { 500.0f, 850.0f, 900.0f, 930.0f };
  crops = vrComputeHandHudCrops(HitPoints, ActionPoints, rightInfo, Screen);

  CHECK(!overlaps(crops.actionPoints, crops.prompt));
  CHECK_NEAR(crops.prompt.left,  526.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.right, 874.0f, 1e-3f);
}


TEST_CASE(text_rect_follows_the_alignment) {
  VrInterfaceRect left = vrComputeTextRect({ 100.0f, 50.0f }, { 40.0f, 20.0f }, VrTextAlign::Left);
  CHECK_NEAR(left.left,   100.0f, 1e-3f);
  CHECK_NEAR(left.right,  140.0f, 1e-3f);
  CHECK_NEAR(left.top,     50.0f, 1e-3f);
  CHECK_NEAR(left.bottom,  70.0f, 1e-3f);

  VrInterfaceRect centre = vrComputeTextRect({ 100.0f, 50.0f }, { 40.0f, 20.0f }, VrTextAlign::Centre);
  CHECK_NEAR(centre.left,   80.0f, 1e-3f);
  CHECK_NEAR(centre.right, 120.0f, 1e-3f);

  VrInterfaceRect right = vrComputeTextRect({ 100.0f, 50.0f }, { 40.0f, 20.0f }, VrTextAlign::Right);
  CHECK_NEAR(right.left,   60.0f, 1e-3f);
  CHECK_NEAR(right.right, 100.0f, 1e-3f);
}


TEST_CASE(united_rects_ignore_empty_ones) {
  VrInterfaceRect a = { 10.0f, 20.0f, 30.0f, 40.0f };
  VrInterfaceRect b = { 0.0f, 25.0f, 20.0f, 60.0f };

  VrInterfaceRect both = vrUniteRects(a, b);
  CHECK_NEAR(both.left,    0.0f, 1e-3f);
  CHECK_NEAR(both.top,    20.0f, 1e-3f);
  CHECK_NEAR(both.right,  30.0f, 1e-3f);
  CHECK_NEAR(both.bottom, 60.0f, 1e-3f);

  VrInterfaceRect alone = vrUniteRects(VrInterfaceRect(), a);
  CHECK_NEAR(alone.left,  10.0f, 1e-3f);
  CHECK_NEAR(alone.right, 30.0f, 1e-3f);
  CHECK_NEAR(vrUniteRects(a, VrInterfaceRect()).bottom, 40.0f, 1e-3f);
}


TEST_CASE(hand_hud_prompt_crop_without_measure_is_one_part) {
  VrHandHudCrops crops = vrComputeHandHudCrops(HitPoints, ActionPoints, Info, Screen);

  CHECK(crops.promptPartCount == 1);
  CHECK_NEAR(crops.promptParts[0].left,  crops.prompt.left,  1e-3f);
  CHECK_NEAR(crops.promptParts[0].right, crops.prompt.right, 1e-3f);
}


TEST_CASE(hand_hud_measured_prompt_keeps_a_long_first_line) {
  // An activator's own action, wider than the group, above the corners,
  // and the item lines below it, level with the corners
  VrInterfaceRect infoContent   = { 300.0f, 780.0f, 980.0f, 900.0f };
  VrInterfaceRect healthContent = {  14.0f, 835.0f, 380.0f, 958.0f };
  VrInterfaceRect weaponContent = { 880.0f, 836.0f, 1280.0f, 958.0f };

  VrHandHudContent content;
  content.hitPoints    = &healthContent;
  content.actionPoints = &weaponContent;
  content.info         = &infoContent;

  VrHandHudCrops crops = vrComputeHandHudCrops(HitPoints, ActionPoints, Info, Screen, content);
  CHECK(crops.promptPartCount == 2);

  // The rows above the corners keep the whole width, with a margin
  CHECK_NEAR(crops.promptParts[0].left,   294.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[0].top,    774.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[0].right,  986.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[0].bottom, 833.0f, 1e-3f);

  // The rows level with the corners reach neither of them
  CHECK_NEAR(crops.promptParts[1].left,   391.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[1].top,    833.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[1].right,  874.0f, 1e-3f);
  CHECK_NEAR(crops.promptParts[1].bottom, 906.0f, 1e-3f);

  for (uint32_t i = 0; i < crops.promptPartCount; i++) {
    CHECK(!overlaps(crops.promptParts[i], healthContent));
    CHECK(!overlaps(crops.promptParts[i], weaponContent));
  }

  CHECK_NEAR(crops.prompt.left,   294.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.top,    774.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.right,  986.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.bottom, 906.0f, 1e-3f);

  // Without the corners' measure, their crops tell where they start
  VrHandHudContent infoOnly;
  infoOnly.info = &infoContent;
  crops = vrComputeHandHudCrops(HitPoints, ActionPoints, Info, Screen, infoOnly);
  CHECK(crops.promptPartCount == 2);
  CHECK_NEAR(crops.promptParts[0].bottom, 821.0f, 1e-3f);
  CHECK(!overlaps(crops.promptParts[1], crops.wrist));
  CHECK(!overlaps(crops.promptParts[1], crops.actionPoints));
}


TEST_CASE(hand_hud_measured_prompt_above_the_corners_is_one_part) {
  VrInterfaceRect infoContent = { 500.0f, 700.0f, 780.0f, 800.0f };

  VrHandHudContent content;
  content.info = &infoContent;

  VrHandHudCrops crops = vrComputeHandHudCrops(HitPoints, ActionPoints, Info, Screen, content);
  CHECK(crops.promptPartCount == 1);
  CHECK_NEAR(crops.prompt.left,   494.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.top,    694.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.right,  786.0f, 1e-3f);
  CHECK_NEAR(crops.prompt.bottom, 806.0f, 1e-3f);
}


TEST_CASE(prompt_image_centres_its_parts_and_only_grows) {
  const VrInterfaceRect parts[] = {
    { 294.0f, 774.0f, 986.0f, 833.0f },
    { 391.0f, 833.0f, 874.0f, 906.0f } };

  VrPromptImage image = vrComputePromptImage(parts, 2, { 800.0f, 100.0f });
  CHECK_NEAR(image.size.x, 800.0f, 1e-3f);
  CHECK_NEAR(image.size.y, 132.0f, 1e-3f);

  CHECK_NEAR(image.targets[0].left,    54.0f, 1e-3f);
  CHECK_NEAR(image.targets[0].top,      0.0f, 1e-3f);
  CHECK_NEAR(image.targets[0].right,  746.0f, 1e-3f);
  CHECK_NEAR(image.targets[0].bottom,  59.0f, 1e-3f);

  CHECK_NEAR(image.targets[1].left,   151.0f, 1e-3f);
  CHECK_NEAR(image.targets[1].top,     59.0f, 1e-3f);
  CHECK_NEAR(image.targets[1].right,  634.0f, 1e-3f);
  CHECK_NEAR(image.targets[1].bottom, 132.0f, 1e-3f);

  // A taller image left by an earlier prompt centres it vertically too
  image = vrComputePromptImage(parts, 2, { 0.0f, 200.0f });
  CHECK_NEAR(image.size.x, 692.0f, 1e-3f);
  CHECK_NEAR(image.size.y, 200.0f, 1e-3f);
  CHECK_NEAR(image.targets[0].left,  0.0f, 1e-3f);
  CHECK_NEAR(image.targets[0].top,  34.0f, 1e-3f);
}


TEST_CASE(wrist_stack_puts_the_action_points_below_the_health_bar) {
  VrInterfaceRect health = {   0.0f, 821.0f,  391.0f, 960.0f };
  VrInterfaceRect weapon = { 874.0f, 821.0f, 1280.0f, 960.0f };
  VrWristStack stack = vrComputeWristStack(health, weapon);

  CHECK_NEAR(stack.size.x, 406.0f, 1e-3f);
  CHECK_NEAR(stack.size.y, 278.0f, 1e-3f);

  // The narrower health bar is centred, on a whole unit
  CHECK_NEAR(stack.health.left,     7.0f, 1e-3f);
  CHECK_NEAR(stack.health.top,      0.0f, 1e-3f);
  CHECK_NEAR(stack.health.width(), 391.0f, 1e-3f);
  CHECK_NEAR(stack.health.height(), 139.0f, 1e-3f);

  CHECK_NEAR(stack.actionPoints.left,     0.0f, 1e-3f);
  CHECK_NEAR(stack.actionPoints.top,    139.0f, 1e-3f);
  CHECK_NEAR(stack.actionPoints.right,  406.0f, 1e-3f);
  CHECK_NEAR(stack.actionPoints.bottom, 278.0f, 1e-3f);

  // The image's centre is half the action points' height below the health bar's
  CHECK_NEAR(stack.healthDrop(), 69.5f, 1e-3f);

  // Without action points, only the health bar is left
  VrWristStack alone = vrComputeWristStack(health, VrInterfaceRect());
  CHECK_NEAR(alone.size.x, 391.0f, 1e-3f);
  CHECK_NEAR(alone.size.y, 139.0f, 1e-3f);
  CHECK_NEAR(alone.healthDrop(), 0.0f, 1e-3f);
}


TEST_CASE(wrist_panel_lies_on_the_back_of_the_hand) {
  VrControllerState hand = controllerAt({ 0.0f, 1.0f, -0.3f });
  VrPose pose = vrComputeWristPanelPose(hand, 0.0f);

  // Right of, above and slightly behind the grip
  CHECK_NEAR(pose.position.x,  0.05f, 1e-5f);
  CHECK_NEAR(pose.position.y,  1.02f, 1e-5f);
  CHECK_NEAR(pose.position.z, -0.28f, 1e-5f);

  // Faces the hand's right axis raised by the face-up angle
  VrVector3 normal = vrRotate(pose.orientation, { 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(normal.x, std::cos(VrHandHudLayout::WristFaceUpAngle), 1e-5f);
  CHECK_NEAR(normal.y, std::sin(VrHandHudLayout::WristFaceUpAngle), 1e-5f);
  CHECK_NEAR(normal.z, 0.0f, 1e-5f);

  // Its up axis runs towards the fingers
  VrVector3 up = vrRotate(pose.orientation, { 0.0f, 1.0f, 0.0f });
  CHECK_NEAR(up.x,  0.0f, 1e-5f);
  CHECK_NEAR(up.y,  0.0f, 1e-5f);
  CHECK_NEAR(up.z, -1.0f, 1e-5f);

  // Turning the hand turns the panel with it
  hand.aimPose.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 1.0f);
  VrPose turned = vrComputeWristPanelPose(hand, 0.0f);
  VrVector3 turnedUp = vrRotate(turned.orientation, { 0.0f, 1.0f, 0.0f });
  VrVector3 forward  = vrRotate(hand.aimPose.orientation, { 0.0f, 0.0f, -1.0f });
  CHECK_NEAR(vrDot(turnedUp, forward), 1.0f, 1e-5f);

  // A taller panel hangs towards the forearm, the health bar stays in place
  VrPose dropped = vrComputeWristPanelPose(hand, 0.03f);
  VrVector3 moved = dropped.position - turned.position;
  CHECK_NEAR(vrDot(moved, forward), -0.03f, 1e-5f);
  CHECK_NEAR(vrLength(moved), 0.03f, 1e-5f);
}


TEST_CASE(prompt_panel_faces_the_head_above_the_fingertip) {
  VrPose head = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.6f, 0.0f } };
  VrControllerState hand = controllerAt({ 0.2f, 1.0f, -0.4f });

  VrPose pose = vrComputePromptPanelPose(hand, head);

  CHECK_NEAR(pose.position.x, 0.2f, 1e-5f);
  CHECK_NEAR(pose.position.y, 1.0f + VrHandHudLayout::PromptLift, 1e-5f);
  CHECK_NEAR(pose.position.z, -0.4f - VrHandHudLayout::FingertipOffset.x, 1e-5f);

  VrVector3 normal = vrRotate(pose.orientation, { 0.0f, 0.0f, 1.0f });
  VrVector3 toHead = head.position - pose.position;
  CHECK_NEAR(vrDot(normal, toHead), vrLength(toHead), 1e-4f);

  // Upright: its horizontal axis stays level
  VrVector3 right = vrRotate(pose.orientation, { 1.0f, 0.0f, 0.0f });
  CHECK_NEAR(right.y, 0.0f, 1e-5f);
}


TEST_CASE(wrist_gaze_needs_the_display_turned_towards_the_eyes) {
  VrPose head = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.6f, 0.0f } };

  // In front of and below the eyes, the head tilted down to it
  head.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -0.6f);

  VrPose facing;
  facing.position    = { 0.0f, 1.3f, -0.35f };
  facing.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -0.6f);

  VrWristGaze gaze;
  CHECK(!gaze.update(&head, &facing, Frame));

  bool shown = false;

  for (float t = 0.0f; t < VrWristGaze::ShowDelay + Frame; t += Frame)
    shown = gaze.update(&head, &facing, Frame);

  CHECK(shown);

  // Turned away, as the back of the hand is while the weapon is aimed,
  // the display stays shown for a moment and then disappears
  VrPose away = facing;
  away.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 2.0f);
  CHECK(gaze.update(&head, &away, Frame));

  for (float t = 0.0f; t < VrWristGaze::HideDelay + Frame; t += Frame)
    shown = gaze.update(&head, &away, Frame);

  CHECK(!shown);

  // A display turned away is never shown, nor an untracked hand
  VrWristGaze other;

  for (float t = 0.0f; t < 1.0f; t += Frame) {
    CHECK(!other.update(&head, &away, Frame));
    CHECK(!other.update(&head, nullptr, Frame));
  }
}


TEST_CASE(prompt_hand_is_the_nearest_with_a_margin) {
  std::array<VrControllerState, VrHandCount> hands = {
    controllerAt({ -0.3f, 1.0f, -0.3f }),
    controllerAt({  0.3f, 1.0f, -0.3f }) };

  VrPromptHand chooser;
  VrHand hand = VrHand::Right;

  // Target on the left
  CHECK(chooser.update(hands, { -1.0f, 1.0f, -1.0f }, hand));
  CHECK(hand == VrHand::Left);

  // Nearly in the middle: the prompt stays on the left hand
  CHECK(chooser.update(hands, { 0.02f, 1.0f, -2.0f }, hand));
  CHECK(hand == VrHand::Left);

  // Clearly on the right
  CHECK(chooser.update(hands, { 1.0f, 1.0f, -1.0f }, hand));
  CHECK(hand == VrHand::Right);

  // Only the tracked hand can show it
  hands[uint32_t(VrHand::Right)].isActive = false;
  CHECK(chooser.update(hands, { 1.0f, 1.0f, -1.0f }, hand));
  CHECK(hand == VrHand::Left);

  hands[uint32_t(VrHand::Left)].isActive = false;
  CHECK(!chooser.update(hands, { 1.0f, 1.0f, -1.0f }, hand));
}


TEST_CASE(activate_hand_is_the_one_that_finds_a_target) {
  std::array<VrHandPickResult, VrHandCount> picks;
  VrHandPickResult& left  = picks[uint32_t(VrHand::Left)];
  VrHandPickResult& right = picks[uint32_t(VrHand::Right)];

  left.tracked  = true;
  right.tracked = true;

  // Neither finds anything: the previous hand stays
  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Right);
  CHECK(vrChooseActivateHand(picks, VrHand::Left) == VrHand::Left);

  // Only the left hand points at something
  left.hit = true;
  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Left);

  // Both do: the previous hand keeps the target
  right.hit = true;
  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Right);
  CHECK(vrChooseActivateHand(picks, VrHand::Left) == VrHand::Left);

  // A ray that reaches its target wins over one that only passes near a body
  right.fuzzy = true;
  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Left);
  left.fuzzy = true;
  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Right);
}


TEST_CASE(activate_hand_is_a_tracked_one) {
  std::array<VrHandPickResult, VrHandCount> picks;
  picks[uint32_t(VrHand::Left)].tracked = true;

  CHECK(vrChooseActivateHand(picks, VrHand::Right) == VrHand::Left);
  CHECK(vrChooseActivateHand(picks, VrHand::Left) == VrHand::Left);

  picks[uint32_t(VrHand::Left)].tracked  = false;
  picks[uint32_t(VrHand::Right)].tracked = true;
  CHECK(vrChooseActivateHand(picks, VrHand::Left) == VrHand::Right);
}
