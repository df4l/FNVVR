#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test.h"

#include "../vr_math.h"
#include "../vr_scope_lens.h"

using namespace dxvk;

namespace {

  constexpr float Tolerance = 1e-4f;

  // The scope looks north with its eyepiece at the origin, in game units
  VrScopeLens northLens() {
    VrScopeLens lens;
    lens.eyepiece.forward  = { 0.0f, 1.0f, 0.0f };
    lens.eyepiece.up       = { 0.0f, 0.0f, 1.0f };
    lens.eyepiece.right    = { 1.0f, 0.0f, 0.0f };
    lens.eyepiece.position = { 0.0f, 0.0f, 0.0f };
    lens.halfWidth         = 2.0f;
    lens.halfHeight        = 2.0f;
    lens.apparentTangent   = vrComputeScopeApparentTangent(2.0f, 2.0f);
    lens.objective         = { 0.0f, 40.0f, -5.0f };
    return lens;
  }

  VrGameCameraPose eyeAt(const VrVector3& position) {
    VrGameCameraPose eye = northLens().eyepiece;
    eye.position = position;
    return eye;
  }

  VrFov wideFov() {
    return { -1.0f, 1.0f, 1.0f, -1.0f };
  }

  // The lens's rim at an angle counted in segments from the right
  const VrScopeLensVertex& rim(const VrScopeLensMesh& mesh, uint32_t segment) {
    return mesh[1 + (VrScopeLensRings - 1) * VrScopeLensSegments + segment];
  }

  const VrScopeLensVertex& rimTop(const VrScopeLensMesh& mesh) {
    return rim(mesh, VrScopeLensSegments / 4);
  }

  // A string as the game's model files store it, after some other bytes
  void addString(std::vector<uint8_t>& file, const std::string& text) {
    uint32_t length = uint32_t(text.size());
    file.push_back(0xAB);
    file.insert(file.end(), reinterpret_cast<const uint8_t*>(&length),
      reinterpret_cast<const uint8_t*>(&length) + sizeof(length));
    file.insert(file.end(), text.begin(), text.end());
  }

}


TEST_CASE(scope_magnification_follows_the_weapons_view) {
  CHECK_NEAR(vrComputeScopeMagnification(75.0f, 75.0f), 1.0f, 1e-5);
  CHECK_NEAR(vrComputeScopeMagnification(25.0f, 75.0f), 3.459f, 1e-2);
}


TEST_CASE(scope_ring_lies_at_the_rim_from_the_designed_distance) {
  // 13 cm from the eye, at the reticle's ring radius
  float tangent = vrComputeScopeApparentTangent(1.5f, 1.5f);
  CHECK_NEAR(0.88f * tangent * 9.0f, 1.5f, Tolerance);
}


TEST_CASE(scope_rectangle_is_sized_by_its_long_side) {
  CHECK_NEAR(vrComputeScopeApparentTangent(2.0f, 1.0f), vrComputeScopeApparentTangent(2.0f, 2.0f), Tolerance);
}


TEST_CASE(scope_area_is_a_centred_square) {
  VrScopeArea area = vrComputeScopeArea(3136, 2352, 1024);

  CHECK(area.left == 1056 && area.right == 2080);
  CHECK(area.top == 664 && area.bottom == 1688);
}


TEST_CASE(scope_area_fits_the_image) {
  VrScopeArea area = vrComputeScopeArea(1600, 1200, 4096);

  CHECK(area.left == 200 && area.right == 1400);
  CHECK(area.top == 0 && area.bottom == 1200);
}


TEST_CASE(scope_area_has_a_smallest_side) {
  VrScopeArea area = vrComputeScopeArea(1600, 1200, 0);

  CHECK(area.right - area.left == 64);
}


TEST_CASE(scope_render_view_puts_the_scopes_field_on_the_area) {
  float magnification = vrComputeScopeMagnification(25.0f, 75.0f);
  float tangent = vrComputeScopeApparentTangent(1.5f, 1.5f);
  VrScopeArea area = vrComputeScopeArea(3136, 2352, 1024);
  VrFov fov = vrComputeScopeRenderFov(magnification, tangent, 3136, 2352, area);

  // The area's edge, as a tangent in the world
  float edge = std::tan(fov.angleUp) * 1024.0f / 2352.0f;
  CHECK_NEAR(edge * magnification, tangent, Tolerance);

  // The pixels are square
  CHECK_NEAR(std::tan(fov.angleRight) / std::tan(fov.angleUp), 3136.0f / 2352.0f, Tolerance);
  CHECK_NEAR(fov.angleLeft + fov.angleRight, 0.0f, 1e-5);
  CHECK_NEAR(fov.angleUp + fov.angleDown, 0.0f, 1e-5);
}


TEST_CASE(scope_camera_looks_along_the_axis_from_the_objective) {
  VrGameCameraPose camera = vrComputeScopeCamera(northLens());

  CHECK_NEAR(camera.position.y, 40.0f, 1e-5);
  CHECK_NEAR(camera.position.z, -5.0f, 1e-5);
  CHECK_NEAR(camera.forward.y, 1.0f, 1e-5);
}


TEST_CASE(scope_lens_on_the_axis_is_centred_on_the_image) {
  VrScopeLensMesh mesh;
  CHECK(vrComputeScopeLensMesh(northLens(), eyeAt({ 0.0f, -4.0f, 0.0f }), mesh));

  CHECK_NEAR(mesh[0].u, 0.5f, 1e-5);
  CHECK_NEAR(mesh[0].v, 0.5f, 1e-5);

  // In the eye's frame the lens is straight ahead
  CHECK_NEAR(mesh[0].x, 0.0f, 1e-5);
  CHECK_NEAR(mesh[0].z, 4.0f, 1e-5);

  // The top of the rim shows what is higher up: nearer the image's top
  CHECK_NEAR(rimTop(mesh).y, 2.0f, Tolerance);
  CHECK_NEAR(rimTop(mesh).v, 0.5f - 0.5f * (2.0f / 4.0f) / northLens().apparentTangent, Tolerance);
}


TEST_CASE(scope_lens_keeps_the_world_still_when_the_eye_moves) {
  // The image lies at infinity: the same image point stays in the same
  // direction from the eye, wherever the eye is behind the lens
  VrScopeLens lens = northLens();
  VrScopeLensMesh centred;
  VrScopeLensMesh moved;
  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 0.0f, -4.0f, 0.0f }), centred));
  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 1.0f, -4.0f, 0.0f }), moved));

  float tangentOf = (moved[0].u - 0.5f) * 2.0f * lens.apparentTangent;

  // The lens's centre is left of the moved eye, and shows what is left of the axis
  CHECK_NEAR(tangentOf, moved[0].x / moved[0].z, 1e-5);
  CHECK(moved[0].u < centred[0].u);
}


TEST_CASE(scope_rectangular_lens_has_corners) {
  VrScopeLens lens = northLens();
  lens.rectangular = true;
  lens.halfWidth   = 2.0f;
  lens.halfHeight  = 1.0f;

  VrScopeLensMesh mesh;
  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 0.0f, -4.0f, 0.0f }), mesh));

  const VrScopeLensVertex& corner = rim(mesh, VrScopeLensSegments / 8);
  CHECK_NEAR(corner.x, 2.0f, Tolerance);
  CHECK_NEAR(corner.y, 1.0f, Tolerance);
}


TEST_CASE(scope_lens_triangles_stay_within_the_mesh) {
  const VrScopeLensIndices& indices = vrScopeLensIndices();
  bool valid = true;

  for (uint16_t index : indices)
    valid &= index < VrScopeLensVertexCount;

  CHECK(valid);
  CHECK(indices[0] == 0);
}


TEST_CASE(scope_glass_edge_is_soft_and_dark) {
  VrVector3 straight = { 0.0f, 1.0f, 0.0f };
  VrScopeGlass centre = vrComputeScopeGlass(northLens(), 0.0f, straight);
  VrScopeGlass edge   = vrComputeScopeGlass(northLens(), 1.0f, straight);

  CHECK_NEAR(centre.coverage, 1.0f, 1e-5);
  CHECK_NEAR(edge.coverage, 0.0f, 1e-5);
  CHECK(edge.transmission < centre.transmission * 0.5f);

  // Barely a reflection when looking straight through
  CHECK(centre.reflection[1] < 0.05f);
}


TEST_CASE(scope_glass_mirrors_more_at_a_grazing_angle) {
  VrVector3 straight = { 0.0f, 1.0f, 0.0f };
  VrVector3 grazing  = { 0.98f, 0.2f, 0.0f };

  CHECK(vrComputeScopeGlass(northLens(), 0.5f, grazing).reflection[1]
      > 2.0f * vrComputeScopeGlass(northLens(), 0.5f, straight).reflection[1]);
}


TEST_CASE(scope_shows_its_image_only_near_the_axis) {
  VrScopeLens lens = northLens();

  CHECK(vrScopeLensShowsImage(lens, eyeAt({ 0.0f, -9.0f, 0.0f }), wideFov()));

  CHECK(vrScopeLensShowsImage(lens, eyeAt({ 3.0f, -9.0f, 0.0f }), wideFov()));

  // The other eye, 4.5 units off at the distance the scope is designed for,
  // sees the dark past the image's edge, as with a real scope
  CHECK(!vrScopeLensShowsImage(lens, eyeAt({ 4.5f, -9.0f, 0.0f }), wideFov()));

  // In front of the eyepiece, and too far to matter
  CHECK(!vrScopeLensShowsImage(lens, eyeAt({ 0.0f, 3.0f, 0.0f }), wideFov()));
  CHECK(!vrScopeLensShowsImage(lens, eyeAt({ 0.0f, -80.0f, 0.0f }), wideFov()));
}


TEST_CASE(scope_shows_no_image_outside_the_eyes_view) {
  // Looking away from the lens, however well the eye is placed
  VrGameCameraPose eye = eyeAt({ 0.0f, -30.0f, 0.0f });
  eye.forward = { 1.0f, 0.0f, 0.0f };
  eye.right   = { 0.0f, -1.0f, 0.0f };

  CHECK(!vrScopeLensShowsImage(northLens(), eye, wideFov()));
}


TEST_CASE(scope_lens_bounds_enclose_its_rim) {
  VrScopeLensMesh mesh;
  VrScopeLensBounds bounds;
  CHECK(vrComputeScopeLensMesh(northLens(), eyeAt({ 0.0f, -10.0f, 0.0f }), mesh));

  VrFov fov = { -std::atan(1.0f), std::atan(1.0f), std::atan(1.0f), -std::atan(1.0f) };
  CHECK(vrComputeScopeLensBounds(mesh, fov, bounds));

  // 2 units at 10 units away: a tenth of the image each side of the middle
  CHECK_NEAR(bounds.left, 0.4f, 0.01f);
  CHECK_NEAR(bounds.right, 0.6f, 0.01f);
  CHECK_NEAR(bounds.top, 0.4f, 0.01f);
  CHECK_NEAR(bounds.bottom, 0.6f, 0.01f);
}


TEST_CASE(scope_lens_outside_the_image_has_no_bounds) {
  VrScopeLensMesh mesh;
  VrScopeLensBounds bounds;
  CHECK(vrComputeScopeLensMesh(northLens(), eyeAt({ 50.0f, -10.0f, 0.0f }), mesh));

  VrFov fov = { -std::atan(1.0f), std::atan(1.0f), std::atan(1.0f), -std::atan(1.0f) };
  CHECK(!vrComputeScopeLensBounds(mesh, fov, bounds));
}


TEST_CASE(scope_lens_reaching_behind_the_eye_takes_the_whole_image) {
  VrScopeLensMesh mesh;
  VrScopeLensBounds bounds;

  // Beside the lens and looking along it: part of the rim is behind the eye's plane
  VrGameCameraPose eye = eyeAt({ 0.0f, -1.0f, 0.0f });
  eye.forward = { 1.0f, 0.0f, 0.0f };
  eye.right   = { 0.0f, -1.0f, 0.0f };

  CHECK(vrComputeScopeLensMesh(northLens(), eye, mesh));

  VrFov fov = { -std::atan(1.0f), std::atan(1.0f), std::atan(1.0f), -std::atan(1.0f) };
  CHECK(vrComputeScopeLensBounds(mesh, fov, bounds));
  CHECK_NEAR(bounds.left, 0.0f, 1e-5);
  CHECK_NEAR(bounds.right, 1.0f, 1e-5);
}


TEST_CASE(scope_reticle_texture_is_found_past_the_surround) {
  std::vector<uint8_t> file;
  addString(file, "keepScope01:0");
  addString(file, "textures\\weapons\\scope\\scope01.dds");
  addString(file, "textures\\weapons\\scope\\MilDot_o.dds");
  addString(file, "textures\\weapons\\scope\\scope03_n.dds");

  CHECK(vrFindScopeReticleTexture(file.data(), file.size())
     == "textures\\weapons\\scope\\MilDot_o.dds");
}


TEST_CASE(scope_model_without_a_reticle_names_none) {
  std::vector<uint8_t> file;
  addString(file, "textures\\weapons\\scope\\Scope01.dds");

  CHECK(vrFindScopeReticleTexture(file.data(), file.size()).empty());
}


TEST_CASE(scope_lens_is_not_drawn_for_an_eye_in_front_of_it) {
  VrScopeLensMesh mesh;
  CHECK(!vrComputeScopeLensMesh(northLens(), eyeAt({ 0.0f, 3.0f, 0.0f }), mesh));
}


TEST_CASE(eye_projection_maps_an_off_centre_view_to_the_image) {
  VrFov fov = { -std::atan(1.3f), std::atan(1.0f), std::atan(1.1f), -std::atan(0.9f) };
  std::array<float, 16> m = vrComputeEyeProjection(fov, 5.0f, 350000.0f);

  auto ndc = [&m] (float x, float y, float z, float& outX, float& outY) {
    float w = x * m[3] + y * m[7] + z * m[11] + m[15];
    outX = (x * m[0] + y * m[4] + z * m[8]  + m[12]) / w;
    outY = (x * m[1] + y * m[5] + z * m[9]  + m[13]) / w;
  };

  float x, y;

  ndc(1.0f, 1.1f, 1.0f, x, y);
  CHECK_NEAR(x, 1.0f, 1e-5);
  CHECK_NEAR(y, 1.0f, 1e-5);

  ndc(-1.3f, -0.9f, 1.0f, x, y);
  CHECK_NEAR(x, -1.0f, 1e-5);
  CHECK_NEAR(y, -1.0f, 1e-5);
}


TEST_CASE(eye_projection_gives_the_games_depth) {
  // Gamebryo's depth: 0 at the near distance, close to 1 far away
  VrFov fov = { -std::atan(1.0f), std::atan(1.0f), std::atan(1.0f), -std::atan(1.0f) };
  std::array<float, 16> m = vrComputeEyeProjection(fov, 5.0f, 1000.0f);

  auto depth = [&m] (float z) { return (z * m[10] + m[14]) / z; };

  CHECK_NEAR(depth(5.0f), 0.0f, 1e-5);
  CHECK_NEAR(depth(1000.0f), 1.0f, 1e-5);
  CHECK(depth(7.0f) < depth(9.0f));
}

namespace {

  // Binoculars looking north, their right eyepiece on the east side
  VrScopeLens northBinoculars() {
    VrScopeLens lens = northLens();
    lens.kind                = VrOpticKind::Binoculars;
    lens.eyepiece.position   = { 3.0f, 0.0f, 0.0f };
    lens.otherEyepiece       = { -3.0f, 0.0f, 0.0f };
    lens.objective           = { 0.0f, 17.0f, 0.0f };
    lens.halfWidth           = 1.0f;
    lens.halfHeight          = 1.0f;
    return lens;
  }

  VrScopeLens northScreen() {
    VrScopeLens lens = northLens();
    lens.kind        = VrOpticKind::Screen;
    lens.halfWidth   = 4.0f;
    lens.halfHeight  = 3.0f;
    lens.rectangular = true;
    return lens;
  }

}


TEST_CASE(binoculars_held_away_stay_lowered) {
  VrScopeLens lens = northBinoculars();
  vrRaiseBinoculars(lens, eyeAt({ 0.0f, -40.0f, 0.0f }));
  CHECK_NEAR(lens.raised, 0.0f, Tolerance);
  CHECK(!vrScopeLensShowsImage(lens, eyeAt({ 0.0f, -40.0f, 0.0f }), wideFov()));
}


TEST_CASE(binoculars_at_the_eyes_follow_the_head) {
  VrScopeLens lens = northBinoculars();

  // The head a little behind them and turned 15 degrees to the east
  VrGameCameraPose head = eyeAt({ 0.0f, -5.0f, 0.0f });
  float angle = 15.0f * 3.14159265f / 180.0f;
  head.forward = { std::sin(angle), std::cos(angle), 0.0f };
  head.right   = { std::cos(angle), -std::sin(angle), 0.0f };

  vrRaiseBinoculars(lens, head);

  CHECK(lens.raised > 0.9f);
  CHECK(vrScopeLensShowsImage(lens, head, wideFov()));

  // The view is almost the head's, not the hands'
  CHECK(vrDot(lens.eyepiece.forward, head.forward) > 0.999f);
  CHECK(vrLength(lens.objective - head.position) < 3.0f);

  // Still a rotation, with the right hand's order of forward, up and right
  CHECK_NEAR(vrLength(lens.eyepiece.right), 1.0f, Tolerance);
  CHECK_NEAR(vrDot(lens.eyepiece.right, lens.eyepiece.up), 0.0f, Tolerance);
  CHECK(vrDot(lens.eyepiece.right, head.right) > 0.99f);
}


TEST_CASE(binoculars_views_of_both_eyes_merge) {
  VrScopeLens lens = northBinoculars();
  lens.raised = 1.0f;
  lens.apparentTangent = 0.55f;

  VrGameCameraPose left  = eyeAt({ -2.2f, -5.0f, 0.5f });
  VrGameCameraPose right = eyeAt({ 2.2f, -5.0f, 0.5f });

  VrScopeLensMesh leftMesh, rightMesh;
  CHECK(vrComputeScopeLensMesh(vrComputeBinocularsEyeLens(lens, left, true, 2.0f), left, leftMesh));
  CHECK(vrComputeScopeLensMesh(vrComputeBinocularsEyeLens(lens, right, true, 2.0f), right, rightMesh));

  // The same place of the image straight ahead of each eye: one round view
  CHECK_NEAR(leftMesh[0].u, 0.5f, Tolerance);
  CHECK_NEAR(rightMesh[0].u, 0.5f, Tolerance);
  CHECK_NEAR(leftMesh[0].v, 0.5f, Tolerance);
  CHECK_NEAR(leftMesh[0].x, 0.0f, Tolerance);
  CHECK_NEAR(rightMesh[0].x, 0.0f, Tolerance);
}


TEST_CASE(binoculars_lowered_keep_each_eyepiece_on_its_side) {
  VrScopeLens lens = northBinoculars();

  VrGameCameraPose left  = eyeAt({ -2.2f, -30.0f, 0.0f });
  VrGameCameraPose right = eyeAt({ 2.2f, -30.0f, 0.0f });

  VrScopeLens leftOwn   = vrComputeBinocularsEyeLens(lens, left, true, 2.0f);
  VrScopeLens rightOwn  = vrComputeBinocularsEyeLens(lens, right, true, 2.0f);
  VrScopeLens leftOther = vrComputeBinocularsEyeLens(lens, left, false, 2.0f);

  CHECK_NEAR(leftOwn.eyepiece.position.x, -3.0f, Tolerance);
  CHECK_NEAR(rightOwn.eyepiece.position.x, 3.0f, Tolerance);
  CHECK_NEAR(leftOther.eyepiece.position.x, 3.0f, Tolerance);
  CHECK_NEAR(leftOwn.halfWidth, 1.0f, Tolerance);
}


TEST_CASE(screen_shows_its_picture_across_its_face_from_any_side) {
  VrScopeLens lens = northScreen();
  VrScopeLensMesh straight, aside;

  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 0.0f, -30.0f, 0.0f }), straight));
  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 15.0f, -20.0f, 10.0f }), aside));

  for (size_t i = 0; i < straight.size(); i++) {
    CHECK_NEAR(straight[i].u, aside[i].u, Tolerance);
    CHECK_NEAR(straight[i].v, aside[i].v, Tolerance);
  }

  CHECK_NEAR(straight[0].u, 0.5f, Tolerance);
  CHECK(vrScopeLensShowsImage(lens, eyeAt({ 15.0f, -20.0f, 10.0f }), { -1.5f, 1.5f, 1.5f, -1.5f }));
  CHECK(!vrScopeLensShowsImage(lens, eyeAt({ 0.0f, 10.0f, 0.0f }), wideFov()));
}


TEST_CASE(screen_housing_goes_past_the_picture) {
  VrScopeLens lens = northScreen();
  VrScopeLensMesh mesh;

  CHECK(vrComputeScopeLensMesh(lens, eyeAt({ 0.0f, -30.0f, 0.0f }), mesh));

  // Segment 0 points to the right. The housing is opaque black in the image.
  const VrScopeLensVertex& housing = mesh[1 + 6 * VrScopeLensSegments];
  CHECK(housing.u > 1.0f);
  CHECK_NEAR(float(housing.transmitted >> 24), 255.0f, 1.0f);
  CHECK_NEAR(float(housing.transmitted & 0xFFFFFF), 0.0f, 0.5f);

  // Soft outer edge
  CHECK_NEAR(float(mesh[mesh.size() - 1].transmitted >> 24), 0.0f, 1.0f);
}
