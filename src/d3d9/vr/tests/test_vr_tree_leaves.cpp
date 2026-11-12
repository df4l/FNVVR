#include "test.h"

#include "../vr_math.h"
#include "../vr_tree_leaves_rules.h"

using namespace dxvk;

namespace {

  constexpr float Eps = 1.0e-4f;

  const VrVector3 EastRight = { 1.0f, 0.0f, 0.0f };

  void checkVector(const VrVector3& v, float x, float y, float z) {
    CHECK_NEAR(v.x, x, Eps);
    CHECK_NEAR(v.y, y, Eps);
    CHECK_NEAR(v.z, z, Eps);
  }

}


TEST_CASE(leaf_cards_face_a_level_eye_upright) {
  // Leaves to the north of the eye: right is east, up is world up, as for a
  // camera looking north without roll
  VrLeafCardAxes axes;
  CHECK(vrLeafCardAxesFacing({ 0.0f, 0.0f, 100.0f }, { 0.0f, 500.0f, 100.0f }, EastRight, axes));
  checkVector(axes.right, 1.0f, 0.0f, 0.0f);
  checkVector(axes.up, 0.0f, 0.0f, 1.0f);
}


TEST_CASE(leaf_cards_ignore_the_camera_axes) {
  // A rolled camera gives the same axes: only the positions count
  VrLeafCardAxes level, rolled;
  CHECK(vrLeafCardAxesFacing({ 0.0f, 0.0f, 100.0f }, { 300.0f, 400.0f, 50.0f }, EastRight, level));
  CHECK(vrLeafCardAxesFacing({ 0.0f, 0.0f, 100.0f }, { 300.0f, 400.0f, 50.0f }, { 0.0f, 0.7071f, 0.7071f }, rolled));
  checkVector(rolled.right, level.right.x, level.right.y, level.right.z);
  checkVector(rolled.up, level.up.x, level.up.y, level.up.z);
}


TEST_CASE(leaf_cards_tilt_to_an_eye_above_them) {
  // Looking down at a bush to the north at 45 degrees: the cards lean back
  // so that they face the eye, and stay level from left to right
  VrLeafCardAxes axes;
  CHECK(vrLeafCardAxesFacing({ 0.0f, 0.0f, 100.0f }, { 0.0f, 100.0f, 0.0f }, EastRight, axes));
  checkVector(axes.right, 1.0f, 0.0f, 0.0f);
  checkVector(axes.up, 0.0f, 0.7071068f, 0.7071068f);
  CHECK_NEAR(vrDot(axes.up, VrVector3 { 0.0f, 0.7071068f, -0.7071068f }), 0.0f, Eps);
}


TEST_CASE(leaf_cards_straight_below_keep_the_camera_heading) {
  VrLeafCardAxes axes;
  CHECK(vrLeafCardAxesFacing({ 0.0f, 0.0f, 100.0f }, { 0.0f, 0.0f, 0.0f }, EastRight, axes));
  checkVector(axes.right, 1.0f, 0.0f, 0.0f);
  CHECK_NEAR(vrDot(axes.up, VrVector3 { 0.0f, 0.0f, -1.0f }), 0.0f, Eps);
  CHECK_NEAR(vrLength(axes.up), 1.0f, Eps);
}


TEST_CASE(leaf_cards_at_the_eye_keep_the_game_axes) {
  VrLeafCardAxes axes;
  CHECK(!vrLeafCardAxesFacing({ 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, EastRight, axes));
}
