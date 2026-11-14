#include <cmath>

#include "test.h"

#include "../vr_scope_finder.h"

using namespace dxvk;

namespace {

  constexpr float Tolerance = 1e-3f;
  constexpr float Pi        = 3.14159265358979f;

  // A rifle in the game's frame: the barrel along +x, up +y, right +z,
  // the muzzle at x 50
  VrScopeBarrel rifleBarrel() {
    VrScopeBarrel barrel;
    barrel.muzzle  = { 50.0f, 0.0f, 0.0f };
    barrel.forward = { 1.0f, 0.0f, 0.0f };
    barrel.up      = { 0.0f, 1.0f, 0.0f };
    barrel.right   = { 0.0f, 0.0f, 1.0f };
    return barrel;
  }

  // A circle of vertices square to the barrel; radii may alternate to make
  // a polygon that no circle fits
  void addRing(VrScopeShape& shape, float x, float y, float r, uint32_t count = 16, float wobble = 0.0f) {
    for (uint32_t i = 0; i < count; i++) {
      float angle = 2.0f * Pi * float(i) / float(count);
      float radius = r + ((i & 1) ? wobble : -wobble);
      shape.points.push_back({ x, y + radius * std::cos(angle), radius * std::sin(angle) });
    }
  }

  VrScopeShape barrelTube() {
    VrScopeShape shape;

    for (float x = 10.0f; x <= 50.0f; x += 5.0f)
      addRing(shape, x, 0.0f, 0.4f);

    return shape;
  }

  VrScopeShape receiver() {
    VrScopeShape shape;

    for (float x = -15.0f; x <= 10.0f; x += 2.5f) {
      for (float y : { -3.0f, 1.0f }) {
        for (float z : { -1.0f, 1.0f })
          shape.points.push_back({ x, y, z });
      }
    }

    return shape;
  }

  // A scope tube above the barrel, its rim at x -3 and its eyepiece's
  // opening of 1.3 just inside
  VrScopeShape scopeTube() {
    VrScopeShape shape;
    shape.namedScope = true;
    addRing(shape, -3.0f, 5.0f, 1.5f);
    addRing(shape, -2.5f, 5.0f, 1.3f);
    addRing(shape, -1.0f, 5.0f, 1.3f);
    addRing(shape,  3.0f, 5.0f, 1.0f);
    addRing(shape,  8.0f, 5.0f, 1.6f);
    return shape;
  }

  // A dome of glass bulging towards the eye, its apex at x
  VrScopeShape glassDome(float x) {
    VrScopeShape shape;
    shape.points.push_back({ x, 5.0f, 0.0f });
    addRing(shape, x + 0.1f, 5.0f, 0.4f);
    addRing(shape, x + 0.2f, 5.0f, 0.8f);
    return shape;
  }

  std::vector<VrScopeShape> scopedRifle(float glassAt) {
    return { barrelTube(), receiver(), scopeTube(), glassDome(glassAt) };
  }

}


TEST_CASE(scope_finder_puts_the_lens_inside_the_rim_of_a_deep_eyepiece) {
  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(vrFindScope(scopedRifle(-0.5f), rifleBarrel(), placement, finding));
  // Glass 2.5 behind the rim: the lens stays near the rim, as wide as the
  // narrowest ring between the eye and it
  CHECK_NEAR(placement.eyepiece.x, -2.7f, Tolerance);
  CHECK_NEAR(placement.eyepiece.y, 5.0f, Tolerance);
  CHECK_NEAR(placement.eyepiece.z, 0.0f, Tolerance);
  CHECK_NEAR(placement.halfWidth, 1.3f, Tolerance);
  CHECK_NEAR(placement.halfHeight, placement.halfWidth, Tolerance);
  CHECK(!placement.rectangular);
  CHECK(placement.kind == VrOpticKind::Scope);
  CHECK(finding.hasGlass);
  CHECK_NEAR(finding.above, 5.0f, Tolerance);
}


TEST_CASE(scope_finder_puts_the_lens_just_in_front_of_near_glass) {
  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(vrFindScope(scopedRifle(-2.6f), rifleBarrel(), placement, finding));
  CHECK_NEAR(placement.eyepiece.x, -2.635f, Tolerance);
  CHECK_NEAR(placement.halfWidth, 1.3f, Tolerance);
}


TEST_CASE(scope_finder_sees_flat_glass_without_a_centre_vertex) {
  // A disc made of its rim only, as some mods model the glass
  VrScopeShape cap;
  addRing(cap, -2.2f, 5.0f, 1.2f);

  std::vector<VrScopeShape> shapes = { barrelTube(), receiver(), scopeTube(), cap };
  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(vrFindScope(shapes, rifleBarrel(), placement, finding));
  CHECK(finding.hasGlass);
  CHECK_NEAR(placement.eyepiece.x, -2.235f, Tolerance);
}


TEST_CASE(scope_finder_finds_a_rim_that_is_no_circle) {
  // A polygon behind the first ring, too uneven for a circle: the Gauss
  // rifle's rim
  VrScopeShape rim;
  addRing(rim, -5.0f, 5.0f, 1.62f, 8, 0.12f);

  std::vector<VrScopeShape> shapes = scopedRifle(-0.5f);
  shapes.push_back(rim);

  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(vrFindScope(shapes, rifleBarrel(), placement, finding));
  CHECK_NEAR(placement.eyepiece.x, -4.7f, Tolerance);
}


TEST_CASE(scope_finder_finds_nothing_on_a_rifle_without_a_scope) {
  std::vector<VrScopeShape> shapes = { barrelTube(), receiver() };
  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(!vrFindScope(shapes, rifleBarrel(), placement, finding));
}


TEST_CASE(scope_finder_rejects_a_hollow_tube) {
  VrScopeShape tube;

  for (float x : { -3.0f, -2.5f, -1.0f, 3.0f, 8.0f })
    addRing(tube, x, 5.0f, 1.5f);

  std::vector<VrScopeShape> shapes = { barrelTube(), receiver(), tube };
  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(!vrFindScope(shapes, rifleBarrel(), placement, finding));
}


TEST_CASE(scope_finder_rejects_a_tube_with_the_view_blocked_behind_it) {
  std::vector<VrScopeShape> shapes = scopedRifle(-0.5f);
  VrScopeShape block;

  for (float y : { 4.8f, 5.0f, 5.2f })
    block.points.push_back({ -9.0f, y, 0.0f });

  shapes.push_back(block);

  VrScopePlacement placement;
  VrScopeFinding finding;

  CHECK(!vrFindScope(shapes, rifleBarrel(), placement, finding));
}


TEST_CASE(scope_finder_knows_scope_names) {
  CHECK(vrIsScopeName("WMKScopeBody:0"));
  CHECK(vrIsScopeName("##SCOPELens"));
  CHECK(!vrIsScopeName("Barrel:0"));
  CHECK(!vrIsScopeName("Scop"));
  CHECK(!vrIsScopeName(nullptr));
}
