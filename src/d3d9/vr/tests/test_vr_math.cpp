#include "test.h"

#include "../vr_math.h"

using namespace dxvk;

namespace {

  constexpr float Pi = 3.14159265358979f;

  VrVector3 project(const Matrix4& m, const VrVector3& p) {
    Vector4 clip = m * Vector4(p.x, p.y, p.z, 1.0f);
    return { clip.x / clip.w, clip.y / clip.w, clip.z / clip.w };
  }

}


TEST_CASE(quaternion_rotates_around_axis) {
  VrQuaternion q = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, Pi * 0.5f);
  VrVector3 v = vrRotate(q, { 1.0f, 0.0f, 0.0f });

  // A quarter turn around +Y maps +X onto -Z in a right-handed system
  CHECK_NEAR(v.x, 0.0f, 1e-5);
  CHECK_NEAR(v.y, 0.0f, 1e-5);
  CHECK_NEAR(v.z, -1.0f, 1e-5);
}


TEST_CASE(slerp_hits_endpoints_and_midpoint) {
  VrQuaternion a;
  VrQuaternion b = vrQuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, Pi * 0.5f);

  VrQuaternion mid = vrSlerp(a, b, 0.5f);
  VrVector3 v = vrRotate(mid, { 1.0f, 0.0f, 0.0f });
  CHECK_NEAR(v.x, std::cos(Pi * 0.25f), 1e-5);
  CHECK_NEAR(v.y, std::sin(Pi * 0.25f), 1e-5);

  CHECK_NEAR(vrSlerp(a, b, 0.0f).w, a.w, 1e-6);
  CHECK_NEAR(vrSlerp(a, b, 1.0f).z, b.z, 1e-6);
}


TEST_CASE(slerp_takes_shortest_arc) {
  VrQuaternion a;
  VrQuaternion b = vrQuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, Pi * 0.5f);
  VrQuaternion negated = { -b.x, -b.y, -b.z, -b.w };

  VrVector3 v = vrRotate(vrSlerp(a, negated, 0.5f), { 1.0f, 0.0f, 0.0f });
  CHECK_NEAR(v.x, std::cos(Pi * 0.25f), 1e-5);
  CHECK_NEAR(v.y, std::sin(Pi * 0.25f), 1e-5);
}


TEST_CASE(pose_inverse_cancels_pose) {
  VrPose pose;
  pose.orientation = vrQuaternionFromAxisAngle({ 1.0f, 2.0f, 3.0f }, 0.7f);
  pose.position    = { 1.0f, -2.0f, 0.5f };

  VrPose identity = vrCompose(pose, vrInverse(pose));
  CHECK_NEAR(identity.position.x, 0.0f, 1e-5);
  CHECK_NEAR(identity.position.y, 0.0f, 1e-5);
  CHECK_NEAR(identity.position.z, 0.0f, 1e-5);
  CHECK_NEAR(std::fabs(identity.orientation.w), 1.0f, 1e-5);
}


TEST_CASE(compose_applies_child_first) {
  VrPose parent;
  parent.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, Pi * 0.5f);
  parent.position    = { 10.0f, 0.0f, 0.0f };

  VrPose child;
  child.position = { 1.0f, 0.0f, 0.0f };

  VrVector3 composed = vrTransformPoint(vrCompose(parent, child), { 0.0f, 0.0f, 0.0f });
  VrVector3 stepwise = vrTransformPoint(parent, vrTransformPoint(child, { 0.0f, 0.0f, 0.0f }));

  CHECK_NEAR(composed.x, stepwise.x, 1e-5);
  CHECK_NEAR(composed.y, stepwise.y, 1e-5);
  CHECK_NEAR(composed.z, stepwise.z, 1e-5);
}


TEST_CASE(pose_matrix_matches_pose_transform) {
  VrPose pose;
  pose.orientation = vrQuaternionFromAxisAngle({ 0.3f, 1.0f, -0.2f }, 1.1f);
  pose.position    = { 0.5f, 1.5f, -2.0f };

  VrVector3 point = { 0.2f, -0.4f, 0.9f };
  VrVector3 expected = vrTransformPoint(pose, point);

  Vector4 viaMatrix = vrPoseToMatrix(pose) * Vector4(point.x, point.y, point.z, 1.0f);
  CHECK_NEAR(viaMatrix.x, expected.x, 1e-5);
  CHECK_NEAR(viaMatrix.y, expected.y, 1e-5);
  CHECK_NEAR(viaMatrix.z, expected.z, 1e-5);
}


TEST_CASE(view_matrix_puts_eye_at_origin) {
  VrPose head;
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.4f);
  head.position    = { 3.0f, 1.7f, -1.0f };

  VrPose eye;
  eye.position.x = 0.032f;

  Matrix4 view = vrComputeViewMatrix(eye, head);
  VrVector3 eyeWorld = vrTransformPoint(vrCompose(head, eye), { 0.0f, 0.0f, 0.0f });

  VrVector3 atOrigin = project(view, eyeWorld);
  CHECK_NEAR(atOrigin.x, 0.0f, 1e-5);
  CHECK_NEAR(atOrigin.y, 0.0f, 1e-5);
  CHECK_NEAR(atOrigin.z, 0.0f, 1e-5);
}


TEST_CASE(projection_maps_frustum_edges_to_clip_edges) {
  VrFov fov;
  fov.angleLeft  = -0.9f;
  fov.angleRight =  0.7f;
  fov.angleUp    =  0.8f;
  fov.angleDown  = -0.6f;

  const float nearZ = 0.1f, farZ = 100.0f;
  Matrix4 proj = vrComputeProjectionMatrix(fov, nearZ, farZ);

  // A point on the right edge of the frustum, 5 metres ahead
  float depth = 5.0f;
  VrVector3 right = project(proj, { std::tan(fov.angleRight) * depth, 0.0f, -depth });
  VrVector3 left  = project(proj, { std::tan(fov.angleLeft) * depth, 0.0f, -depth });
  VrVector3 up    = project(proj, { 0.0f, std::tan(fov.angleUp) * depth, -depth });
  VrVector3 down  = project(proj, { 0.0f, std::tan(fov.angleDown) * depth, -depth });

  CHECK_NEAR(right.x, 1.0f, 1e-5);
  CHECK_NEAR(left.x, -1.0f, 1e-5);
  CHECK_NEAR(up.y, 1.0f, 1e-5);
  CHECK_NEAR(down.y, -1.0f, 1e-5);
}


TEST_CASE(projection_depth_is_zero_to_one) {
  VrFov fov = { -0.8f, 0.8f, 0.8f, -0.8f };
  Matrix4 proj = vrComputeProjectionMatrix(fov, 0.1f, 100.0f);

  CHECK_NEAR(project(proj, { 0.0f, 0.0f, -0.1f }).z, 0.0f, 1e-5);
  CHECK_NEAR(project(proj, { 0.0f, 0.0f, -100.0f }).z, 1.0f, 1e-4);
}


TEST_CASE(union_fov_is_symmetric_and_covers_both_eyes) {
  VrFov left  = { -0.9f, 0.7f, 0.8f, -0.6f };
  VrFov right = { -0.7f, 0.9f, 0.8f, -0.6f };

  VrFov u = vrUnionFov(left, right);
  CHECK_NEAR(u.angleLeft, -0.9f, 1e-5);
  CHECK_NEAR(u.angleRight, 0.9f, 1e-5);
  CHECK_NEAR(u.angleUp, 0.8f, 1e-5);
  CHECK_NEAR(u.angleDown, -0.8f, 1e-5);
}


TEST_CASE(texture_bounds_cover_whole_target_for_union_fov) {
  VrFov fov = { -0.8f, 0.8f, 0.7f, -0.7f };
  VrTextureBounds b = vrComputeTextureBounds(fov, fov);

  CHECK_NEAR(b.uMin, 0.0f, 1e-5);
  CHECK_NEAR(b.uMax, 1.0f, 1e-5);
  CHECK_NEAR(b.vMin, 0.0f, 1e-5);
  CHECK_NEAR(b.vMax, 1.0f, 1e-5);
}


TEST_CASE(texture_bounds_shrink_for_narrower_eye) {
  VrFov eye   = { -0.5f, 0.8f, 0.7f, -0.7f };
  VrFov outer = { -0.8f, 0.8f, 0.7f, -0.7f };

  VrTextureBounds b = vrComputeTextureBounds(eye, outer);
  CHECK(b.uMin > 0.0f);
  CHECK_NEAR(b.uMax, 1.0f, 1e-5);
  CHECK_NEAR(b.vMin, 0.0f, 1e-5);
  CHECK_NEAR(b.vMax, 1.0f, 1e-5);
}
