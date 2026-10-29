#include <algorithm>
#include <cmath>

#include "test.h"

#include "../vr_grab.h"
#include "../vr_grab_fingers.h"
#include "../vr_math.h"
#include "../vr_melee.h"

using namespace dxvk;

namespace {

  VrGameCameraPose controllerAt(const VrVector3& position) {
    VrGameCameraPose pose;
    pose.forward  = { 1.0f, 0.0f, 0.0f };
    pose.up       = { 0.0f, 1.0f, 0.0f };
    pose.right    = { 0.0f, 0.0f, 1.0f };
    pose.position = position;
    return pose;
  }

  // Bones three units long along their parent's x axis, metacarpals four
  VrFingerRig straightRig() {
    VrFingerRig rig;

    for (uint32_t i = 0; i < VrFingerBoneCount; i++) {
      rig.translate[i] = { 3.0f, 0.0f, 0.0f };
      rig.boneScale[i] = 1.0f;
    }

    for (uint32_t first : { 3u, 7u, 11u, 15u })
      rig.translate[first] = { 4.0f, 0.0f, float(first) * 0.2f - 2.0f };

    return rig;
  }

  // Whether a segment crosses the plane x . normal = offset
  struct Plane {
    VrVector3 normal;
    float     offset;

    bool operator () (const VrVector3& from, const VrVector3& to) const {
      float a = vrDot(from, normal) - offset;
      float b = vrDot(to, normal) - offset;
      return (a <= 0.0f) != (b <= 0.0f);
    }
  };

}


TEST_CASE(grab_fingers_close_fully_on_nothing) {
  VrFingerRig rig = straightRig();
  VrFingerCurls curls = vrFitFingers(rig, VrHand::Left, controllerAt({ }),
    [] (const VrVector3&, const VrVector3&) { return false; });

  for (float curl : curls)
    CHECK_NEAR(curl, 1.0f, 1e-6f);

  curls = vrFitFingers(rig, VrHand::Left, controllerAt({ }),
    [] (const VrVector3&, const VrVector3&) { return true; });

  for (float curl : curls)
    CHECK_NEAR(curl, 0.0f, 1e-6f);
}


TEST_CASE(grab_finger_stops_on_a_surface_in_its_way) {
  VrFingerRig rig = straightRig();
  VrGameCameraPose controller = controllerAt({ 5.0f, -2.0f, 1.0f });

  VrFingerJoints open   = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, 0.0f, controller);
  VrFingerJoints closed = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, 1.0f, controller);

  // A surface across the tip's way, clear of the open finger
  VrVector3 way = closed[3] - open[3];
  VrVector3 normal = way * (1.0f / vrLength(way));
  float clear = vrDot(open[0], normal);

  for (const VrVector3& joint : open)
    clear = std::max(clear, vrDot(joint, normal));

  Plane plane = { normal, clear + (vrDot(closed[3], normal) - clear) * 0.4f };

  VrFingerCurls curls = vrFitFingers(rig, VrHand::Left, controller, plane);
  float curl = curls[uint32_t(VrFinger::Middle)];
  CHECK(curl > 0.0f);
  CHECK(curl < 1.0f);

  // Resting on it, not through it by more than a step
  VrFingerJoints fitted = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, curl, controller);
  VrFingerJoints before = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, curl - 0.0625f, controller);
  CHECK(!plane(open[3], before[3]));
  CHECK(vrDot(fitted[3], normal) - plane.offset < vrLength(way) * 0.25f);
}


TEST_CASE(grab_right_fingers_mirror_the_left) {
  VrFingerRig rig = straightRig();
  VrFingerRig mirrored = rig;

  for (VrVector3& t : mirrored.translate)
    t.z = -t.z;

  mirrored.hand.translate.z = -rig.hand.translate.z;

  VrFingerJoints left  = vrFingerJoints(rig, VrHand::Left, VrFinger::Index, 0.7f, controllerAt({ }));
  VrFingerJoints right = vrFingerJoints(mirrored, VrHand::Right, VrFinger::Index, 0.7f, controllerAt({ }));

  for (uint32_t i = 0; i < left.size(); i++) {
    CHECK_NEAR(left[i].x, right[i].x, 0.05f);
    CHECK_NEAR(left[i].y, right[i].y, 0.05f);
    CHECK_NEAR(left[i].z, -right[i].z, 0.05f);
  }
}


TEST_CASE(grab_palm_faces_where_the_fingers_close) {
  VrFingerRig rig = straightRig();
  VrGameCameraPose controller = controllerAt({ });
  VrVector3 palm = vrPalmDirection(rig, VrHand::Left, controller);
  CHECK_NEAR(vrLength(palm), 1.0f, 1e-4f);

  VrFingerJoints open   = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, 0.0f, controller);
  VrFingerJoints closed = vrFingerJoints(rig, VrHand::Left, VrFinger::Middle, 1.0f, controller);
  CHECK(vrDot(closed[3] - open[0], palm) > 0.0f);
}


TEST_CASE(grab_palm_turns_towards_the_point_touched_up_to_a_limit) {
  VrVector3 turn = vrPalmTurn({ 1.0f, 0.0f, 0.0f }, { }, { 0.0f, 10.0f, 0.0f }, 0.6f);
  CHECK_NEAR(turn.x, 0.0f, 1e-5f);
  CHECK_NEAR(turn.y, 0.0f, 1e-5f);
  CHECK_NEAR(turn.z, 0.6f, 1e-5f);

  turn = vrPalmTurn({ 1.0f, 0.0f, 0.0f }, { }, { 10.0f, 1.0f, 0.0f }, 0.6f);
  CHECK_NEAR(vrLength(turn), std::atan2(1.0f, 10.0f), 1e-4f);

  CHECK_NEAR(vrLength(vrPalmTurn({ 1.0f, 0.0f, 0.0f }, { }, { }, 0.6f)), 0.0f, 1e-6f);
}


TEST_CASE(grab_palm_turn_keeps_the_held_point) {
  VrGameCameraPose hand = controllerAt({ 3.0f, 4.0f, 5.0f });

  VrGrabCorrection correction;
  correction.palm  = { 0.0f, 0.5f, 0.0f };
  correction.pivot = { 6.0f, -1.0f, 2.0f };

  VrGameCameraPose drawn = vrGrabCorrectHand(hand, correction);
  VrVector3 before = vrPointFromHand(hand, correction.pivot);
  VrVector3 after  = vrPointFromHand(drawn, correction.pivot);
  CHECK_NEAR(vrLength(after - before), 0.0f, 1e-4f);
  CHECK_NEAR(std::acos(vrDot(drawn.forward, hand.forward)), 0.5f, 1e-4f);
}


TEST_CASE(grab_fingers_rest_on_the_object_unless_raised) {
  VrGrabFingerBlend blend;
  VrFingerCurls tracked = { 1.0f, 0.1f, 1.0f, 1.0f, 1.0f };
  VrFingerCurls fitted  = { 0.5f, 0.5f, 0.6f, 0.7f, 0.8f };
  VrFingerCurls curls;

  float step = VrGrabFingerBlend::BlendTime / 2.0f;
  CHECK(blend.update(&tracked, &fitted, step, curls));
  CHECK_NEAR(curls[2], 0.8f, 1e-5f);

  blend.update(&tracked, &fitted, step, curls);
  CHECK_NEAR(curls[0], 0.5f, 1e-5f);
  CHECK_NEAR(curls[1], 0.1f, 1e-5f);
  CHECK_NEAR(curls[4], 0.8f, 1e-5f);

  // Let go: back to the player's own
  blend.update(&tracked, nullptr, step, curls);
  CHECK_NEAR(curls[2], 0.8f, 1e-5f);
  blend.update(&tracked, nullptr, step, curls);
  CHECK_NEAR(curls[2], 1.0f, 1e-5f);

  // Without tracking the fitted curls are drawn while held, nothing after
  VrGrabFingerBlend untracked;
  untracked.update(nullptr, &fitted, VrGrabFingerBlend::BlendTime, curls);
  CHECK_NEAR(curls[3], 0.7f, 1e-5f);
  CHECK(!untracked.update(nullptr, nullptr, VrGrabFingerBlend::BlendTime, curls));
}
