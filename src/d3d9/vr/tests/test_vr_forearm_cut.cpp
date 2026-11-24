#include "test.h"

#include "../vr_forearm_cut.h"
#include "../vr_math.h"

using namespace dxvk;

namespace {

  /** Clip-space position of a world point: [p - origin, 1] * M */
  std::array<float, 4> toClip(const float m[4][4], const VrVector3& origin, const VrVector3& p) {
    float v[4] = { p.x - origin.x, p.y - origin.y, p.z - origin.z, 1.0f };
    std::array<float, 4> clip = { };

    for (uint32_t c = 0; c < 4; c++) {
      for (uint32_t r = 0; r < 4; r++)
        clip[c] += v[r] * m[r][c];
    }

    return clip;
  }

  float dot4(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  }

}


TEST_CASE(forearm_cut_lies_across_the_axis_from_the_wrist) {
  VrForearmCut cut = vrComputeForearmCut({ 0.0f, 0.0f, 0.0f }, { 20.0f, 0.0f, 0.0f }, 8.0f, 2.0f);
  CHECK(cut.valid);
  CHECK_NEAR(cut.normal.x, 1.0f, 1e-6);
  CHECK_NEAR(cut.point.x, 12.0f, 1e-5);
  CHECK_NEAR(cut.gather.x, 0.0f, 1e-6);

  // Never closer to the elbow than the margin
  cut = vrComputeForearmCut({ 0.0f, 0.0f, 0.0f }, { 20.0f, 0.0f, 0.0f }, 30.0f, 2.0f);
  CHECK_NEAR(cut.point.x, 2.0f, 1e-5);

  // Never beyond the wrist
  cut = vrComputeForearmCut({ 0.0f, 0.0f, 0.0f }, { 20.0f, 0.0f, 0.0f }, -5.0f, 2.0f);
  CHECK_NEAR(cut.point.x, 20.0f, 1e-5);

  cut = vrComputeForearmCut({ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, 8.0f, 2.0f);
  CHECK(!cut.valid);
}


TEST_CASE(distance_towards_elbow_is_measured_along_the_axis) {
  VrVector3 elbow = { 0.0f, 0.0f, 0.0f };
  VrVector3 wrist = { 0.0f, 20.0f, 0.0f };
  CHECK_NEAR(vrDistanceTowardsElbow(elbow, wrist, { 3.0f, 15.0f, -4.0f }), 5.0f, 1e-5);
  CHECK_NEAR(vrDistanceTowardsElbow(elbow, wrist, { 0.0f, 22.0f, 0.0f }), -2.0f, 1e-5);
}


TEST_CASE(clip_plane_keeps_the_hand_side_in_clip_space) {
  // A perspective-like matrix with a translation, row-vector convention
  const float m[4][4] = {
    { 1.2f,  0.1f,  0.0f,  0.0f },
    { 0.0f,  1.6f,  0.2f,  0.0f },
    { 0.3f,  0.0f,  1.01f, 1.0f },
    { 0.5f, -0.4f, -2.0f,  0.0f },
  };

  VrVector3 origin = { 100.0f, -50.0f, 30.0f };
  VrVector3 point  = { 104.0f, -48.0f, 40.0f };
  VrVector3 normal = { 0.6f, 0.0f, 0.8f };

  std::array<float, 4> plane = { };
  CHECK(vrComputeClipPlane(m, origin, point, normal, plane));

  // The clip-space test equals the world distance to the plane, scaled
  const VrVector3 samples[] = {
    { 110.0f, -40.0f, 45.0f },
    {  90.0f, -55.0f, 20.0f },
    { 104.0f,  10.0f, 40.0f },
    { 100.6f, -48.0f, 42.55f },
  };

  for (const VrVector3& p : samples) {
    float world = vrDot(normal, p - point);
    float clip  = dot4(plane, toClip(m, origin, p));
    CHECK_NEAR(clip, world, 1e-3);
  }

  const float singular[4][4] = { };
  CHECK(!vrComputeClipPlane(singular, origin, point, normal, plane));
}
