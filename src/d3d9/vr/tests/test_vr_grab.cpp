#include <cmath>

#include "test.h"

#include "../vr_grab.h"
#include "../vr_math.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  // Facing north (+Y), Z up
  VrGameCameraPose northHand(const VrVector3& position) {
    VrGameCameraPose hand;
    hand.forward  = { 0.0f, 1.0f, 0.0f };
    hand.up       = { 0.0f, 0.0f, 1.0f };
    hand.right    = { 1.0f, 0.0f, 0.0f };
    hand.position = position;
    return hand;
  }

  // Facing east (+X), Z up
  VrGameCameraPose eastHand(const VrVector3& position) {
    VrGameCameraPose hand;
    hand.forward  = { 1.0f, 0.0f, 0.0f };
    hand.up       = { 0.0f, 0.0f, 1.0f };
    hand.right    = { 0.0f, -1.0f, 0.0f };
    hand.position = position;
    return hand;
  }

  VrGrabHandInput input(const VrGameCameraPose* hand, bool grip, bool free = true) {
    VrGrabHandInput result;
    result.hand = hand;
    result.grip = grip;
    result.free = free;
    result.dt   = Frame;
    return result;
  }

}


TEST_CASE(grab_probe_rays_end_at_the_centre_from_the_sphere) {
  VrVector3 centre = { 10.0f, -4.0f, 100.0f };
  auto rays = vrGrabProbeRays(centre, 7.0f);

  for (const VrGrabRay& ray : rays) {
    CHECK_NEAR(vrLength(ray.to - centre), 0.0f, 1e-4f);
    CHECK_NEAR(vrLength(ray.from - centre), 7.0f, 1e-4f);
  }

  // Straight down is one of them, for an object under the hand
  bool below = false;

  for (const VrGrabRay& ray : rays)
    below = below || (ray.from.z < centre.z - 6.9f);

  CHECK(below);
}


TEST_CASE(grab_released_body_is_clear_of_the_player_horizontally) {
  VrVector3 player = { 0.0f, 0.0f, 0.0f };

  CHECK(!vrGrabClearOfPlayer({ 30.0f, 0.0f, 0.0f }, player, 42.0f));
  // High above the player is still over the capsule
  CHECK(!vrGrabClearOfPlayer({ 30.0f, 0.0f, 150.0f }, player, 42.0f));
  CHECK(vrGrabClearOfPlayer({ 30.0f, 30.0f, 0.0f }, player, 42.0f));
}


TEST_CASE(grab_press_while_free_looks_for_a_short_time) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;

  CHECK(grab.update(input(&hand, false)) == VrGrabRequest::None);
  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::Grab);

  uint32_t frames = 1;

  while (grab.update(input(&hand, true)) == VrGrabRequest::Grab)
    frames++;

  CHECK(frames >= uint32_t(VrGrabHand::GrabWindow / Frame) - 1);
  CHECK(frames <= uint32_t(VrGrabHand::GrabWindow / Frame) + 1);

  // Holding the grip longer does not grab, a new press does
  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::None);
  CHECK(grab.update(input(&hand, false)) == VrGrabRequest::None);
  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::Grab);
}


TEST_CASE(grab_press_while_busy_never_grabs) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;

  CHECK(grab.update(input(&hand, true, false)) == VrGrabRequest::None);
  // The hand becoming free with the grip still held does not grab
  CHECK(grab.update(input(&hand, true, true)) == VrGrabRequest::None);
  // Nor does a press of an untracked hand
  grab.update(input(nullptr, false));
  CHECK(grab.update(input(nullptr, true)) == VrGrabRequest::None);
}


TEST_CASE(grab_hold_lets_go_with_the_grip_or_the_hand) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });

  {
    VrGrabHand grab;
    grab.update(input(&hand, true));
    grab.hold(hand, { 0.0f, 5.0f, 0.0f });
    CHECK(grab.holding());
    CHECK(grab.update(input(&hand, true)) == VrGrabRequest::None);
    CHECK(grab.update(input(&hand, false)) == VrGrabRequest::Release);
    grab.drop();
    CHECK(!grab.holding());
  }

  {
    VrGrabHand grab;
    grab.update(input(&hand, true));
    grab.hold(hand, { 0.0f, 5.0f, 0.0f });
    CHECK(grab.update(input(&hand, true, false)) == VrGrabRequest::Release);
  }

  {
    VrGrabHand grab;
    grab.update(input(&hand, true));
    grab.hold(hand, { 0.0f, 5.0f, 0.0f });
    CHECK(grab.update(input(nullptr, true)) == VrGrabRequest::Release);
  }
}


TEST_CASE(grab_lost_object_needs_a_new_press) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;

  grab.update(input(&hand, true));
  grab.hold(hand, { 0.0f, 5.0f, 0.0f });
  grab.drop();

  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::None);
  CHECK(grab.update(input(&hand, false)) == VrGrabRequest::None);
  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::Grab);
}


TEST_CASE(grab_held_point_follows_the_hand_and_turns_with_it) {
  VrGameCameraPose hand = northHand({ 100.0f, 200.0f, 50.0f });
  VrGrabHand grab;

  grab.update(input(&hand, true));
  // Five units ahead of the hand and two above
  grab.hold(hand, { 100.0f, 205.0f, 52.0f });

  VrVector3 still = grab.target(hand);
  CHECK_NEAR(still.x, 100.0f, 1e-4f);
  CHECK_NEAR(still.y, 205.0f, 1e-4f);
  CHECK_NEAR(still.z, 52.0f, 1e-4f);

  // Moved and turned to face east: still ahead and above
  VrGameCameraPose turned = eastHand({ 0.0f, 0.0f, 0.0f });
  VrVector3 moved = grab.target(turned);
  CHECK_NEAR(moved.x, 5.0f, 1e-4f);
  CHECK_NEAR(moved.y, 0.0f, 1e-4f);
  CHECK_NEAR(moved.z, 2.0f, 1e-4f);
}


TEST_CASE(grab_pulled_point_comes_to_the_hand_and_stays) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;

  float distance = 2.0f * VrGameUnitsPerMetre;
  grab.update(input(&hand, true));
  grab.pull(hand, { 0.0f, distance, 0.0f }, VrGrabHand::PullSpeed);
  CHECK(grab.pulling());
  CHECK_NEAR(grab.target(hand).y, distance, 1e-3f);

  // Straight along the line from the hand, at the set speed
  CHECK(grab.update(input(&hand, true)) == VrGrabRequest::None);
  CHECK_NEAR(grab.target(hand).y, distance - VrGrabHand::PullSpeed * Frame, 1e-2f);
  CHECK_NEAR(grab.target(hand).x, 0.0f, 1e-4f);

  uint32_t frames = 1;

  while (grab.pulling() && frames < 1000) {
    grab.update(input(&hand, true));
    frames++;
  }

  CHECK(frames <= uint32_t(distance / (VrGrabHand::PullSpeed * Frame)) + 1);
  CHECK_NEAR(vrLength(grab.target(hand)), 0.0f, 1e-4f);

  // Then held in the hand's centre wherever the hand goes
  VrGameCameraPose moved = eastHand({ 50.0f, 20.0f, 10.0f });
  CHECK(grab.update(input(&moved, true)) == VrGrabRequest::None);
  CHECK_NEAR(vrLength(grab.target(moved) - moved.position), 0.0f, 1e-4f);
}


TEST_CASE(grab_pulled_point_follows_the_hand_on_its_way) {
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;

  grab.update(input(&hand, true));
  grab.pull(hand, { 0.0f, 100.0f, 0.0f }, VrGrabHand::PullSpeed);
  grab.update(input(&hand, true));

  // Turned to the east, the point is in front of the hand there
  VrGameCameraPose turned = eastHand({ 0.0f, 0.0f, 0.0f });
  VrVector3 target = grab.target(turned);
  CHECK(target.x > 90.0f);
  CHECK_NEAR(target.y, 0.0f, 1e-3f);

  // Letting go stops the pull
  CHECK(grab.update(input(&turned, false)) == VrGrabRequest::Release);
  grab.drop();
  CHECK(!grab.pulling());
}


TEST_CASE(grab_heavy_body_is_pulled_more_slowly) {
  CHECK_NEAR(vrGrabPullSpeed(0.5f), VrGrabHand::PullSpeed, 1e-3f);
  CHECK_NEAR(vrGrabPullSpeed(VrGrabLightMass), VrGrabHand::PullSpeed, 1e-3f);
  CHECK(vrGrabPullSpeed(20.0f) < VrGrabHand::PullSpeed);
  CHECK(vrGrabPullSpeed(48.0f) < vrGrabPullSpeed(20.0f));
  // Never to a crawl
  CHECK(vrGrabPullSpeed(1000.0f) >= 0.3f * VrGrabHand::PullSpeed);

  // A slower pull takes longer to reach the hand
  VrGameCameraPose hand = northHand({ 0.0f, 0.0f, 0.0f });
  VrGrabHand grab;
  grab.update(input(&hand, true));
  grab.pull(hand, { 0.0f, 100.0f, 0.0f }, vrGrabPullSpeed(48.0f));
  grab.update(input(&hand, true));
  CHECK_NEAR(grab.target(hand).y, 100.0f - vrGrabPullSpeed(48.0f) * Frame, 1e-2f);
}


TEST_CASE(grab_hand_force_is_shared_by_the_weight) {
  constexpr float Full    = 750.0f;
  constexpr float Gravity = 70.0f;

  // Light bodies follow as with the game's grab
  CHECK_NEAR(vrGrabMaxAcceleration(2.0f, 1, Full, Gravity), Full, 1e-3f);
  CHECK_NEAR(vrGrabMaxAcceleration(VrGrabLightMass, 1, Full, Gravity), Full, 1e-3f);

  // Heavier ones less, two hands more than one
  float one = vrGrabMaxAcceleration(30.0f, 1, Full, Gravity);
  float two = vrGrabMaxAcceleration(30.0f, 2, Full, Gravity);
  CHECK(one < Full);
  CHECK(two > one);
  CHECK(vrGrabMaxAcceleration(48.0f, 1, Full, Gravity) < one);

  // Always above gravity, never above the game's
  CHECK(vrGrabMaxAcceleration(1000.0f, 1, Full, Gravity) > Gravity);
  CHECK(vrGrabMaxAcceleration(1000.0f, 1, 50.0f, Gravity) <= 50.0f);
}


TEST_CASE(grab_spring_lag_matches_its_steady_state) {
  VrGrabSpringConstants spring;
  spring.damping       = 0.5f;
  spring.elasticity    = 0.2f;
  spring.objectDamping = 0.9f;

  // Step the spring's velocity rule on a target moving at a steady speed
  constexpr float Dt    = 1.0f / 72.0f;
  constexpr float Speed = 30.0f;

  float target   = 0.0f;
  float point    = 0.0f;
  float velocity = 0.0f;

  for (uint32_t i = 0; i < 2000; i++) {
    target += Speed * Dt;
    velocity *= spring.objectDamping;
    velocity += -spring.damping * velocity + spring.elasticity / Dt * (target - point);
    point += velocity * Dt;
  }

  // The target moved again since the last step
  CHECK_NEAR((target + Speed * Dt - point) / (Speed * Dt), vrGrabSpringLagSteps(spring), 1e-2f);

  // The game's damping takes more of the speed, the point trails further
  VrGrabSpringConstants game = spring;
  game.objectDamping = 0.75f;
  CHECK(vrGrabSpringLagSteps(game) > vrGrabSpringLagSteps(spring));
}


TEST_CASE(grab_lead_follows_the_player_but_not_a_teleport) {
  VrVector3 step = { 3.0f, 0.0f, 0.0f };

  VrVector3 lead = vrGrabLead(step, 4.0f);
  CHECK_NEAR(lead.x, 12.0f, 1e-4f);
  CHECK_NEAR(lead.y, 0.0f, 1e-4f);

  // Standing still, or no frames to make up
  CHECK_NEAR(vrLength(vrGrabLead(VrVector3(), 4.0f)), 0.0f, 1e-6f);
  CHECK_NEAR(vrLength(vrGrabLead(step, 0.0f)), 0.0f, 1e-6f);

  // Limited in length
  VrVector3 sprint = { 0.0f, 0.2f * VrGameUnitsPerMetre, 0.0f };
  CHECK(vrLength(vrGrabLead(sprint, 5.0f)) < 0.61f * VrGameUnitsPerMetre);
  CHECK(vrGrabLead(sprint, 5.0f).y > 0.0f);

  // Moved by a load door or a teleport
  VrVector3 jump = { 0.0f, 5.0f * VrGameUnitsPerMetre, 0.0f };
  CHECK_NEAR(vrLength(vrGrabLead(jump, 4.0f)), 0.0f, 1e-6f);
}


namespace {

  VrGrabFrame rotateFrame(const VrGrabFrame& frame, const VrVector3& rotation) {
    float angle = vrLength(rotation);

    if (!(angle > 0.0f))
      return frame;

    VrQuaternion q = vrQuaternionFromAxisAngle(rotation, angle);
    return { vrRotate(q, frame.x), vrRotate(q, frame.y), vrRotate(q, frame.z) };
  }

}


TEST_CASE(grab_rotation_between_frames_is_axis_times_angle) {
  constexpr float Pi = 3.14159265f;
  VrGrabFrame identity;

  CHECK_NEAR(vrLength(vrGrabRotationBetween(identity, identity)), 0.0f, 1e-5f);

  VrVector3 quarter = vrGrabRotationBetween(identity, rotateFrame(identity, { 0.0f, 0.0f, Pi / 2.0f }));
  CHECK_NEAR(quarter.x, 0.0f, 1e-4f);
  CHECK_NEAR(quarter.y, 0.0f, 1e-4f);
  CHECK_NEAR(quarter.z, Pi / 2.0f, 1e-4f);

  // Turned the other way from the other frame
  VrGrabFrame tilted = rotateFrame(identity, { 0.3f, -0.5f, 0.2f });
  VrVector3 back = vrGrabRotationBetween(tilted, identity);
  VrGrabFrame returned = rotateFrame(tilted, back);
  CHECK_NEAR(vrLength(returned.x - identity.x), 0.0f, 1e-4f);
  CHECK_NEAR(vrLength(returned.y - identity.y), 0.0f, 1e-4f);

  // Half a turn, where the trace is lowest
  VrVector3 half = vrGrabRotationBetween(identity, rotateFrame(identity, { Pi, 0.0f, 0.0f }));
  CHECK_NEAR(vrLength(half), Pi, 1e-3f);
  CHECK_NEAR(std::fabs(half.x), Pi, 1e-3f);
}


TEST_CASE(grab_object_keeps_its_rotation_in_the_hand) {
  VrGrabFrame body = rotateFrame(VrGrabFrame(), { 0.4f, 0.1f, -0.7f });
  VrGrabFrame north = vrGrabHandFrame(northHand({ }));
  VrGrabFrame east  = vrGrabHandFrame(eastHand({ }));

  VrGrabFrame local = vrGrabFrameToLocal(north, body);
  VrGrabFrame same  = vrGrabFrameFromLocal(north, local);
  CHECK_NEAR(vrLength(same.x - body.x), 0.0f, 1e-5f);
  CHECK_NEAR(vrLength(same.z - body.z), 0.0f, 1e-5f);

  // The hand turned a quarter to the right, so did the object
  VrGrabFrame turned = vrGrabFrameFromLocal(east, local);
  VrVector3 rotation = vrGrabRotationBetween(body, turned);
  CHECK_NEAR(rotation.x, 0.0f, 1e-4f);
  CHECK_NEAR(rotation.y, 0.0f, 1e-4f);
  CHECK_NEAR(rotation.z, -1.5707963f, 1e-4f);
}


TEST_CASE(grab_turn_settles_on_the_hand_without_spinning) {
  constexpr float Step = 1.0f / 60.0f;

  VrGrabSpringConstants spring;
  spring.damping       = 0.5f;
  spring.elasticity    = 0.2f;
  spring.objectDamping = 0.9f;

  VrGrabFrame target = rotateFrame(VrGrabFrame(), { 1.2f, -1.0f, 0.8f });
  VrGrabFrame body;
  VrVector3 omega;
  float largest = vrLength(vrGrabRotationBetween(body, target));
  float error = largest;

  for (uint32_t i = 0; i < 60; i++) {
    // applyAction damps the body before the callbacks run
    omega = omega * spring.objectDamping;
    omega = omega + vrGrabTurnStep(spring, omega, vrGrabRotationBetween(body, target), Step, 300.0f);
    body  = rotateFrame(body, omega * Step);

    float next = vrLength(vrGrabRotationBetween(body, target));
    CHECK(next <= largest + 1e-3f);
    error = next;
  }

  CHECK(error < 0.02f);
}


TEST_CASE(grab_turn_is_limited_by_the_hands_strength) {
  VrGrabSpringConstants spring;
  spring.damping    = 0.5f;
  spring.elasticity = 0.2f;

  VrVector3 change = vrGrabTurnStep(spring, VrVector3(), { 0.0f, 3.0f, 0.0f }, 0.01f, 50.0f);
  CHECK_NEAR(vrLength(change), 0.5f, 1e-4f);
  CHECK(change.y > 0.0f);

  CHECK_NEAR(vrLength(vrGrabTurnStep(spring, { 1.0f, 0.0f, 0.0f }, { }, 0.0f, 50.0f)), 0.0f, 1e-6f);
}
