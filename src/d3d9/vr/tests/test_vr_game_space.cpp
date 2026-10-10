#include "test.h"

#include "../vr_game_space.h"

using namespace dxvk;

namespace {

  constexpr float Pi = 3.14159265358979f;

  // Facing north (+Y) with Z up, so the right-hand side is east (+X)
  VrGameCameraPose northFacingCamera() {
    VrGameCameraPose pose;
    pose.forward  = { 0.0f, 1.0f, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = { 100.0f, 200.0f, 300.0f };
    return pose;
  }

  void checkVector(const VrVector3& v, float x, float y, float z) {
    CHECK_NEAR(v.x, x, 1e-4);
    CHECK_NEAR(v.y, y, 1e-4);
    CHECK_NEAR(v.z, z, 1e-4);
  }

}


TEST_CASE(eye_at_reference_keeps_base_pose) {
  VrGameCameraPose base = northFacingCamera();
  VrGameCameraPose pose = vrComputeEyeCameraPose(base, VrPose(), VrGameUnitsPerMetre);

  checkVector(pose.forward, 0.0f, 1.0f, 0.0f);
  checkVector(pose.up, 0.0f, 0.0f, 1.0f);
  checkVector(pose.right, 1.0f, 0.0f, 0.0f);
  checkVector(pose.position, 100.0f, 200.0f, 300.0f);
}


TEST_CASE(eye_offset_moves_along_camera_axes) {
  VrPose eye;
  eye.position = { 0.032f, 0.01f, -0.5f };

  VrGameCameraPose pose = vrComputeEyeCameraPose(northFacingCamera(), eye, 100.0f);

  // Right is east, up is +Z, forward is north
  checkVector(pose.position, 100.0f + 3.2f, 200.0f + 50.0f, 300.0f + 1.0f);
}


TEST_CASE(eye_offset_follows_a_rotated_base_camera) {
  // Camera turned to face east (+X): right is now south (-Y)
  VrGameCameraPose base;
  base.forward  = { 1.0f, 0.0f, 0.0f };
  base.up       = { 0.0f, 0.0f, 1.0f };
  base.right    = { 0.0f, -1.0f, 0.0f };
  base.position = { 0.0f, 0.0f, 0.0f };

  VrPose eye;
  eye.position = { 1.0f, 0.0f, 0.0f };

  VrGameCameraPose pose = vrComputeEyeCameraPose(base, eye, 1.0f);
  checkVector(pose.position, 0.0f, -1.0f, 0.0f);
}


TEST_CASE(turning_the_head_left_turns_the_camera_left) {
  VrPose eye;
  eye.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, Pi * 0.5f);

  VrGameCameraPose pose = vrComputeEyeCameraPose(northFacingCamera(), eye, 1.0f);

  // A positive turn around +Y points the head towards -X, which is west
  checkVector(pose.forward, -1.0f, 0.0f, 0.0f);
  checkVector(pose.up, 0.0f, 0.0f, 1.0f);
  checkVector(pose.right, 0.0f, 1.0f, 0.0f);
}


TEST_CASE(looking_up_tilts_the_camera_up) {
  VrPose eye;
  eye.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, Pi * 0.5f);

  VrGameCameraPose pose = vrComputeEyeCameraPose(northFacingCamera(), eye, 1.0f);

  checkVector(pose.forward, 0.0f, 0.0f, 1.0f);
  checkVector(pose.up, 0.0f, -1.0f, 0.0f);
}


TEST_CASE(game_axes_stay_right_handed) {
  VrPose eye;
  eye.orientation = vrNormalize({ 0.2f, 0.5f, -0.3f, 0.8f });

  VrGameCameraPose pose = vrComputeEyeCameraPose(northFacingCamera(), eye, 1.0f);

  // right = forward x up
  VrVector3 cross = {
    pose.forward.y * pose.up.z - pose.forward.z * pose.up.y,
    pose.forward.z * pose.up.x - pose.forward.x * pose.up.z,
    pose.forward.x * pose.up.y - pose.forward.y * pose.up.x };
  checkVector(cross, pose.right.x, pose.right.y, pose.right.z);
}


TEST_CASE(recenter_reference_cancels_the_initial_head_pose) {
  VrPose head;
  head.position    = { 0.3f, 1.7f, -0.2f };
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.6f);

  VrPose reference = vrMakeRecenterReference(head);
  VrPose relative  = vrComputeEyeInReference(reference, head);

  CHECK_NEAR(vrLength(relative.position), 0.0f, 1e-5);
  CHECK_NEAR(std::fabs(relative.orientation.w), 1.0f, 1e-5);
}


TEST_CASE(recenter_reference_keeps_the_head_pitch) {
  VrQuaternion yaw   = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.9f);
  VrQuaternion pitch = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, 0.4f);

  VrPose head;
  head.orientation = yaw * pitch;

  VrPose relative = vrComputeEyeInReference(vrMakeRecenterReference(head), head);

  // Only the pitch remains, no heading
  VrVector3 forward = vrRotate(relative.orientation, { 0.0f, 0.0f, -1.0f });
  CHECK_NEAR(forward.x, 0.0f, 1e-5);
  CHECK(forward.y > 0.3f);
}


TEST_CASE(frustum_uses_tangents_of_the_field_of_view) {
  VrFov fov;
  fov.angleLeft  = -Pi * 0.25f;
  fov.angleRight =  Pi * 0.25f;
  fov.angleUp    =  Pi * 0.25f;
  fov.angleDown  = -Pi * 0.25f;

  VrGameFrustum frustum = vrComputeGameFrustum(fov);
  CHECK_NEAR(frustum.left, -1.0f, 1e-5);
  CHECK_NEAR(frustum.right, 1.0f, 1e-5);
  CHECK_NEAR(frustum.top, 1.0f, 1e-5);
  CHECK_NEAR(frustum.bottom, -1.0f, 1e-5);
}


TEST_CASE(narrow_eye_is_widened_to_the_interface_aspect) {
  VrExtent size = vrComputeGameResolution({ 1832, 1920 }, VrGameMinAspect);

  CHECK(size.height == 1920);
  CHECK(size.width  == 2560);
}


TEST_CASE(wide_eye_keeps_its_size_and_width_is_even) {
  VrExtent size = vrComputeGameResolution({ 2501, 1400 }, VrGameMinAspect);

  CHECK(size.height == 1400);
  CHECK(size.width  == 2502);
}
