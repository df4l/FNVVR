#include <cmath>

#include "test.h"

#include "../vr_weapon_aim.h"

using namespace dxvk;

namespace {

  constexpr float Pi = 3.14159265358979f;

  void checkVector(const VrVector3& v, float x, float y, float z) {
    CHECK_NEAR(v.x, x, 1e-4);
    CHECK_NEAR(v.y, y, 1e-4);
    CHECK_NEAR(v.z, z, 1e-4);
  }

  VrGameCameraPose northFacing(const VrVector3& position) {
    VrGameCameraPose pose;
    pose.forward  = { 0.0f, 1.0f, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = position;
    return pose;
  }

  VrGameCameraPose eastFacing(const VrVector3& position) {
    VrGameCameraPose pose;
    pose.forward  = { 1.0f, 0.0f, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { 0.0f, -1.0f, 0.0f };
    pose.position = position;
    return pose;
  }

  VrVector3 apply(const VrGameTransform& t, const VrVector3& v) {
    return t.rotate * v + t.translate;
  }

}


TEST_CASE(camera_rotation_maps_local_axes_to_camera_axes) {
  VrGameRotation r = vrCameraRotation(eastFacing({ }));

  // The camera looks along its local +X, up is +Y and right is +Z
  checkVector(r * VrVector3 { 1.0f, 0.0f, 0.0f }, 1.0f, 0.0f, 0.0f);
  checkVector(r * VrVector3 { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 1.0f);
  checkVector(r * VrVector3 { 0.0f, 0.0f, 1.0f }, 0.0f, -1.0f, 0.0f);
}


TEST_CASE(rig_moves_weapon_onto_hand) {
  VrGameCameraPose camera = northFacing({ 0.0f, 0.0f, 120.0f });
  VrGameCameraPose hand   = northFacing({ 15.0f, 30.0f, 90.0f });
  VrVector3 weapon = { 8.0f, 20.0f, 110.0f };

  VrGameTransform rig = vrComputeRigToHand(camera, weapon, hand);
  checkVector(apply(rig, weapon), 15.0f, 30.0f, 90.0f);
}


TEST_CASE(rig_turns_with_hand_relative_to_camera) {
  // The game holds the weapon pointing north; the hand points east
  VrGameCameraPose camera = northFacing({ });
  VrGameCameraPose hand   = eastFacing({ 50.0f, 0.0f, 0.0f });
  VrVector3 weapon = { 0.0f, 10.0f, 0.0f };

  VrGameTransform rig = vrComputeRigToHand(camera, weapon, hand);

  // The barrel, along the camera's forward, now points east
  checkVector(rig.rotate * VrVector3 { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, 0.0f);
  checkVector(apply(rig, weapon), 50.0f, 0.0f, 0.0f);

  // A muzzle further along the barrel stays in front of the hand
  checkVector(apply(rig, VrVector3 { 0.0f, 30.0f, 0.0f }), 70.0f, 0.0f, 0.0f);
}


TEST_CASE(aim_angles_follow_game_convention) {
  VrHeadAngles north = vrComputeAimAngles({ 0.0f, 2.0f, 0.0f });
  CHECK_NEAR(north.yaw, 0.0f, 1e-5);
  CHECK_NEAR(north.pitch, 0.0f, 1e-5);

  VrHeadAngles east = vrComputeAimAngles({ 1.0f, 0.0f, 0.0f });
  CHECK_NEAR(east.yaw, Pi / 2.0f, 1e-5);

  VrHeadAngles west = vrComputeAimAngles({ -1.0f, 0.0f, 0.0f });
  CHECK_NEAR(west.yaw, 3.0f * Pi / 2.0f, 1e-5);

  // Positive pitch looks down
  VrHeadAngles down = vrComputeAimAngles({ 0.0f, 1.0f, -1.0f });
  CHECK_NEAR(down.pitch, Pi / 4.0f, 1e-5);
}


TEST_CASE(two_handed_pose_turns_towards_the_supporting_hand) {
  VrGameCameraPose hand = northFacing({ 5.0f, 6.0f, 7.0f });

  VrGameCameraPose turned = vrComputeTwoHandedPose(hand, { 0.0f, 20.0f, 0.0f }, { 30.0f, 0.0f, 0.0f });
  checkVector(turned.forward, 1.0f, 0.0f, 0.0f);
  checkVector(turned.up, 0.0f, 0.0f, 1.0f);
  checkVector(turned.right, 0.0f, -1.0f, 0.0f);
  checkVector(turned.position, 5.0f, 6.0f, 7.0f);

  float h = std::sqrt(0.5f);
  VrGameCameraPose raised = vrComputeTwoHandedPose(hand, { 0.0f, 20.0f, 0.0f }, { 0.0f, 10.0f, 10.0f });
  checkVector(raised.forward, 0.0f, h, h);
  checkVector(raised.up, 0.0f, -h, h);
  checkVector(raised.right, 1.0f, 0.0f, 0.0f);
}


TEST_CASE(two_handed_pose_keeps_the_hand_when_aligned) {
  VrGameCameraPose hand = northFacing({ 0.0f, 0.0f, 0.0f });

  VrGameCameraPose same = vrComputeTwoHandedPose(hand, { 0.0f, 20.0f, -5.0f }, { 0.0f, 40.0f, -10.0f });
  checkVector(same.forward, 0.0f, 1.0f, 0.0f);
  checkVector(same.up, 0.0f, 0.0f, 1.0f);

  VrGameCameraPose none = vrComputeTwoHandedPose(hand, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
  checkVector(none.forward, 0.0f, 1.0f, 0.0f);
}
