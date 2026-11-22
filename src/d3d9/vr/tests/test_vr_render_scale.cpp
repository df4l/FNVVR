#include "test.h"

#include "../vr_performance_levers.h"
#include "../vr_render_scale_size.h"

using namespace dxvk;

namespace {

  // The Steam Frame's eye target at the tester's resolution
  constexpr VrExtent Screen = { 1728, 1728 };

}


TEST_CASE(render_scale_keeps_the_screen_at_full_scale) {
  VrExtent scaled = vrScaledWorldExtent(Screen, 1.0f);
  CHECK(scaled.width == Screen.width);
  CHECK(scaled.height == Screen.height);

  uint32_t width = Screen.width, height = Screen.height;
  CHECK(!vrScaleScreenTexture(Screen, scaled, width, height));
  CHECK(width == Screen.width);
}


TEST_CASE(render_scale_rounds_to_eight_pixels) {
  VrExtent scaled = vrScaledWorldExtent(VrExtent { 2244, 2352 }, 0.7f);
  CHECK(scaled.width % 8 == 0);
  CHECK(scaled.height % 8 == 0);
  CHECK(scaled.width == 1568);
  CHECK(scaled.height == 1648);
}


TEST_CASE(render_scale_clamps_its_scale) {
  VrExtent low = vrScaledWorldExtent(Screen, 0.1f);
  CHECK(low.width == 864);

  VrExtent high = vrScaledWorldExtent(Screen, 2.0f);
  CHECK(high.width == Screen.width);
}


TEST_CASE(render_scale_scales_the_screen_and_its_halves_and_quarters) {
  VrExtent scaled = vrScaledWorldExtent(Screen, 0.8f);
  CHECK(scaled.width == 1384);

  uint32_t width = Screen.width, height = Screen.height;
  CHECK(vrScaleScreenTexture(Screen, scaled, width, height));
  CHECK(width == 1384);
  CHECK(height == 1384);

  width = Screen.width / 2; height = Screen.height / 2;
  CHECK(vrScaleScreenTexture(Screen, scaled, width, height));
  CHECK(width == 692);

  width = Screen.width / 4; height = Screen.height / 4;
  CHECK(vrScaleScreenTexture(Screen, scaled, width, height));
  CHECK(width == 346);
}


TEST_CASE(render_scale_leaves_other_sizes) {
  VrExtent scaled = vrScaledWorldExtent(Screen, 0.8f);

  // Water reflection, shadow map, the HDR's fixed size
  for (uint32_t side : { 1024u, 512u, 256u }) {
    uint32_t width = side, height = side;
    CHECK(!vrScaleScreenTexture(Screen, scaled, width, height));
    CHECK(width == side);
  }

  // The screen's width with another height (interface textures)
  uint32_t width = Screen.width, height = 960;
  CHECK(!vrScaleScreenTexture(Screen, scaled, width, height));
  CHECK(height == 960);
}


TEST_CASE(performance_levers_lower_resolution_only_after_shadows) {
  CHECK(VrPerformanceLevers::renderScale(0) == 1.0f);
  CHECK(VrPerformanceLevers::renderScale(1) == 1.0f);
  CHECK(VrPerformanceLevers::gpuScale(1) < 1.0f);

  for (uint32_t level = 1; level <= VrPerformanceLevers::GpuSteps; level++) {
    CHECK(VrPerformanceLevers::renderScale(level) <= VrPerformanceLevers::renderScale(level - 1));
    CHECK(VrPerformanceLevers::renderScale(level) >= 0.7f);
  }

  CHECK(!VrPerformanceLevers::waterReflectionsOff(1));
  CHECK(VrPerformanceLevers::waterReflectionsOff(2));
}


TEST_CASE(render_scale_sharpens_more_at_lower_scales) {
  CHECK(vrSharpenWeight(1.0f) == 0.0f);
  CHECK(vrSharpenWeight(0.95f) < 0.0f);
  CHECK(vrSharpenWeight(0.8f) < vrSharpenWeight(0.95f));
  CHECK_NEAR(vrSharpenWeight(0.7f), -1.0f / 5.5f, 1e-5f);
  CHECK(vrSharpenWeight(0.5f) == vrSharpenWeight(0.7f));
  CHECK(vrSharpenWeight(0.95f) > -1.0f / 9.0f);
}


TEST_CASE(performance_levers_lower_resolution_by_five_percent) {
  for (uint32_t level = 1; level <= VrPerformanceLevers::GpuSteps; level++) {
    float step = VrPerformanceLevers::renderScale(level - 1) - VrPerformanceLevers::renderScale(level);
    CHECK(step >= 0.0f && step <= 0.05f + 1e-5f);
  }

  CHECK(VrPerformanceLevers::renderScale(VrPerformanceLevers::GpuSteps) == 0.7f);
}
