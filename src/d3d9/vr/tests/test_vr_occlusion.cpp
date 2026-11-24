#include "test.h"

#include "../vr_occlusion_rules.h"

using namespace dxvk;

namespace {

  constexpr float Near = 10.0f;
  constexpr float Far  = 10000.0f;

  // An eye at the origin looking along +z (the D3D convention the game's
  // renderer uses), 90 degrees across, on a 1000x1000 viewport
  VrOcclusionView eyeView(bool reversed) {
    VrOcclusionView view;

    for (uint32_t i = 0; i < 4; i++)
      view.view[i * 5] = 1.0f;

    view.proj[0]  = 1.0f;
    view.proj[5]  = 1.0f;
    view.proj[11] = 1.0f;

    if (reversed) {
      view.proj[10] = -Near / (Far - Near);
      view.proj[14] = Near * Far / (Far - Near);
    } else {
      view.proj[10] = Far / (Far - Near);
      view.proj[14] = -Near * Far / (Far - Near);
    }

    view.width  = 1000.0f;
    view.height = 1000.0f;
    return view;
  }

  VrOcclusionBox box(float x0, float y0, float z0, float x1, float y1, float z1) {
    VrOcclusionBox b;
    b.min[0] = x0; b.min[1] = y0; b.min[2] = z0;
    b.max[0] = x1; b.max[1] = y1; b.max[2] = z1;
    return b;
  }

}


TEST_CASE(occlusion_projects_a_box_in_front_of_the_eye) {
  VrOcclusionView view = eyeView(false);
  VrOcclusionVertex vertices[VrOcclusionBoxVertices];

  CHECK(vrProjectOcclusionBox(box(-10.0f, -10.0f, 100.0f, 10.0f, 10.0f, 120.0f), view, vertices));

  for (const VrOcclusionVertex& v : vertices) {
    CHECK(v.x > 400.0f && v.x < 600.0f);
    CHECK(v.y > 400.0f && v.y < 600.0f);
    CHECK(v.z > 0.0f && v.z < 1.0f);
    CHECK(v.rhw > 0.0f);
    CHECK(v.colour == 0xFFFFFFFFu);
  }

  // The nearest face's corner: x = 10 at z = 100 lands at 500 + 500 * 0.1
  bool found = false;

  for (const VrOcclusionVertex& v : vertices)
    found |= std::fabs(v.x - 550.0f) < 0.01f && std::fabs(v.rhw - 0.01f) < 1e-6f;

  CHECK(found);
}


TEST_CASE(occlusion_does_not_test_boxes_behind_the_eye) {
  VrOcclusionView view = eyeView(false);
  VrOcclusionVertex vertices[VrOcclusionBoxVertices];

  CHECK(!vrProjectOcclusionBox(box(-10.0f, -10.0f, -120.0f, 10.0f, 10.0f, -100.0f), view, vertices));
  CHECK(!vrProjectOcclusionBox(box(-10.0f, -10.0f, -50.0f, 10.0f, 10.0f, 100.0f), view, vertices));
  CHECK(!vrProjectOcclusionBox(box(-10.0f, -10.0f, 0.5f, 10.0f, 10.0f, 100.0f), view, vertices));
}


TEST_CASE(occlusion_clamps_depths_to_the_viewport) {
  VrOcclusionVertex vertices[VrOcclusionBoxVertices];

  // Past the far plane: at the cleared depth, where it passes the test
  VrOcclusionView standard = eyeView(false);
  CHECK(vrProjectOcclusionBox(box(-10.0f, -10.0f, 5000.0f, 10.0f, 10.0f, 20000.0f), standard, vertices));

  for (const VrOcclusionVertex& v : vertices)
    CHECK(v.z >= 0.0f && v.z <= 1.0f);

  // Closer than the near plane: brought to it, the nearest depth
  CHECK(vrProjectOcclusionBox(box(-10.0f, -10.0f, 2.0f, 10.0f, 10.0f, 20.0f), standard, vertices));

  bool nearest = false;

  for (const VrOcclusionVertex& v : vertices)
    nearest |= v.z == 0.0f;

  CHECK(nearest);

  VrOcclusionView reversed = eyeView(true);
  CHECK(vrProjectOcclusionBox(box(-10.0f, -10.0f, 2.0f, 10.0f, 10.0f, 20000.0f), reversed, vertices));

  for (const VrOcclusionVertex& v : vertices)
    CHECK(v.z >= 0.0f && v.z <= 1.0f);
}


TEST_CASE(occlusion_reads_the_depth_direction) {
  CHECK(vrDepthGrowsWithDistance(eyeView(false)));
  CHECK(!vrDepthGrowsWithDistance(eyeView(true)));
  CHECK_NEAR(vrOcclusionFocal(eyeView(false)), 500.0f, 1e-3f);
}


TEST_CASE(occlusion_grows_boxes_by_pixels_at_their_farthest_corner) {
  const float eye[3] = { 0.0f, 0.0f, 0.0f };
  VrOcclusionBox b = box(0.0f, 0.0f, 300.0f, 0.0f, 0.0f, 400.0f);

  // 2 pixels at 400 units with a focal of 500: 1.6 units
  VrOcclusionBox grown = vrGrowOcclusionBox(b, eye, 2.0f, 500.0f);
  CHECK_NEAR(grown.max[2], 401.6f, 1e-3f);
  CHECK_NEAR(grown.min[0], -1.6f, 1e-3f);
  CHECK(grown.contains(b));

  VrOcclusionBox same = vrGrowOcclusionBox(b, eye, 0.0f, 500.0f);
  CHECK(same.contains(b) && b.contains(same));
}


TEST_CASE(occlusion_reveal_grows_with_the_eye_move) {
  CHECK(vrRevealPixels(500.0f, 0.0f, 150.0f, 250.0f) == 0.0f);
  CHECK_NEAR(vrRevealPixels(500.0f, 3.0f, 150.0f, 250.0f), 10.0f, 1e-4f);
  CHECK(vrRevealPixels(500.0f, 300.0f, 150.0f, 250.0f) == 250.0f);
  CHECK(vrRevealPixels(500.0f, 3.0f, 0.0f, 250.0f) == 0.0f);
}


TEST_CASE(occlusion_measures_the_eye_turn) {
  const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  const float quarter[9]  = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };

  CHECK_NEAR(vrRotationDegrees(identity, identity), 0.0f, 0.05f);
  CHECK_NEAR(vrRotationDegrees(identity, quarter), 90.0f, 0.05f);
}


TEST_CASE(occlusion_measures_the_distance_to_a_box) {
  VrOcclusionBox b = box(0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 10.0f);
  const float inside[3]  = { 5.0f, 5.0f, 5.0f };
  const float outside[3] = { 13.0f, 14.0f, 5.0f };

  CHECK(vrDistanceToBox(inside, b) == 0.0f);
  CHECK_NEAR(vrDistanceToBox(outside, b), 5.0f, 1e-4f);
}


TEST_CASE(occlusion_tests_covered_candidates_first) {
  CHECK(vrOcclusionUrgency(false, false, 0, 4) == VrOcclusionUrgency::New);
  CHECK(vrOcclusionUrgency(true, true, 1, 4) == VrOcclusionUrgency::Covered);
  CHECK(vrOcclusionUrgency(true, false, 2, 4) == VrOcclusionUrgency::NotDue);
  CHECK(vrOcclusionUrgency(true, false, 4, 4) == VrOcclusionUrgency::Visible);

  std::vector<VrOcclusionTurn> turns = {
    { VrOcclusionUrgency::Visible, 9, 0 },
    { VrOcclusionUrgency::New,     1, 1 },
    { VrOcclusionUrgency::Covered, 1, 2 },
    { VrOcclusionUrgency::Visible, 4, 3 },
    { VrOcclusionUrgency::Covered, 2, 4 },
  };

  vrSelectOcclusionTurns(turns, 4);
  CHECK(turns.size() == 4);

  bool hasShortWait = false;

  for (const VrOcclusionTurn& turn : turns)
    hasShortWait |= turn.index == 3;

  // The visible one that waited least is left for later
  CHECK(!hasShortWait);

  vrSelectOcclusionTurns(turns, 10);
  CHECK(turns.size() == 4);
}


TEST_CASE(occlusion_box_contains) {
  VrOcclusionBox outer = box(0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 10.0f);

  CHECK(outer.contains(box(1.0f, 1.0f, 1.0f, 9.0f, 9.0f, 9.0f)));
  CHECK(!outer.contains(box(1.0f, 1.0f, 1.0f, 11.0f, 9.0f, 9.0f)));
}
