#include "test.h"

#include "../vr_game_space.h"
#include "../vr_roomscale_rules.h"

using namespace dxvk;

namespace {

  constexpr float MaxStep = 10.0f;

}


TEST_CASE(roomscale_capsule_stays_within_the_dead_zone) {
  VrVector2 still = vrRoomscaleWantedMove({ 3.0f, 4.0f }, 5.0f);
  CHECK_NEAR(still.x, 0.0f, 1e-6);
  CHECK_NEAR(still.y, 0.0f, 1e-6);

  // Ten away with a dead zone of five: half the way
  VrVector2 move = vrRoomscaleWantedMove({ 6.0f, 8.0f }, 5.0f);
  CHECK_NEAR(move.x, 3.0f, 1e-5);
  CHECK_NEAR(move.y, 4.0f, 1e-5);
}


TEST_CASE(roomscale_limits_a_step) {
  VrVector2 limited = vrRoomscaleLimitMove({ 30.0f, 40.0f }, MaxStep);
  CHECK_NEAR(limited.x, 6.0f, 1e-5);
  CHECK_NEAR(limited.y, 8.0f, 1e-5);

  VrVector2 kept = vrRoomscaleLimitMove({ 3.0f, 4.0f }, MaxStep);
  CHECK_NEAR(kept.x, 3.0f, 1e-6);
  CHECK_NEAR(kept.y, 4.0f, 1e-6);
}


TEST_CASE(roomscale_without_stick_counts_what_the_capsule_moved) {
  // Sliding along a wall: not the way that was asked, all of it counts
  VrVector2 achieved = vrRoomscaleAchievedMove({ 2.0f, 0.0f }, { }, { 2.0f, 2.0f }, true, MaxStep);
  CHECK_NEAR(achieved.x, 2.0f, 1e-6);
  CHECK_NEAR(achieved.y, 0.0f, 1e-6);

  // The capsule goes on a little after the head's move
  VrVector2 after = vrRoomscaleAchievedMove({ 1.0f, 0.0f }, { }, { }, true, MaxStep);
  CHECK_NEAR(after.x, 1.0f, 1e-6);
}


TEST_CASE(roomscale_leaves_other_moves_to_the_view) {
  // Pushed with no head's move lately: the view goes along
  VrVector2 pushed = vrRoomscaleAchievedMove({ 3.0f, 0.0f }, { }, { }, false, MaxStep);
  CHECK_NEAR(pushed.x, 0.0f, 1e-6);

  // A teleport during a step is not the head's either
  VrVector2 teleported = vrRoomscaleAchievedMove({ 500.0f, 0.0f }, { }, { 2.0f, 0.0f }, true, MaxStep);
  CHECK_NEAR(teleported.x, 0.0f, 1e-6);
}


TEST_CASE(roomscale_with_stick_keeps_the_head_out_of_it) {
  // Both moves went through
  VrVector2 both = vrRoomscaleAchievedMove({ 5.0f, 1.0f }, { 5.0f, 0.0f }, { 0.0f, 1.0f }, true, MaxStep);
  CHECK_NEAR(both.x, 0.0f, 1e-6);
  CHECK_NEAR(both.y, 1.0f, 1e-6);

  // A wall stopped the stick: the head did not go backwards
  VrVector2 stopped = vrRoomscaleAchievedMove({ 0.0f, 0.0f }, { 5.0f, 0.0f }, { 1.0f, 0.0f }, true, MaxStep);
  CHECK_NEAR(stopped.x, 0.0f, 1e-6);

  // Never more than was asked
  VrVector2 limited = vrRoomscaleAchievedMove({ 9.0f, 0.0f }, { 5.0f, 0.0f }, { 1.0f, 0.0f }, true, MaxStep);
  CHECK_NEAR(limited.x, 1.0f, 1e-6);
}


TEST_CASE(roomscale_tells_a_blocked_capsule) {
  CHECK(vrRoomscaleIsBlocked({ 0.0f, 0.0f }, { 2.0f, 0.0f }));
  CHECK(!vrRoomscaleIsBlocked({ 2.0f, 0.0f }, { 2.0f, 0.0f }));
  CHECK(!vrRoomscaleIsBlocked({ 0.0f, 0.0f }, { 0.0f, 0.0f }));
}


TEST_CASE(roomscale_view_stops_at_the_head_room) {
  VrVector2 free = vrRoomscalePushback({ 3.0f, 4.0f }, 6.0f);
  CHECK_NEAR(free.x, 0.0f, 1e-6);

  VrVector2 back = vrRoomscalePushback({ 6.0f, 8.0f }, 5.0f);
  CHECK_NEAR(back.x, 3.0f, 1e-5);
  CHECK_NEAR(back.y, 4.0f, 1e-5);
}


TEST_CASE(shifting_the_reference_moves_the_eye_back_in_the_world) {
  VrGameCameraPose base;
  base.forward = { 0.0f, 1.0f, 0.0f };
  base.up      = { 0.0f, 0.0f, 1.0f };
  base.right   = { 1.0f, 0.0f, 0.0f };

  VrPose head;
  head.position = { 1.0f, 1.7f, -2.0f };
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.8f);

  VrPose reference = vrMakeRecenterReference(head);
  reference.position = { 0.5f, 1.7f, 0.0f };

  VrVector3 before = vrComputeEyeCameraPose(base,
    vrComputeEyeInReference(reference, head), VrGameUnitsPerMetre).position;

  VrVector3 shift = { 7.0f, -14.0f, 0.0f };
  VrPose shifted = vrShiftReference(reference, base, shift, VrGameUnitsPerMetre);

  VrVector3 after = vrComputeEyeCameraPose(base,
    vrComputeEyeInReference(shifted, head), VrGameUnitsPerMetre).position;

  CHECK_NEAR(after.x, before.x - shift.x, 1e-3);
  CHECK_NEAR(after.y, before.y - shift.y, 1e-3);
  CHECK_NEAR(after.z, before.z, 1e-3);
}
