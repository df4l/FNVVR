#include <cmath>
#include <cstdint>
#include <string>

#include "test.h"

#include "../vr_scope_reticle.h"

using namespace dxvk;

namespace {

  constexpr uint32_t Clear = 0x00FFFFFF;
  constexpr uint32_t Black = 0x00000000;

  // The parts' colours: a white grid, an empty mask, no glow
  VrImage clearSheet() {
    VrImage sheet;
    sheet.width  = 64;
    sheet.height = 32;
    sheet.pixels.assign(size_t(sheet.width) * sheet.height, Clear);
    return sheet;
  }

  uint32_t alphaAt(const VrImage& image, float x, float y) {
    uint32_t column = uint32_t((x * 0.5f + 0.5f) * float(image.width));
    uint32_t row    = uint32_t((0.5f - y * 0.5f) * float(image.height));
    return image.at(column, row) >> 24;
  }

}


TEST_CASE(reticle_sheet_is_told_by_its_name) {
  CHECK(vrIsReticleSheet("textures\\dlcanch\\weapons\\scope\\DLCAnchGaussScope01.dds"));
  CHECK(!vrIsReticleSheet("textures\\weapons\\scope\\MilDot_o.dds"));
  CHECK(!vrIsReticleSheet(""));
}


TEST_CASE(reticle_from_a_sheet_is_a_square_with_its_opening_where_the_others_have_it) {
  VrImage reticle = vrComposeReticleSheet(clearSheet());

  CHECK(reticle.width == reticle.height);
  CHECK(alphaAt(reticle, 0.5f, 0.0f) == 0);
  CHECK(alphaAt(reticle, 0.0f, -0.5f) == 0);
  // Black from the opening's edge, at 0.88, to the image's
  CHECK(alphaAt(reticle, 0.89f, 0.0f) == 255);
  CHECK(alphaAt(reticle, 0.7f, 0.7f) == 255);
}


TEST_CASE(reticle_from_a_sheet_has_its_dot_at_the_centre) {
  VrImage reticle = vrComposeReticleSheet(clearSheet());

  uint32_t centre = reticle.at(reticle.width / 2, reticle.height / 2);
  CHECK(centre >> 24 == 255);
  CHECK((centre >> 16 & 0xFF) == 255);
  CHECK((centre & 0xFFFF) == 0);
}


TEST_CASE(reticle_from_a_sheet_mirrors_the_grids_quarter) {
  VrImage sheet = clearSheet();

  // A black pixel in the grid's quarter, the view's centre being at its corner
  sheet.pixels[size_t(16) * sheet.width + 48] = Black;

  VrImage reticle = vrComposeReticleSheet(sheet);

  uint32_t seen = 0;
  uint32_t mirrored[4] = { };

  for (uint32_t y = 0; y < reticle.height; y++) {
    for (uint32_t x = 0; x < reticle.width; x++) {
      if ((reticle.at(x, y) >> 24) > 128 && std::hypot(float(x) - 511.5f, float(y) - 511.5f) < 400.0f) {
        seen += 1;
        mirrored[(x < reticle.width / 2 ? 0 : 1) + (y < reticle.height / 2 ? 0 : 2)] += 1;
      }
    }
  }

  CHECK(seen > 0);

  for (uint32_t count : mirrored)
    CHECK(count == mirrored[0]);
}
