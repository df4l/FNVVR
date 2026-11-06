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


TEST_CASE(vats_target_stays_in_front_of_a_head_turned_in_the_room) {
  // The game turned its camera to a target to the east, looking down at it,
  // and the player's head had been turned 90 degrees to the left of the
  // reference when V.A.T.S. began
  float pitch = 0.3f;

  VrGameCameraPose game;
  game.forward  = { std::cos(pitch), 0.0f, -std::sin(pitch) };
  game.up       = { std::sin(pitch), 0.0f, std::cos(pitch) };
  game.right    = { 0.0f, -1.0f, 0.0f };
  game.position = { 1.0f, 2.0f, 3.0f };

  VrPose head;
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, Pi / 2.0f);

  float yaw = vrComputeHeadAngles(head).yaw;
  CHECK_NEAR(yaw, -Pi / 2.0f, 1e-4);

  VrGameCameraPose base = vrComputeVatsCameraPose(game, yaw);
  VrGameCameraPose eye  = vrComputeEyeCameraPose(base, head, 1.0f);

  // That head sees the target's heading, with the horizon level
  checkVector(eye.forward, 1.0f, 0.0f, 0.0f);
  checkVector(eye.up, 0.0f, 0.0f, 1.0f);
  checkVector(base.position, 1.0f, 2.0f, 3.0f);
}


TEST_CASE(vats_camera_looking_straight_down_keeps_its_heading) {
  VrGameCameraPose game;
  game.forward = { 0.0f, 0.0f, -1.0f };
  game.up      = { 1.0f, 0.0f, 0.0f };
  game.right   = { 0.0f, -1.0f, 0.0f };

  checkVector(vrComputeVatsCameraPose(game, 0.0f).forward, 1.0f, 0.0f, 0.0f);
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


TEST_CASE(head_angles_follow_the_game_conventions) {
  // Looking ahead
  VrHeadAngles ahead = vrComputeHeadAngles(VrPose());
  CHECK_NEAR(ahead.yaw, 0.0f, 1e-6);
  CHECK_NEAR(ahead.pitch, 0.0f, 1e-6);

  // Turning the head right is a positive (clockwise) heading change
  VrPose right;
  right.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, -0.5f);
  CHECK_NEAR(vrComputeHeadAngles(right).yaw, 0.5f, 1e-5);

  // Looking up is a negative pitch in the game
  VrPose up;
  up.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, 0.3f);
  VrHeadAngles upAngles = vrComputeHeadAngles(up);
  CHECK_NEAR(upAngles.pitch, -0.3f, 1e-5);
  CHECK_NEAR(upAngles.yaw, 0.0f, 1e-5);

  // Roll does not change the heading
  VrPose roll;
  roll.orientation = vrQuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, 0.4f);
  CHECK_NEAR(vrComputeHeadAngles(roll).yaw, 0.0f, 1e-5);
}


TEST_CASE(body_camera_faces_the_heading) {
  VrGameCameraPose north = vrComputeBodyCameraPose(0.0f, { 1.0f, 2.0f, 3.0f });
  CHECK_NEAR(north.forward.y, 1.0f, 1e-6);
  CHECK_NEAR(north.right.x, 1.0f, 1e-6);
  CHECK_NEAR(north.up.z, 1.0f, 1e-6);
  CHECK_NEAR(north.position.z, 3.0f, 1e-6);

  VrGameCameraPose east = vrComputeBodyCameraPose(1.5707964f, { });
  CHECK_NEAR(east.forward.x, 1.0f, 1e-6);
  CHECK_NEAR(east.right.y, -1.0f, 1e-6);

  // Same handedness as the game camera: right = forward x up
  VrVector3 cross = {
    east.forward.y * east.up.z - east.forward.z * east.up.y,
    east.forward.z * east.up.x - east.forward.x * east.up.z,
    east.forward.x * east.up.y - east.forward.y * east.up.x,
  };
  CHECK_NEAR(cross.x, east.right.x, 1e-6);
  CHECK_NEAR(cross.y, east.right.y, 1e-6);
}


TEST_CASE(heading_wraps_into_one_turn) {
  CHECK_NEAR(vrWrapHeading(-0.5f), 6.2831853f - 0.5f, 1e-5);
  CHECK_NEAR(vrWrapHeading(7.0f), 7.0f - 6.2831853f, 1e-5);
  CHECK_NEAR(vrWrapHeading(1.0f), 1.0f, 1e-6);
  CHECK(vrWrapHeading(-1e-9f) < 6.2831853f);
}


TEST_CASE(turning_carries_a_point_around_the_pivot) {
  // A point north of the pivot ends up east of it after a quarter turn
  VrVector3 east = vrTurnAroundPivot({ 10.0f, 25.0f, 7.0f }, { 10.0f, 20.0f, 0.0f }, 0.5f * Pi);
  CHECK_NEAR(east.x, 15.0f, 1e-5);
  CHECK_NEAR(east.y, 20.0f, 1e-5);
  CHECK_NEAR(east.z, 7.0f, 1e-6);

  // Turning back restores the point
  VrVector3 back = vrTurnAroundPivot(east, { 10.0f, 20.0f, 0.0f }, -0.5f * Pi);
  CHECK_NEAR(back.x, 10.0f, 1e-5);
  CHECK_NEAR(back.y, 25.0f, 1e-5);

  // The pivot itself does not move
  VrVector3 pivot = vrTurnAroundPivot({ 3.0f, 4.0f, 5.0f }, { 3.0f, 4.0f, 0.0f }, 1.0f);
  CHECK_NEAR(pivot.x, 3.0f, 1e-6);
  CHECK_NEAR(pivot.y, 4.0f, 1e-6);
}


TEST_CASE(culling_fov_covers_both_eyes_with_a_margin) {
  // Canted eyes: each one sees further on its own side
  VrFov left  = { -0.9f, 0.7f, 0.8f, -0.85f };
  VrFov right = { -0.7f, 0.9f, 0.75f, -0.9f };

  VrFov fov = vrComputeCullingFov(left, right, 0.1f);
  CHECK_NEAR(fov.angleLeft,  -1.0f,  1e-6);
  CHECK_NEAR(fov.angleRight,  1.0f,  1e-6);
  CHECK_NEAR(fov.angleUp,     0.9f,  1e-6);
  CHECK_NEAR(fov.angleDown,  -1.0f,  1e-6);
}


TEST_CASE(culling_fov_stays_below_a_right_angle) {
  VrFov wide = { -1.5f, 1.5f, 1.4f, -1.4f };

  VrFov fov = vrComputeCullingFov(wide, wide, 0.2f);
  CHECK_NEAR(fov.angleLeft,  -VrMaxCullingAngle, 1e-6);
  CHECK_NEAR(fov.angleRight,  VrMaxCullingAngle, 1e-6);
  CHECK_NEAR(fov.angleUp,     VrMaxCullingAngle, 1e-6);
  CHECK_NEAR(fov.angleDown,  -VrMaxCullingAngle, 1e-6);
}


TEST_CASE(game_point_comes_back_into_the_reference) {
  VrGameCameraPose base;
  base.forward  = { 1.0f, 0.0f, 0.0f };
  base.up       = { 0.0f, 0.0f, 1.0f };
  base.right    = { 0.0f, -1.0f, 0.0f };
  base.position = { 10.0f, 20.0f, 30.0f };

  // An eye moved to the point found is drawn at that point
  VrPose eye;
  eye.position = vrComputeGamePointInReference(base, { 210.0f, -30.0f, 10.0f }, 100.0f);
  checkVector(eye.position, 0.5f, -0.2f, -2.0f);
  checkVector(vrComputeEyeCameraPose(base, eye, 100.0f).position, 210.0f, -30.0f, 10.0f);
}


TEST_CASE(menu_scene_camera_sees_the_target_at_the_anchor) {
  // The anchor 1 m ahead of the origin along -Z, facing back
  VrPose anchor;
  anchor.position = { 0.0f, 1.5f, -1.0f };

  VrPose eye;
  eye.position = { 0.0f, 1.5f, 0.0f };

  VrVector3 target = { 20.0f, 0.5f, 0.0f };

  // Straight in front: the camera looks along +X, 10 units before the target
  VrGameCameraPose pose = vrComputeMenuSceneCameraPose(anchor, eye, target, 10.0f);
  checkVector(pose.position, 10.0f, 0.5f, 0.0f);
  checkVector(pose.forward, 1.0f, 0.0f, 0.0f);
  checkVector(pose.up,      0.0f, 1.0f, 0.0f);
  checkVector(pose.right,   0.0f, 0.0f, 1.0f);

  // An eye to the right and above is to the camera's right (+Z) and up (+Y)
  eye.position = { 0.1f, 1.7f, 0.0f };
  pose = vrComputeMenuSceneCameraPose(anchor, eye, target, 10.0f);
  checkVector(pose.position, 10.0f, 2.5f, 1.0f);

  // Turning the head left turns the camera towards -Z
  eye.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.5f * Pi);
  pose = vrComputeMenuSceneCameraPose(anchor, eye, target, 10.0f);
  checkVector(pose.forward, 0.0f, 0.0f, -1.0f);
  checkVector(pose.right,   1.0f, 0.0f,  0.0f);
}


TEST_CASE(menu_scene_camera_follows_a_turned_anchor) {
  // The anchor to the viewer's right (+X), turned to face the origin
  VrPose anchor;
  anchor.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, -0.5f * Pi);
  anchor.position    = { 2.0f, 0.0f, 0.0f };

  VrPose eye;
  eye.orientation = anchor.orientation;

  VrGameCameraPose pose = vrComputeMenuSceneCameraPose(anchor, eye, { 20.0f, 0.0f, 0.0f }, 5.0f);
  checkVector(pose.position, 10.0f, 0.0f, 0.0f);
  checkVector(pose.forward, 1.0f, 0.0f, 0.0f);
}


TEST_CASE(menu_scene_points_map_back_to_the_room) {
  VrPose anchor;
  anchor.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.3f * Pi);
  anchor.position    = { 0.5f, 1.4f, -0.8f };

  VrVector3 target = { 20.0f, 0.38f, 0.0f };

  // The eye's camera in the menu's space is where the eye is in the room
  VrPose eye;
  eye.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -0.2f);
  eye.position    = { 0.1f, 1.7f, 0.2f };

  VrGameCameraPose camera = vrComputeMenuSceneCameraPose(anchor, eye, target, 9.6f);
  checkVector(vrComputeMenuScenePointInRoom(anchor, target, 9.6f, camera.position),
    eye.position.x, eye.position.y, eye.position.z);

  // And its forward direction is the eye's
  VrVector3 forward = vrRotate(eye.orientation, { 0.0f, 0.0f, -1.0f });
  checkVector(vrComputeMenuSceneDirectionInRoom(anchor, camera.forward), forward.x, forward.y, forward.z);
}


TEST_CASE(head_view_is_above_and_in_front_of_the_head_bone) {
  // A body facing north, as the skeleton's head bone in its rest pose
  const float rotation[9] = {
    0.0f, 0.0f, -1.0f,
    0.0f, 1.0f,  0.0f,
    1.0f, 0.0f,  0.0f };

  VrVector3 eyes = vrComputeHeadViewPosition({ 10.0f, 20.0f, 100.0f }, rotation, 2.0f);

  checkVector(eyes, 10.0f, 36.0f, 112.0f);
}


TEST_CASE(camera_blend_continues_from_the_last_frame) {
  VrCameraBlend blend;

  VrGameCameraPose first = vrComputeBodyCameraPose(0.0f, { 0.0f, 0.0f, 100.0f });
  checkVector(blend.update(first, false, 0.01f).position, 0.0f, 0.0f, 100.0f);

  // Without a change, the camera is followed as it is
  VrGameCameraPose moved = vrComputeBodyCameraPose(0.0f, { 5.0f, 0.0f, 100.0f });
  checkVector(blend.update(moved, false, 0.01f).position, 5.0f, 0.0f, 100.0f);

  // A change starts from what was shown, without a jump
  VrGameCameraPose other = vrComputeBodyCameraPose(0.5f, { 5.0f, 0.0f, 70.0f });
  VrGameCameraPose start = blend.update(other, true, 0.0f);
  checkVector(start.position, 5.0f, 0.0f, 100.0f);
  checkVector(start.forward, 0.0f, 1.0f, 0.0f);
  checkVector(start.right, 1.0f, 0.0f, 0.0f);

  // Halfway there, on the way to the new heading
  VrGameCameraPose half = blend.update(other, false, 0.1f);
  CHECK(half.position.z < 100.0f && half.position.z > 70.0f);
  float heading = vrComputeCameraHeading(half);
  CHECK(heading > 0.0f && heading < 0.5f);

  // And it gets there
  VrGameCameraPose end = blend.update(other, false, 5.0f);
  checkVector(end.position, 5.0f, 0.0f, 70.0f);
  CHECK_NEAR(vrComputeCameraHeading(end), 0.5f, 1e-4);
}


TEST_CASE(head_heading_is_where_the_face_looks) {
  // Column 1 is the face, facing east (heading of 90 degrees)
  const float east[9] = {
    0.0f, 1.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };

  float heading = -1.0f;
  CHECK(vrComputeHeadHeading(east, heading));
  CHECK_NEAR(heading, 1.5707963f, 1e-4);

  // Looking straight down, it says nothing
  const float down[9] = {
    0.0f,  0.0f, 1.0f,
    1.0f,  0.0f, 0.0f,
    0.0f, -1.0f, 0.0f,
  };

  heading = -1.0f;
  CHECK(!vrComputeHeadHeading(down, heading));
  CHECK_NEAR(heading, -1.0f, 1e-6);
}

TEST_CASE(head_tilt_follows_a_head_lying_on_its_back) {
  // Standing, facing east: top up, face east
  const float standing[9] = {
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 0.0f,
  };

  float heading = -1.0f;
  float pitch   = -1.0f;
  CHECK(vrComputeHeadTilt(standing, heading, pitch));
  CHECK_NEAR(heading, 1.5707963f, 1e-4);
  CHECK_NEAR(pitch, 0.0f, 1e-4);

  // Lying on the back, the top of the head to the west: the face looks up,
  // and sitting up would face east
  const float lying[9] = {
    -1.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 1.0f,
     0.0f, 1.0f, 0.0f,
  };

  CHECK(vrComputeHeadTilt(lying, heading, pitch));
  CHECK_NEAR(heading, 1.5707963f, 1e-4);
  CHECK_NEAR(pitch, 1.5707963f, 1e-4);

  // The camera built from it looks up, and gives that heading back
  VrGameCameraPose pose = vrComputeTiltedCameraPose(heading, pitch, { 0.0f, 0.0f, 0.0f });
  checkVector(pose.forward, 0.0f, 0.0f, 1.0f);
  checkVector(pose.up, -1.0f, 0.0f, 0.0f);
  checkVector(pose.right, 0.0f, -1.0f, 0.0f);
  CHECK_NEAR(vrComputeCameraHeading(pose), 1.5707963f, 1e-4);

  // On its side, the top of the head to the east, it says nothing
  const float onSide[9] = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };

  heading = -1.0f;
  pitch   = -1.0f;
  CHECK(!vrComputeHeadTilt(onSide, heading, pitch));
  CHECK_NEAR(heading, -1.0f, 1e-6);
}


TEST_CASE(view_heading_starts_where_the_view_is) {
  VrViewHeading heading;

  // The head is turned in the room: the view has nothing to do with the body
  heading.start(3.0f, 1.0f);
  CHECK_NEAR(heading.update(3.0f, 0.0f, 0.01f), 1.0f, 1e-4);

  // The body turns nearly around, the view with it
  CHECK_NEAR(heading.update(3.0f + 3.0f, 0.0f, 0.01f), 1.0f + 3.0f, 1e-4);
}

TEST_CASE(view_heading_turns_the_short_way_softly) {
  VrViewHeading heading;
  heading.start(3.0f, 3.0f);

  // Across the wrap of the angle: 3.0 to -3.0 is about a third of a radian
  float soft = heading.update(-3.0f, 0.1f, 0.05f);
  CHECK(soft > 3.0f && soft < 3.2831853f);

  float settled = heading.update(-3.0f, 0.1f, 10.0f);
  CHECK_NEAR(std::remainder(settled + 3.0f, 6.2831853f), 0.0f, 1e-3);
}

TEST_CASE(view_heading_keeps_what_something_else_gave_it) {
  VrViewHeading heading;
  heading.start(0.0f, 0.0f);

  heading.keep(0.7f);
  CHECK_NEAR(heading.update(0.0f, 0.0f, 0.01f), 0.7f, 1e-4);
  CHECK_NEAR(heading.update(0.5f, 0.0f, 0.01f), 1.2f, 1e-4);
}

TEST_CASE(camera_blend_cuts_when_too_far) {
  VrCameraBlend blend;

  blend.update(vrComputeBodyCameraPose(0.0f, { 0.0f, 0.0f, 0.0f }), false, 0.01f);

  VrGameCameraPose result = blend.update(vrComputeBodyCameraPose(1.0f, { 500.0f, 0.0f, 0.0f }), true, 0.01f);

  checkVector(result.position, 500.0f, 0.0f, 0.0f);
  CHECK_NEAR(vrComputeCameraHeading(result), 1.0f, 1e-4);
}
