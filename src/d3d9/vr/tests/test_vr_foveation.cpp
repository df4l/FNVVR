#include "test.h"

#include "../vr_foveation_map.h"

using namespace dxvk;

namespace {

  // 90 degrees each way: tangents from -1 to 1 on both axes
  constexpr float QuarterTurn = 0.785398163f;

  VrFov squareFov() {
    VrFov fov;
    fov.angleLeft  = -QuarterTurn;
    fov.angleRight =  QuarterTurn;
    fov.angleUp    =  QuarterTurn;
    fov.angleDown  = -QuarterTurn;
    return fov;
  }

  constexpr VrExtent Target   = { 1600u, 1600u };
  constexpr uint32_t TexelSize = 16u;

}


TEST_CASE(foveation_gaze_straight_ahead_is_the_image_centre) {
  VrGazeTexel texel;
  CHECK(vrLocateGazeTexel(squareFov(), VrGaze { 0.0f, 0.0f }, Target, TexelSize, texel));
  CHECK_NEAR(texel.x, 50.0f, 1e-4);
  CHECK_NEAR(texel.y, 50.0f, 1e-4);
}


TEST_CASE(foveation_gaze_up_is_towards_the_top_row) {
  VrGazeTexel texel;
  CHECK(vrLocateGazeTexel(squareFov(), VrGaze { 1.0f, 0.5f }, Target, TexelSize, texel));
  CHECK_NEAR(texel.x, 100.0f, 1e-4);
  CHECK_NEAR(texel.y, 25.0f, 1e-4);
}


TEST_CASE(foveation_gaze_outside_the_view_is_refused) {
  VrGazeTexel texel;
  CHECK(!vrLocateGazeTexel(squareFov(), VrGaze { 1.2f, 0.0f }, Target, TexelSize, texel));
  CHECK(!vrLocateGazeTexel(squareFov(), VrGaze { 0.0f, -1.5f }, Target, TexelSize, texel));
}


TEST_CASE(foveation_map_is_twice_the_eye_image) {
  VrFoveationMap map(squareFov(), Target, TexelSize, VrFoveationZones());
  CHECK(map.imageSize().width == 100u);
  CHECK(map.imageSize().height == 100u);
  CHECK(map.rowLength() == 200u);
  CHECK(map.texels().size() == 200u * 200u);

  // A target that is not a multiple of the texel size rounds up
  VrFoveationMap odd(squareFov(), VrExtent { 1610u, 1599u }, TexelSize, VrFoveationZones());
  CHECK(odd.imageSize().width == 101u);
  CHECK(odd.imageSize().height == 100u);
}


TEST_CASE(foveation_map_rates_grow_away_from_the_centre) {
  // A texel spans 0.02 in tangent: full rate up to tan 20 degrees (0.364,
  // 18 texels), half up to tan 35 degrees (0.700, 35 texels)
  VrFoveationMap map(squareFov(), Target, TexelSize, VrFoveationZones());

  CHECK(map.rate(100u, 100u) == VrShadingRate::Full);
  CHECK(map.rate(99u, 99u) == VrShadingRate::Full);
  CHECK(map.rate(118u, 100u) == VrShadingRate::Full);
  CHECK(map.rate(81u, 100u) == VrShadingRate::Full);
  CHECK(map.rate(119u, 100u) == VrShadingRate::Half);
  CHECK(map.rate(100u, 80u) == VrShadingRate::Half);
  CHECK(map.rate(134u, 100u) == VrShadingRate::Half);
  CHECK(map.rate(136u, 100u) == VrShadingRate::Quarter);
  CHECK(map.rate(0u, 0u) == VrShadingRate::Quarter);

  // The zones are round: a diagonal reaches the edge of a zone sooner
  CHECK(map.rate(113u, 113u) == VrShadingRate::Half);
}


TEST_CASE(foveation_map_follows_the_view_aspect) {
  // Half as tall in pixels for the same angles: a texel spans twice the
  // tangent vertically, so the zones are half as many texels tall
  VrFoveationMap map(squareFov(), VrExtent { 1600u, 800u }, TexelSize, VrFoveationZones());

  CHECK(map.rate(118u, 50u) == VrShadingRate::Full);
  CHECK(map.rate(100u, 59u) == VrShadingRate::Full);
  CHECK(map.rate(100u, 60u) == VrShadingRate::Half);
}


TEST_CASE(foveation_window_puts_the_gaze_on_the_map_centre) {
  VrFoveationMap map(squareFov(), Target, TexelSize, VrFoveationZones());

  CHECK(map.windowOffset(VrGazeTexel { 50.0f, 50.0f }) == 50u * 200u + 50u);
  CHECK(map.windowOffset(VrGazeTexel { 0.0f, 0.0f }) == 100u * 200u + 100u);
  CHECK(map.windowOffset(VrGazeTexel { 100.0f, 100.0f }) == 0u);
  CHECK(map.windowOffset(VrGazeTexel { 20.4f, 70.6f }) == 29u * 200u + 80u);

  // The window stays inside the map
  CHECK(map.windowOffset(VrGazeTexel { -5.0f, 120.0f }) == 0u * 200u + 100u);
}


TEST_CASE(foveation_zones_can_leave_out_the_quarter_rate) {
  VrFoveationZones zones;
  zones.fullDegrees = 10.0f;
  zones.halfDegrees = 89.0f;

  VrFoveationMap map(squareFov(), Target, TexelSize, zones);
  CHECK(map.rate(0u, 0u) == VrShadingRate::Half);
  CHECK(map.rate(100u, 100u) == VrShadingRate::Full);
}


TEST_CASE(foveation_gaze_is_held_through_a_blink) {
  VrGazeHold hold;
  std::array<VrGaze, VrEyeCount> gaze = { VrGaze { 0.1f, 0.2f }, VrGaze { -0.1f, 0.2f } };

  CHECK(hold.update(nullptr, 0.01f) == nullptr);

  const auto* used = hold.update(&gaze, 0.01f);
  CHECK(used != nullptr);
  CHECK(!hold.holding());

  used = hold.update(nullptr, 0.2f);
  CHECK(used != nullptr);
  CHECK(hold.holding());
  CHECK_NEAR((*used)[0].tanX, 0.1f, 1e-6);
  CHECK_NEAR((*used)[1].tanX, -0.1f, 1e-6);

  CHECK(hold.update(nullptr, 0.2f) != nullptr);
  CHECK(hold.update(nullptr, 0.2f) == nullptr);
  CHECK(!hold.holding());
  CHECK(hold.update(nullptr, 0.01f) == nullptr);

  CHECK(hold.update(&gaze, 0.01f) != nullptr);
  hold.reset();
  CHECK(hold.update(nullptr, 0.01f) == nullptr);
}
