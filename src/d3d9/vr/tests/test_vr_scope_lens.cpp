#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test.h"

#include "../vr_scope_lens.h"

using namespace dxvk;

namespace {

  // The scope looks north with its eyepiece at the origin, in game units
  VrScopeLens northLens() {
    VrScopeLens lens;
    lens.eyepiece.forward  = { 0.0f, 1.0f, 0.0f };
    lens.eyepiece.up       = { 0.0f, 0.0f, 1.0f };
    lens.eyepiece.right    = { 1.0f, 0.0f, 0.0f };
    lens.eyepiece.position = { 0.0f, 0.0f, 0.0f };
    lens.radius     = 2.0f;
    lens.tubeLength = 20.0f;
    return lens;
  }

  VrGameCameraPose eyeAt(const VrVector3& position) {
    VrGameCameraPose eye = northLens().eyepiece;
    eye.position = position;
    return eye;
  }

  // Where the disc is at the top of its rim, a quarter of the way round
  const VrScopeLensVertex& rimTop(const VrScopeLensDisc& disc) {
    return disc[1 + VrScopeLensSegments / 4];
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
  CHECK_NEAR(vrComputeScopeMagnification(75.0f), 1.0f, 1e-5);
  CHECK_NEAR(vrComputeScopeMagnification(25.0f), 3.459f, 1e-2);
}


TEST_CASE(scope_image_scale_fills_the_image_at_its_apparent_field) {
  float half = std::atan(0.25f);
  VrFov fov = { -half, half, half, -half };

  VrScopeImageScale scale = vrComputeScopeImageScale(fov, 2.0f);

  // A direction at a tangent of 0.5 through the lens is 0.25 in the world
  CHECK_NEAR(scale.x * 0.5f, 1.0f, 1e-5);
  CHECK_NEAR(scale.y * 0.5f, 1.0f, 1e-5);
}


TEST_CASE(scope_camera_sits_at_the_objective_on_the_axis) {
  VrGameCameraPose camera = vrComputeScopeCamera(northLens());

  CHECK_NEAR(camera.position.y, 20.0f, 1e-5);
  CHECK_NEAR(camera.forward.y, 1.0f, 1e-5);
}


TEST_CASE(scope_lens_on_the_axis_is_centred_on_the_image) {
  VrScopeLensDisc disc;
  CHECK(vrComputeScopeLensDisc(northLens(), eyeAt({ 0.0f, -4.0f, 0.0f }), { 1.0f, 1.0f }, disc));

  CHECK_NEAR(disc[0].u, 0.5f, 1e-5);
  CHECK_NEAR(disc[0].v, 0.5f, 1e-5);

  // In the eye's frame the disc is straight ahead
  CHECK_NEAR(disc[0].x, 0.0f, 1e-5);
  CHECK_NEAR(disc[0].z, 4.0f, 1e-5);

  // The top of the rim shows what is higher up: nearer the image's top
  CHECK_NEAR(rimTop(disc).y, 2.0f, 1e-4);
  CHECK_NEAR(rimTop(disc).v, 0.5f - 0.5f * 2.0f / 4.0f, 1e-4);
}


TEST_CASE(scope_lens_keeps_the_world_still_when_the_eye_moves) {
  // The image lies at infinity: the same image point stays in the same
  // direction from the eye, wherever the eye is behind the lens
  VrScopeLens lens = northLens();
  VrScopeImageScale scale = { 1.0f, 1.0f };

  VrScopeLensDisc centred;
  VrScopeLensDisc moved;
  CHECK(vrComputeScopeLensDisc(lens, eyeAt({ 0.0f, -4.0f, 0.0f }), scale, centred));
  CHECK(vrComputeScopeLensDisc(lens, eyeAt({ 1.0f, -4.0f, 0.0f }), scale, moved));

  float tangentOf = (moved[0].u - 0.5f) * 2.0f;

  // The disc's centre is left of the moved eye, and shows what is left of the axis
  CHECK_NEAR(tangentOf, moved[0].x / moved[0].z, 1e-5);
  CHECK(moved[0].u < centred[0].u);
}


TEST_CASE(scope_reticle_covers_the_same_part_of_the_world_at_any_magnification) {
  float half = std::atan(0.1662f);
  VrFov fov = { -half, half, half, -half };

  // The edge of the reticle's texture, as a tangent in the world
  float edge = 1.0f / (vrComputeScopeImageScale(fov, 3.0f).reticle * 3.0f);

  CHECK_NEAR(edge, 0.1662f * 1.023f, 1e-4);
}


TEST_CASE(scope_reticle_stays_on_the_axis_when_the_eye_moves) {
  VrScopeImageScale scale = { 1.0f, 1.0f, 0.5f };

  VrScopeLensDisc disc;
  CHECK(vrComputeScopeLensDisc(northLens(), eyeAt({ 1.0f, -4.0f, 0.0f }), scale, disc));

  // The eye is 1 unit right of the axis at 4 units: the disc's centre is
  // left of the eye and shows the reticle left of its centre
  CHECK_NEAR(disc[0].reticleU, 0.5f + 0.5f * 0.5f * (-1.0f / 4.0f), 1e-5);
  CHECK_NEAR(disc[0].reticleV, 0.5f, 1e-5);
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
  VrScopeLensDisc disc;
  CHECK(!vrComputeScopeLensDisc(northLens(), eyeAt({ 0.0f, 3.0f, 0.0f }), { 1.0f, 1.0f }, disc));
}


TEST_CASE(eye_projection_maps_an_off_centre_view_to_the_image) {
  VrFov fov = { -std::atan(1.3f), std::atan(1.0f), std::atan(1.1f), -std::atan(0.9f) };
  std::array<float, 16> m = vrComputeEyeProjection(fov);

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
