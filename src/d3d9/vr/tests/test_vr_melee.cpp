#include <cmath>

#include "test.h"

#include "../vr_melee.h"

using namespace dxvk;

namespace {

  constexpr float M = VrGameUnitsPerMetre;

  void checkVector(const VrVector3& v, float x, float y, float z) {
    CHECK_NEAR(v.x, x, 1e-3);
    CHECK_NEAR(v.y, y, 1e-3);
    CHECK_NEAR(v.z, z, 1e-3);
  }

  VrGameCameraPose northFacing(const VrVector3& position) {
    VrGameCameraPose pose;
    pose.forward  = { 0.0f, 1.0f, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = position;
    return pose;
  }

  // A hand turned so that it points up, the back of the hand facing south
  VrGameCameraPose upPointing(const VrVector3& position) {
    VrGameCameraPose pose;
    pose.forward  = { 0.0f, 0.0f, 1.0f };
    pose.up       = { 0.0f, -1.0f, 0.0f };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = position;
    return pose;
  }

  VrSegment segment(const VrVector3& a, const VrVector3& b) {
    return { a, b };
  }

}


TEST_CASE(melee_hand_frame_round_trips) {
  VrGameCameraPose hand = upPointing({ 10.0f, 20.0f, 30.0f });
  VrVector3 local = { 1.0f, 2.0f, 3.0f };
  VrVector3 world = vrPointFromHand(hand, local);

  checkVector(world, 13.0f, 18.0f, 31.0f);
  checkVector(vrPointToHand(hand, world), 1.0f, 2.0f, 3.0f);
}


TEST_CASE(melee_blade_reaches_far_side_of_bound) {
  VrGameCameraPose hand = northFacing({ 0.0f, 0.0f, 0.0f });
  VrBlade blade;

  // A sword along the hand: bound centred 30 units ahead, radius 35
  CHECK(vrMeasureBlade(hand, { 0.0f, 30.0f, 0.0f }, 35.0f, blade));
  checkVector(blade.base, 0.0f, 0.0f, 0.0f);
  checkVector(blade.tip, 65.0f, 0.0f, 0.0f);

  // An axe whose head leans up: the blade leans with it
  CHECK(vrMeasureBlade(hand, { 0.0f, 30.0f, 40.0f }, 10.0f, blade));
  checkVector(blade.tip, 36.0f, 48.0f, 0.0f);
}


TEST_CASE(melee_blade_follows_hand) {
  VrGameCameraPose hand = northFacing({ 0.0f, 0.0f, 0.0f });
  VrBlade blade;
  CHECK(vrMeasureBlade(hand, { 0.0f, 30.0f, 0.0f }, 35.0f, blade));

  // Raised to point up, the blade points up from the new hand position
  VrSegment world = vrBladeInWorld(upPointing({ 5.0f, 5.0f, 100.0f }), blade);
  checkVector(world.a, 5.0f, 5.0f, 100.0f);
  checkVector(world.b, 5.0f, 5.0f, 165.0f);
}


TEST_CASE(melee_blade_rejects_odd_bounds) {
  VrGameCameraPose hand = northFacing({ 0.0f, 0.0f, 0.0f });
  VrBlade blade;

  CHECK(!vrMeasureBlade(hand, { 0.0f, 1.0f, 0.0f }, 0.5f, blade));
  CHECK(!vrMeasureBlade(hand, { 0.0f, 1.0f, 0.0f }, 1000.0f, blade));
  CHECK(!vrMeasureBlade(hand, { 0.0f, 1.0f, 0.0f }, std::nanf(""), blade));

  // Centred on the hand: along the hand's forward axis
  CHECK(vrMeasureBlade(hand, { 0.0f, 0.0f, 0.0f }, 20.0f, blade));
  checkVector(blade.tip, 20.0f, 0.0f, 0.0f);
}


TEST_CASE(melee_segment_distance) {
  // Crossing at right angles, one unit apart
  CHECK_NEAR(vrSegmentDistance(
    segment({ -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }),
    segment({ 0.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f })), 1.0f, 1e-5);

  // Parallel and overlapping
  CHECK_NEAR(vrSegmentDistance(
    segment({ 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }),
    segment({ 1.0f, 3.0f, 0.0f }, { 3.0f, 3.0f, 0.0f })), 3.0f, 1e-5);

  // In line, end to end
  CHECK_NEAR(vrSegmentDistance(
    segment({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }),
    segment({ 3.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f })), 2.0f, 1e-5);

  // Closest points at an end of each
  CHECK_NEAR(vrSegmentDistance(
    segment({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }),
    segment({ 4.0f, 4.0f, 0.0f }, { 4.0f, 8.0f, 0.0f })), 5.0f, 1e-5);

  // A point against a segment
  CHECK_NEAR(vrSegmentDistance(
    segment({ 0.0f, 2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }),
    segment({ -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f })), 2.0f, 1e-5);
}


TEST_CASE(melee_sweep_catches_bone_between_frames) {
  // A vertical bone, and a blade that passes from one side of it to the other
  VrSegment bone = segment({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f });
  VrSegment from = segment({ -20.0f, -10.0f, 25.0f }, { -20.0f, 40.0f, 25.0f });
  VrSegment to   = segment({ 20.0f, -10.0f, 25.0f }, { 20.0f, 40.0f, 25.0f });

  CHECK(vrSegmentDistance(from, bone) > 19.0f);
  CHECK(vrSegmentDistance(to, bone) > 19.0f);
  CHECK(vrSweptSegmentContact(from, to, bone, 4, 1.0f).touches);
  CHECK(!vrSweptSegmentContact(from, to, bone, 1, 1.0f).touches);
}


TEST_CASE(melee_sweep_contact_is_the_first_touch) {
  // A vertical bone at the origin, and a blade along Y that moves along X
  // from 40 units before it to 40 units past it
  VrSegment bone = segment({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 50.0f });
  VrSegment from = segment({ -40.0f, -10.0f, 20.0f }, { -40.0f, 30.0f, 20.0f });
  VrSegment to   = segment({ 40.0f, -10.0f, 20.0f }, { 40.0f, 30.0f, 20.0f });

  VrSweepContact contact = vrSweptSegmentContact(from, to, bone, 8, 12.0f);

  // Positions are 10 units apart: the first within 12 units is at -10
  CHECK(contact.touches);
  CHECK_NEAR(contact.time, 3.0f / 8.0f, 1e-5);
  CHECK_NEAR(contact.distance, 10.0f, 1e-4);
  checkVector(contact.fixedPoint, 0.0f, 0.0f, 20.0f);
  checkVector(contact.movingPoint, -10.0f, 0.0f, 20.0f);
  checkVector(contact.motion, 80.0f, 0.0f, 0.0f);

  VrBladeHit hit = vrComputeBladeHit(contact, 5.0f);
  checkVector(hit.location, -5.0f, 0.0f, 20.0f);
  checkVector(hit.direction, 1.0f, 0.0f, 0.0f);

  // Within the capsule, the hit is where the blade is
  hit = vrComputeBladeHit(contact, 15.0f);
  checkVector(hit.location, -10.0f, 0.0f, 20.0f);
}


TEST_CASE(melee_blade_hit_without_motion) {
  VrSweepContact contact;
  contact.touches     = true;
  contact.fixedPoint  = { 0.0f, 0.0f, 0.0f };
  contact.movingPoint = { 0.0f, 4.0f, 0.0f };

  // A still blade hits towards the bone
  VrBladeHit hit = vrComputeBladeHit(contact, 2.0f);
  checkVector(hit.location, 0.0f, 2.0f, 0.0f);
  checkVector(hit.direction, 0.0f, -1.0f, 0.0f);

  // On the bone, downwards
  contact.movingPoint = contact.fixedPoint;
  hit = vrComputeBladeHit(contact, 2.0f);
  checkVector(hit.location, 0.0f, 0.0f, 0.0f);
  checkVector(hit.direction, 0.0f, 0.0f, -1.0f);
}


TEST_CASE(melee_swing_needs_hit_speed) {
  VrSwingConfig config;
  VrSwingTracker swing(config);

  // The first frame has no speed
  swing.update({ 0.0f, 0.0f, 0.0f }, 0.01f);
  CHECK(!swing.swinging());

  // 1 m/s
  swing.update({ 0.01f * M, 0.0f, 0.0f }, 0.01f);
  CHECK_NEAR(swing.speed(), 1.0f, 1e-3);
  CHECK(!swing.swinging());

  // 3 m/s
  swing.update({ 0.04f * M, 0.0f, 0.0f }, 0.01f);
  CHECK_NEAR(swing.speed(), 3.0f, 1e-3);
  CHECK(swing.swinging());
}


TEST_CASE(melee_swing_hits_each_target_once) {
  VrSwingConfig config;
  VrSwingTracker swing(config);
  constexpr float T = VrSwingTracker::RehitTime;
  int a = 0;
  int b = 0;

  // The tip moves along X at 3 m/s while x grows by 0.03 m every 10 ms
  float x = 0.0f;
  swing.update({ x, 0.0f, 0.0f }, 0.01f);
  swing.update({ x += 0.03f * M, 0.0f, 0.0f }, 0.01f);
  swing.addHit(&a);
  CHECK(swing.hasHit(&a));
  CHECK(!swing.hasHit(&b));

  // A swing that keeps going does not hit it again, however long it lasts
  swing.update({ x += 3.0f * T * M, 0.0f, 0.0f }, T);
  CHECK(swing.swinging());
  CHECK(swing.hasHit(&a));

  // Once the swing is over, it can be hit again
  swing.update({ x, 0.0f, 0.0f }, 0.01f);
  CHECK(!swing.swinging());
  CHECK(!swing.hasHit(&a));

  // Shaking the blade against a target: the next swing comes too soon
  swing.update({ x += 0.03f * M, 0.0f, 0.0f }, 0.01f);
  swing.addHit(&b);
  swing.update({ x, 0.0f, 0.0f }, 0.01f);
  swing.update({ x -= 0.03f * M, 0.0f, 0.0f }, 0.01f);
  CHECK(swing.swinging());
  CHECK(swing.hasHit(&b));

  // A later swing can hit it again
  swing.update({ x, 0.0f, 0.0f }, T);
  swing.update({ x += 0.03f * M, 0.0f, 0.0f }, 0.01f);
  CHECK(swing.swinging());
  CHECK(!swing.hasHit(&b));

  swing.addHit(&b);
  swing.reset();
  CHECK(!swing.hasHit(&b));
  CHECK(!swing.swinging());
}


TEST_CASE(melee_guard_pose) {
  VrGameCameraPose head = northFacing({ 0.0f, 0.0f, 1.7f * M });

  // Held across the face, 40 cm ahead, a little below the eyes
  VrSegment guard = segment({ -0.3f * M, 0.4f * M, 1.6f * M }, { 0.3f * M, 0.4f * M, 1.65f * M });
  CHECK(vrIsGuardPose(head, guard));

  // Pointing forward: a thrust, not a guard
  CHECK(!vrIsGuardPose(head, segment({ 0.0f, 0.2f * M, 1.6f * M }, { 0.0f, 0.8f * M, 1.6f * M })));

  // Held upright
  CHECK(!vrIsGuardPose(head, segment({ 0.0f, 0.4f * M, 1.3f * M }, { 0.1f * M, 0.4f * M, 1.9f * M })));

  // Across the body, but down at the hips
  CHECK(!vrIsGuardPose(head, segment({ -0.3f * M, 0.3f * M, 0.9f * M }, { 0.3f * M, 0.3f * M, 0.9f * M })));

  // Across, but out of reach
  CHECK(!vrIsGuardPose(head, segment({ -0.3f * M, 1.2f * M, 1.6f * M }, { 0.3f * M, 1.2f * M, 1.6f * M })));

  // Behind the head
  CHECK(!vrIsGuardPose(head, segment({ -0.3f * M, -0.2f * M, 1.6f * M }, { 0.3f * M, -0.2f * M, 1.6f * M })));

  // The same guard follows the head when it turns east
  VrGameCameraPose east = head;
  east.forward = { 1.0f, 0.0f, 0.0f };
  east.right   = { 0.0f, -1.0f, 0.0f };
  CHECK(!vrIsGuardPose(east, guard));
  CHECK(vrIsGuardPose(east, segment({ 0.4f * M, 0.3f * M, 1.6f * M }, { 0.4f * M, -0.3f * M, 1.65f * M })));
}


TEST_CASE(melee_guard_holds_through_short_exits) {
  VrGuardTracker guard;

  guard.update(true, false, 0.01f);
  CHECK(guard.guarding());

  // Left for less than the release time
  guard.update(false, false, 0.1f);
  CHECK(guard.guarding());
  guard.update(true, false, 0.01f);
  CHECK(guard.guarding());

  // Left for longer
  guard.update(false, false, 0.1f);
  guard.update(false, false, 0.1f);
  CHECK(!guard.guarding());

  // A swing ends it at once, and is no guard
  guard.update(true, false, 0.01f);
  guard.update(true, true, 0.01f);
  CHECK(!guard.guarding());
}


TEST_CASE(melee_fist_needs_four_closed_fingers) {
  CHECK(vrIsFist({ 0.0f, 0.9f, 1.0f, 1.0f, 1.0f }));
  CHECK(vrIsFist({ 1.0f, 0.7f, 0.7f, 0.7f, 0.7f }));
  CHECK(!vrIsFist({ 1.0f, 0.0f, 1.0f, 1.0f, 1.0f }));
  CHECK(!vrIsFist({ 1.0f, 1.0f, 1.0f, 0.3f, 1.0f }));
}


TEST_CASE(melee_fist_strikes_ahead_of_the_grip) {
  VrGameCameraPose hand = northFacing({ 0.0f, 0.0f, 0.0f });
  VrSegment fist = vrBladeInWorld(hand, vrFistBlade());

  checkVector(fist.a, 0.0f, 0.0f, 0.0f);
  CHECK(fist.b.y > 0.05f * M && fist.b.y < 0.15f * M);
}
