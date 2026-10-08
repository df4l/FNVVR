#include <cmath>

#include "test.h"

#include "../vr_math.h"
#include "../vr_openvr_convert.h"

using namespace dxvk;


// Rotating a vector with the converted quaternion must give the same result
// as multiplying it with the matrix, for every branch of the conversion
static void checkMatrix(const float m[3][4]) {
  VrPose pose = vrPoseFromMatrix34(m);

  const VrVector3 vectors[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.3f, -0.5f, 0.8f } };

  for (const VrVector3& v : vectors) {
    VrVector3 rotated = vrRotate(pose.orientation, v);

    CHECK_NEAR(rotated.x, m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, 1e-5);
    CHECK_NEAR(rotated.y, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, 1e-5);
    CHECK_NEAR(rotated.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z, 1e-5);
  }
}


TEST_CASE(identity_matrix_gives_identity_pose) {
  const float m[3][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 0, 1, 3 } };
  VrPose pose = vrPoseFromMatrix34(m);

  CHECK_NEAR(pose.orientation.w, 1.0, 1e-6);
  CHECK_NEAR(pose.position.x, 1.0, 1e-6);
  CHECK_NEAR(pose.position.y, 2.0, 1e-6);
  CHECK_NEAR(pose.position.z, 3.0, 1e-6);
}


TEST_CASE(matrix_rotations_match_in_every_branch) {
  // Positive trace: a quarter turn about +Y
  const float yaw[3][4] = { { 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { -1, 0, 0, 0 } };
  checkMatrix(yaw);

  // Half turns about each axis take the other three branches
  const float aboutX[3][4] = { { 1, 0, 0, 0 }, { 0, -1, 0, 0 }, { 0, 0, -1, 0 } };
  const float aboutY[3][4] = { { -1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, -1, 0 } };
  const float aboutZ[3][4] = { { -1, 0, 0, 0 }, { 0, -1, 0, 0 }, { 0, 0, 1, 0 } };
  checkMatrix(aboutX);
  checkMatrix(aboutY);
  checkMatrix(aboutZ);
}


TEST_CASE(forward_is_minus_z_after_a_left_turn) {
  // A quarter turn about +Y takes forward (-Z) to the left (-X)
  const float yaw[3][4] = { { 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { -1, 0, 0, 0 } };
  VrVector3 forward = vrRotate(vrPoseFromMatrix34(yaw).orientation, { 0, 0, -1 });

  CHECK_NEAR(forward.x, -1.0, 1e-5);
  CHECK_NEAR(forward.z, 0.0, 1e-5);
}


TEST_CASE(projection_raw_is_converted_with_negated_vertical_values) {
  VrFov fov = vrFovFromProjectionRaw(-1.0f, 1.2f, -1.5f, 1.4f);

  CHECK_NEAR(fov.angleLeft, -std::atan(1.0), 1e-6);
  CHECK_NEAR(fov.angleRight, std::atan(1.2), 1e-6);
  CHECK_NEAR(fov.angleUp, std::atan(1.5), 1e-6);
  CHECK_NEAR(fov.angleDown, -std::atan(1.4), 1e-6);
}


TEST_CASE(buttons_are_mapped_from_the_pressed_mask) {
  CHECK(vrButtonsFromOpenVr(0) == 0);
  CHECK(vrButtonsFromOpenVr(uint64_t(1) << VrOpenVrButtonId::A) == uint32_t(VrButton::Primary));
  CHECK(vrButtonsFromOpenVr(uint64_t(1) << VrOpenVrButtonId::ApplicationMenu) == uint32_t(VrButton::Secondary));
  CHECK(vrButtonsFromOpenVr(uint64_t(1) << VrOpenVrButtonId::Axis0) == uint32_t(VrButton::Stick));

  // The grip and trigger are analog values, not buttons of the layer
  CHECK(vrButtonsFromOpenVr((uint64_t(1) << VrOpenVrButtonId::Grip) | (uint64_t(1) << VrOpenVrButtonId::Axis1)) == 0);
}


TEST_CASE(haptic_pulse_scales_and_is_limited) {
  CHECK(vrHapticMicroseconds(1.0f, 2000000) == 2000);
  CHECK(vrHapticMicroseconds(0.5f, 2000000) == 1000);
  CHECK(vrHapticMicroseconds(1.0f, 100000000) == VrOpenVrMaxHapticMicroseconds);
  CHECK(vrHapticMicroseconds(-1.0f, 2000000) == 0);
  CHECK(vrHapticMicroseconds(1.0f, -5) == 0);
}
