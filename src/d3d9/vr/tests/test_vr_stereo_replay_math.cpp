#include <cmath>

#include "test.h"

#include "../vr_math.h"
#include "../vr_stereo_replay_math.h"

using namespace dxvk;

namespace {

  constexpr float NearPlane = 10.0f;
  constexpr float FarPlane  = 300000.0f;

  // Looking north (+Y) with Z up, tilted down a little
  VrGameCameraPose camera(const VrVector3& position) {
    VrGameCameraPose pose;
    float pitch = -0.2f;
    pose.forward  = { 0.0f, std::cos(pitch), std::sin(pitch) };
    pose.up       = { 0.0f, -std::sin(pitch), std::cos(pitch) };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = position;
    return pose;
  }

  VrGameFrustum leftFrustum() {
    return { -1.0f, 0.85f, 0.95f, -1.1f };
  }

  VrGameFrustum rightFrustum() {
    return { -0.85f, 1.0f, 0.95f, -1.1f };
  }

  // The game's projection, with the depth range LOD Flicker Fix reverses
  std::array<float, 16> projection(const VrGameFrustum& frustum, bool reversedDepth) {
    auto proj = vrComputeGameProjection(frustum, NearPlane, FarPlane);

    if (reversedDepth) {
      proj[10] = -NearPlane / (FarPlane - NearPlane);
      proj[14] = NearPlane * FarPlane / (FarPlane - NearPlane);
    }

    return proj;
  }

  // Clip position of a world point as the game draws it, relative to the camera
  std::array<double, 4> clip(const VrStereoEyeCamera& eye, const VrVector3& point, bool reversedDepth) {
    auto view = vrComputeGameView(eye.pose);
    auto proj = projection(eye.frustum, reversedDepth);

    VrVector3 p = point - eye.pose.position;
    std::array<double, 4> v = { p.x, p.y, p.z, 1.0 };
    std::array<double, 4> viewPos = { };
    std::array<double, 4> result = { };

    for (uint32_t j = 0; j < 4; j++) {
      for (uint32_t k = 0; k < 4; k++)
        viewPos[j] += v[k] * view[k * 4 + j];
    }

    for (uint32_t j = 0; j < 4; j++) {
      for (uint32_t k = 0; k < 4; k++)
        result[j] += viewPos[k] * proj[k * 4 + j];
    }

    return result;
  }

  VrRendererCamera rendererFor(const VrStereoEyeCamera& eye, bool reversedDepth = false) {
    VrRendererCamera renderer;
    renderer.view = vrComputeGameView(eye.pose);
    renderer.projection = projection(eye.frustum, reversedDepth);
    return renderer;
  }

  void checkReplay(const VrStereoEyeCamera& left, const VrStereoEyeCamera& right, const VrVector3& point,
      bool reversedDepth = false) {
    std::array<float, 16> rows = { };
    std::string mismatch;
    CHECK(vrComputeStereoReplayMatrix(rendererFor(left, reversedDepth), left, right, rows, mismatch));

    auto l = clip(left, point, reversedDepth);
    auto r = clip(right, point, reversedDepth);

    std::array<double, 4> moved = { };

    for (uint32_t i = 0; i < 4; i++) {
      for (uint32_t j = 0; j < 4; j++)
        moved[i] += double(rows[i * 4 + j]) * l[j];
    }

    // Compare after the perspective divide, as the rasterizer sees them
    CHECK_NEAR(moved[0] / moved[3], r[0] / r[3], 1e-4);
    CHECK_NEAR(moved[1] / moved[3], r[1] / r[3], 1e-4);
    CHECK_NEAR(moved[2] / moved[3], r[2] / r[3], 1e-5);
    CHECK_NEAR(moved[3], r[3], 1e-3 * std::abs(r[3]));
  }

}


TEST_CASE(stereo_replay_moves_points_to_the_right_eye) {
  VrStereoEyeCamera left  = { camera({ 1000.0f, 2000.0f, 500.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 1004.5f, 2000.0f, 500.0f }), rightFrustum() };

  checkReplay(left, right, { 1000.0f, 2020.0f, 498.0f });
  checkReplay(left, right, { 1100.0f, 2500.0f, 400.0f });
  checkReplay(left, right, { -20000.0f, 90000.0f, 3000.0f });
}


TEST_CASE(stereo_replay_keeps_a_reversed_depth_range) {
  VrStereoEyeCamera left  = { camera({ 1000.0f, 2000.0f, 500.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 1004.5f, 2000.0f, 500.0f }), rightFrustum() };

  checkReplay(left, right, { 1000.0f, 2020.0f, 498.0f }, true);
  checkReplay(left, right, { -20000.0f, 90000.0f, 3000.0f }, true);
}


TEST_CASE(stereo_replay_follows_canted_eyes) {
  VrStereoEyeCamera left  = { camera({ 0.0f, 0.0f, 0.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 4.5f, 0.2f, -0.1f }), rightFrustum() };

  // Right eye turned 5 degrees outwards about the up axis
  float angle = 0.087f;
  VrVector3 forward = right.pose.forward;
  VrVector3 sideways = right.pose.right;
  right.pose.forward = forward * std::cos(angle) + sideways * std::sin(angle);
  right.pose.right   = sideways * std::cos(angle) - forward * std::sin(angle);

  checkReplay(left, right, { 3.0f, 40.0f, -5.0f });
  checkReplay(left, right, { -300.0f, 2000.0f, 100.0f });
}


TEST_CASE(stereo_replay_is_the_identity_for_one_eye) {
  VrStereoEyeCamera eye = { camera({ 10.0f, 20.0f, 30.0f }), leftFrustum() };

  std::array<float, 16> rows = { };
  std::string mismatch;
  CHECK(vrComputeStereoReplayMatrix(rendererFor(eye), eye, eye, rows, mismatch));

  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++)
      CHECK_NEAR(rows[i * 4 + j], i == j ? 1.0f : 0.0f, 1e-5);
  }
}


TEST_CASE(stereo_replay_refuses_another_camera) {
  VrStereoEyeCamera left  = { camera({ 0.0f, 0.0f, 0.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 4.5f, 0.0f, 0.0f }), rightFrustum() };

  std::array<float, 16> rows = { };
  std::string mismatch;

  // Another frustum, as a reflection camera or a menu's would have
  VrRendererCamera other = rendererFor({ left.pose, { -0.5f, 0.5f, 0.5f, -0.5f } });
  CHECK(!vrComputeStereoReplayMatrix(other, left, right, rows, mismatch));
  CHECK(!mismatch.empty());

  // Another orientation
  VrStereoEyeCamera turned = left;
  turned.pose.forward = { 1.0f, 0.0f, 0.0f };
  turned.pose.right   = { 0.0f, -1.0f, 0.0f };
  turned.pose.up      = { 0.0f, 0.0f, 1.0f };
  CHECK(!vrComputeStereoReplayMatrix(rendererFor(turned), left, right, rows, mismatch));

  // An orthographic projection, as the interface's
  VrRendererCamera flat = rendererFor(left);
  flat.projection[11] = 0.0f;
  flat.projection[15] = 1.0f;
  CHECK(!vrComputeStereoReplayMatrix(flat, left, right, rows, mismatch));
}


namespace {

  // Whether a point is inside a frustum starting at a camera, sides only
  bool insideSides(const VrGameCameraPose& pose, const VrGameFrustum& frustum, const VrVector3& point) {
    VrVector3 p = point - pose.position;
    float z = vrDot(p, pose.forward);

    if (z <= 0.0f)
      return false;

    float x = vrDot(p, pose.right) / z;
    float y = vrDot(p, pose.up) / z;
    float slack = 1e-4f;

    return x >= frustum.left - slack && x <= frustum.right + slack
        && y >= frustum.bottom - slack && y <= frustum.top + slack;
  }

  // Points along the edges of an eye's frustum, near and far
  void checkEdgesInside(const VrStereoEyeCamera& eye, const VrGameCameraPose& pose, const VrGameFrustum& frustum) {
    const float distances[] = { 0.5f, 10.0f, 1000.0f };
    const float xs[] = { eye.frustum.left, 0.0f, eye.frustum.right };
    const float ys[] = { eye.frustum.bottom, 0.0f, eye.frustum.top };

    for (float d : distances) {
      for (float x : xs) {
        for (float y : ys) {
          VrVector3 point = eye.pose.position + eye.pose.forward * d
            + eye.pose.right * (x * d) + eye.pose.up * (y * d);
          CHECK(insideSides(pose, frustum, point));
        }
      }
    }
  }

}


TEST_CASE(stereo_culling_view_holds_both_eyes) {
  VrStereoEyeCamera left  = { camera({ 100.0f, 200.0f, 50.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 104.5f, 200.0f, 50.0f }), rightFrustum() };

  VrGameCameraPose pose;
  VrGameFrustum frustum;
  CHECK(vrComputeStereoCullingView(left, right, pose, frustum));

  checkEdgesInside(left, pose, frustum);
  checkEdgesInside(right, pose, frustum);

  // Widest of both sides, a few units behind the eyes, same axes
  CHECK_NEAR(frustum.left, -1.0f, 1e-5);
  CHECK_NEAR(frustum.right, 1.0f, 1e-5);
  float back = vrDot(left.pose.position - pose.position, left.pose.forward);
  CHECK(back > 0.0f && back < 4.0f);
  CHECK_NEAR(vrDot(pose.forward, left.pose.forward), 1.0f, 1e-6);
}


TEST_CASE(stereo_culling_view_holds_canted_eyes) {
  VrStereoEyeCamera left  = { camera({ 0.0f, 0.0f, 0.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 4.5f, 0.2f, -0.1f }), rightFrustum() };

  float angle = 0.087f;
  VrVector3 forward = right.pose.forward;
  VrVector3 sideways = right.pose.right;
  right.pose.forward = forward * std::cos(angle) + sideways * std::sin(angle);
  right.pose.right   = sideways * std::cos(angle) - forward * std::sin(angle);

  VrGameCameraPose pose;
  VrGameFrustum frustum;
  CHECK(vrComputeStereoCullingView(left, right, pose, frustum));

  checkEdgesInside(left, pose, frustum);
  checkEdgesInside(right, pose, frustum);
}


TEST_CASE(stereo_culling_view_refuses_odd_frustums) {
  VrStereoEyeCamera left  = { camera({ 0.0f, 0.0f, 0.0f }), leftFrustum() };
  VrStereoEyeCamera right = { camera({ 4.5f, 0.0f, 0.0f }), { 0.1f, 1.0f, 0.95f, -1.1f } };

  VrGameCameraPose pose;
  VrGameFrustum frustum;
  CHECK(!vrComputeStereoCullingView(left, right, pose, frustum));
}
