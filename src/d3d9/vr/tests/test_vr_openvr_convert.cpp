#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

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


TEST_CASE(action_paths_are_in_the_set_of_their_context) {
  for (uint32_t i = 0; i < VrActionCount; i++) {
    std::string path = vrOpenVrActionPath(VrAction(i));
    std::string set  = VrOpenVrActionSets[uint32_t(vrOpenVrActionContext(VrAction(i)))];

    CHECK(path.compare(0, set.size() + 4, set + "/in/") == 0);

    for (uint32_t j = 0; j < i; j++)
      CHECK(path != vrOpenVrActionPath(VrAction(j)));
  }

  CHECK(vrOpenVrActionContext(VrAction::Grab) == VrInputContext::Game);
  CHECK(vrOpenVrActionContext(VrAction::MenuSelect) == VrInputContext::Menu);
  CHECK(vrOpenVrStickPath(VrInputContext::Menu, true) == nullptr);
}


TEST_CASE(manifest_declares_every_action) {
  std::ifstream file(VR_OPENVR_DIR "/actions.json");
  CHECK(file.good());

  std::stringstream stream;
  stream << file.rdbuf();
  std::string manifest = stream.str();

  auto declared = [&manifest] (const char* path) {
    return manifest.find(std::string("\"") + path + "\"") != std::string::npos;
  };

  for (uint32_t i = 0; i < VrActionCount; i++)
    CHECK(declared(vrOpenVrActionPath(VrAction(i))));

  for (const char* set : VrOpenVrActionSets)
    CHECK(declared(set));

  CHECK(declared(vrOpenVrStickPath(VrInputContext::Game, false)));
  CHECK(declared(vrOpenVrStickPath(VrInputContext::Game, true)));
  CHECK(declared(vrOpenVrStickPath(VrInputContext::Menu, false)));
  CHECK(declared(vrOpenVrHapticPath(VrHand::Left)));
  CHECK(declared(vrOpenVrHapticPath(VrHand::Right)));
}


TEST_CASE(pose_to_matrix_is_the_inverse_of_matrix_to_pose) {
  VrPose pose;
  pose.orientation = vrQuaternionFromAxisAngle({ 0.2f, 1.0f, -0.4f }, 1.1f);
  pose.position    = { 0.5f, 1.6f, -2.0f };

  float m[3][4] = { };
  vrPoseToMatrix34(pose, m);
  checkMatrix(m);

  VrPose back = vrPoseFromMatrix34(m);
  VrVector3 v = { 0.3f, -0.5f, 0.8f };
  VrVector3 expected = vrTransformPoint(pose, v);
  VrVector3 actual   = vrTransformPoint(back, v);

  CHECK_NEAR(actual.x, expected.x, 1e-5);
  CHECK_NEAR(actual.y, expected.y, 1e-5);
  CHECK_NEAR(actual.z, expected.z, 1e-5);
}
