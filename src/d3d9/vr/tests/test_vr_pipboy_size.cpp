#include "test.h"

#include "../vr_pipboy_size.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  float run(VrPipBoyGrowth& growth, bool grown, float scale, float duration) {
    float factor = growth.factor();

    for (float t = 0.0f; t < duration; t += Frame)
      factor = growth.update(grown, scale, Frame);

    return factor;
  }

}


TEST_CASE(closest_point_on_line_projects_onto_the_axis) {
  VrVector3 point = vrClosestPointOnLine({ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 4.0f, 3.0f, -2.0f });
  CHECK_NEAR(point.x, 4.0f, 1e-6);
  CHECK_NEAR(point.y, 0.0f, 1e-6);
  CHECK_NEAR(point.z, 0.0f, 1e-6);

  // Beyond the ends, the line goes on
  point = vrClosestPointOnLine({ 1.0f, 1.0f, 0.0f }, { 1.0f, 2.0f, 0.0f }, { 5.0f, -3.0f, 0.0f });
  CHECK_NEAR(point.x, 1.0f, 1e-6);
  CHECK_NEAR(point.y, -3.0f, 1e-6);

  // Without a line, the first point
  point = vrClosestPointOnLine({ 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, { 5.0f, 5.0f, 5.0f });
  CHECK_NEAR(point.x, 1.0f, 1e-6);
  CHECK_NEAR(point.z, 3.0f, 1e-6);
}


TEST_CASE(scaling_about_a_point_keeps_the_point) {
  VrNodePlacement node;
  node.translate = { 2.0f, -1.0f, 0.5f };
  node.scale     = 1.5f;
  VrVector3 pivot  = { 3.0f, 0.0f, 0.0f };
  VrVector3 axis   = { 1.0f, 0.0f, 0.0f };

  VrNodePlacement scaled = vrPlaceAboutAxis(node, pivot, axis, 0.0f, 2.0f);
  CHECK_NEAR(scaled.scale, 3.0f, 1e-6);

  // A node point x (rotation identity) lands at s * x + t: the node point
  // at the pivot stays there, others move away by the factor
  VrVector3 atPivot = { (pivot.x - node.translate.x) / node.scale,
                        (pivot.y - node.translate.y) / node.scale,
                        (pivot.z - node.translate.z) / node.scale };
  CHECK_NEAR(atPivot.x * scaled.scale + scaled.translate.x, pivot.x, 1e-5);
  CHECK_NEAR(atPivot.y * scaled.scale + scaled.translate.y, pivot.y, 1e-5);
  CHECK_NEAR(atPivot.z * scaled.scale + scaled.translate.z, pivot.z, 1e-5);

  // The node's origin was 1 unit left of the pivot, it goes to 2
  CHECK_NEAR(scaled.translate.x, 1.0f, 1e-6);
  CHECK_NEAR(scaled.translate.y, -2.0f, 1e-6);

  // No turn and the factor 1 leave the node alone
  VrNodePlacement same = vrPlaceAboutAxis(node, pivot, axis, 0.0f, 1.0f);
  CHECK_NEAR(same.translate.x, node.translate.x, 1e-6);
  CHECK_NEAR(same.translate.y, node.translate.y, 1e-6);
  CHECK_NEAR(same.scale, node.scale, 1e-6);
  CHECK_NEAR(same.rotate.m[0][0], 1.0f, 1e-6);
}


TEST_CASE(turning_about_an_axis_keeps_the_distance_to_it) {
  // A node 2 units to the left (-x) of an axis along -z through (0, 1, 0)
  VrNodePlacement node;
  node.translate = { -2.0f, 1.0f, 5.0f };
  VrVector3 pivot = { 0.0f, 1.0f, 0.0f };
  VrVector3 axis  = { 0.0f, 0.0f, -3.0f };

  // A quarter turn about -z by the right-hand rule takes the left side up
  VrNodePlacement turned = vrPlaceAboutAxis(node, pivot, axis, 3.14159265f * 0.5f, 1.0f);
  CHECK_NEAR(turned.translate.x, 0.0f, 1e-5);
  CHECK_NEAR(turned.translate.y, 3.0f, 1e-5);
  CHECK_NEAR(turned.translate.z, 5.0f, 1e-5);
  CHECK_NEAR(turned.scale, 1.0f, 1e-6);

  // The node turns with it: its -x axis now points up
  VrVector3 left = turned.rotate * VrVector3 { -1.0f, 0.0f, 0.0f };
  CHECK_NEAR(left.x, 0.0f, 1e-5);
  CHECK_NEAR(left.y, 1.0f, 1e-5);
  CHECK_NEAR(left.z, 0.0f, 1e-5);

  // Turned and grown: twice as far from the axis
  VrNodePlacement grown = vrPlaceAboutAxis(node, pivot, axis, 3.14159265f * 0.5f, 2.0f);
  CHECK_NEAR(grown.translate.y, 5.0f, 1e-5);
  CHECK_NEAR(grown.translate.z, 10.0f, 1e-5);
  CHECK_NEAR(grown.scale, 2.0f, 1e-6);
}


TEST_CASE(pipboy_grows_while_open_and_shrinks_back) {
  VrPipBoyGrowth growth;
  CHECK_NEAR(growth.factor(), 1.0f, 1e-6);

  // Halfway through the grow time, halfway to the size
  float factor = growth.update(true, 2.0f, VrPipBoyGrowth::GrowTime * 0.5f);
  CHECK_NEAR(factor, 1.5f, 1e-5);

  CHECK_NEAR(run(growth, true, 2.0f, 1.0f), 2.0f, 1e-6);

  // Closed: back to the game's size in the same time
  growth.update(false, 2.0f, VrPipBoyGrowth::GrowTime * 0.5f);
  CHECK_NEAR(growth.factor(), 1.5f, 1e-5);
  CHECK_NEAR(run(growth, false, 2.0f, 1.0f), 1.0f, 1e-6);
}


TEST_CASE(pipboy_keeps_its_size_at_the_normal_option) {
  VrPipBoyGrowth growth;
  CHECK_NEAR(run(growth, true, 1.0f, 1.0f), 1.0f, 1e-6);

  // Sizes below the game's are not used
  CHECK_NEAR(run(growth, true, 0.5f, 1.0f), 1.0f, 1e-6);
}


TEST_CASE(pipboy_follows_a_size_changed_while_open) {
  VrPipBoyGrowth growth;
  run(growth, true, 2.0f, 1.0f);

  // Set back to Normal from the options page while open
  CHECK_NEAR(run(growth, true, 1.0f, 1.0f), 1.0f, 1e-6);

  CHECK_NEAR(run(growth, true, 1.5f, 1.0f), 1.5f, 1e-6);
}
