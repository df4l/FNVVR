#include "test.h"

#include "../vr_pipboy_touch.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  /**
   * \brief A screen 8 units wide and 6 high in the XY plane, facing +Z,
   *    u growing along +X and v growing along -Y (texture top at +Y)
   */
  VrTouchScreen flatScreen() {
    const VrVector3 positions[] = {
      { -4.0f,  3.0f, 0.0f }, {  4.0f,  3.0f, 0.0f },
      { -4.0f, -3.0f, 0.0f }, {  4.0f, -3.0f, 0.0f } };
    const VrVector2 uvs[] = {
      { 0.0f, 0.0f }, { 1.0f, 0.0f },
      { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    // Counter-clockwise seen from +Z
    const uint16_t triangles[] = { 0, 2, 1,  1, 2, 3 };
    return vrFitTouchScreen(positions, uvs, 4, triangles, 2);
  }

  VrPipBoyTouchPoint onScreen(float distance, float u, float v) {
    VrPipBoyTouchPoint point;
    point.distance = distance;
    point.uv       = { u, v };
    point.inside   = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
    point.plane    = { (u - 0.5f) * 0.08f, (v - 0.5f) * 0.06f };
    point.surface  = { u, v, 0.0f };
    return point;
  }

  VrPipBoyTouchState step(VrPipBoyTouch& touch, const VrPipBoyTouchPoint& point, bool padUsed = false) {
    VrPipBoyTouchInput input;
    input.point   = &point;
    input.padUsed = padUsed;
    input.dt      = Frame;
    return touch.update(input);
  }

}


TEST_CASE(touch_screen_fits_plane_and_texture) {
  VrTouchScreen screen = flatScreen();
  CHECK(screen.valid);
  CHECK_NEAR(screen.normal.z, 1.0f, 1e-5);
  CHECK_NEAR(screen.uv0.x, 0.5f, 1e-5);
  CHECK_NEAR(screen.uv0.y, 0.5f, 1e-5);
  CHECK_NEAR(screen.uAxis.x, 1.0f, 1e-5);
  CHECK_NEAR(screen.vAxis.y, -1.0f, 1e-5);

  VrScreenPoint point = vrPointOnScreen(screen, { 2.0f, 1.5f, 0.5f });
  CHECK_NEAR(point.distance, 0.5f, 1e-5);
  CHECK_NEAR(point.uv.x, 0.75f, 1e-5);
  CHECK_NEAR(point.uv.y, 0.25f, 1e-5);
  CHECK_NEAR(point.plane.x, 2.0f, 1e-5);
  CHECK_NEAR(point.plane.y, -1.5f, 1e-5);
  CHECK_NEAR(point.surface.z, 0.0f, 1e-5);
  CHECK(point.inside);

  // Behind the screen and off its side
  point = vrPointOnScreen(screen, { 5.0f, 0.0f, -1.0f });
  CHECK_NEAR(point.distance, -1.0f, 1e-5);
  CHECK(!point.inside);

  VrTouchScreen flipped = vrFlipTouchScreen(screen);
  CHECK_NEAR(vrPointOnScreen(flipped, { 0.0f, 0.0f, 0.5f }).distance, -0.5f, 1e-5);
}


TEST_CASE(touch_screen_rejects_degenerate_meshes) {
  const VrVector3 line[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
  const VrVector2 uvs[]  = { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f } };
  const uint16_t triangle[] = { 0, 1, 2 };
  CHECK(!vrFitTouchScreen(line, uvs, 3, triangle, 1).valid);

  // Texture coordinates that do not vary
  const VrVector3 flat[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
  const VrVector2 same[] = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f } };
  CHECK(!vrFitTouchScreen(flat, same, 3, triangle, 1).valid);

  // An index past the vertices
  const uint16_t bad[] = { 0, 1, 7 };
  CHECK(!vrFitTouchScreen(flat, uvs, 3, bad, 1).valid);
}


TEST_CASE(touch_hover_moves_cursor_and_aims_at_screen) {
  VrPipBoyTouch touch;

  VrPipBoyTouchState state = step(touch, onScreen(0.2f, 0.3f, 0.4f));
  CHECK(!state.cursorActive);
  CHECK(state.aim.kind == VrPipBoyAimKind::None);

  state = step(touch, onScreen(0.05f, 0.3f, 0.4f));
  CHECK(state.cursorActive);
  CHECK(!state.button);
  CHECK(state.aim.kind == VrPipBoyAimKind::Screen);
  CHECK_NEAR(state.cursor.x, 0.3f, 1e-6);
  CHECK_NEAR(state.cursor.y, 0.4f, 1e-6);

  // Away again: the finger keeps the cursor, the pick sees nothing
  state = step(touch, onScreen(0.3f, 0.3f, 0.4f));
  CHECK(state.cursorActive);
  CHECK(state.aim.kind == VrPipBoyAimKind::None);

  // The gamepad takes it back
  state = step(touch, onScreen(0.3f, 0.3f, 0.4f), true);
  CHECK(!state.cursorActive);
}


TEST_CASE(touch_tap_clicks_where_it_touched) {
  VrPipBoyTouch touch;
  step(touch, onScreen(0.03f, 0.6f, 0.2f));

  VrPipBoyTouchState state = step(touch, onScreen(0.002f, 0.6f, 0.2f));
  CHECK(!state.button);
  CHECK(state.haptic > 0.0f);
  CHECK(state.aim.kind == VrPipBoyAimKind::Screen);

  // Pushed a little through, moved a hair: still the same touch
  state = step(touch, onScreen(-0.01f, 0.605f, 0.2f));
  CHECK(!state.button);
  CHECK_NEAR(state.cursor.x, 0.6f, 1e-6);

  // Lifted: pressed on this frame, released on the next, where it touched
  state = step(touch, onScreen(0.03f, 0.61f, 0.2f));
  CHECK(state.button);
  CHECK_NEAR(state.aim.surface.x, 0.6f, 1e-6);

  state = step(touch, onScreen(0.03f, 0.61f, 0.2f));
  CHECK(!state.button);
  CHECK_NEAR(state.aim.surface.x, 0.6f, 1e-6);

  state = step(touch, onScreen(0.03f, 0.61f, 0.2f));
  CHECK(!state.button);
  CHECK_NEAR(state.aim.surface.x, 0.61f, 1e-6);
}


TEST_CASE(touch_needs_finger_from_the_front) {
  VrPipBoyTouch touch;

  // A hand coming from behind or straight onto the glass does not touch
  VrPipBoyTouchState state = step(touch, onScreen(-0.02f, 0.5f, 0.5f));
  CHECK(state.haptic == 0.0f);
  state = step(touch, onScreen(0.0f, 0.5f, 0.5f));
  CHECK(state.haptic == 0.0f);
  state = step(touch, onScreen(0.03f, 0.5f, 0.5f));
  state = step(touch, onScreen(0.03f, 0.5f, 0.5f));
  CHECK(!state.button);
}


TEST_CASE(touch_slide_scrolls_without_clicking) {
  VrPipBoyTouch touch;
  step(touch, onScreen(0.03f, 0.5f, 0.5f));
  step(touch, onScreen(0.0f, 0.5f, 0.5f));

  int32_t wheel = 0;

  // Slide down 3 cm along the screen (v grows downwards)
  for (uint32_t i = 1; i <= 10; i++) {
    float v = 0.5f + float(i) * 0.05f;
    wheel += step(touch, onScreen(0.0f, 0.5f, std::min(v, 1.0f))).wheel;
  }

  CHECK(wheel > 0);
  CHECK(wheel % VrPipBoyTouch::WheelNotch == 0);

  VrPipBoyTouchState state = step(touch, onScreen(0.04f, 0.5f, 1.0f));
  CHECK(!state.button);
  state = step(touch, onScreen(0.04f, 0.5f, 1.0f));
  CHECK(!state.button);
}


TEST_CASE(touch_taps_tab_buttons) {
  VrPipBoyTouch touch;

  VrPipBoyTouchPoint point;
  point.inside = false;
  point.button = 2;
  point.buttonDistance = 0.02f;

  VrPipBoyTouchState state = step(touch, point);
  CHECK(state.aim.kind == VrPipBoyAimKind::Button);
  CHECK(state.aim.button == 2);

  point.buttonDistance = 0.0f;
  state = step(touch, point);
  CHECK(state.haptic > 0.0f);
  CHECK(!state.button);

  point.buttonDistance = 0.03f;
  state = step(touch, point);
  CHECK(state.button);
  CHECK(state.aim.button == 2);

  state = step(touch, point);
  CHECK(!state.button);
}


TEST_CASE(touch_ends_without_pip_boy) {
  VrPipBoyTouch touch;
  step(touch, onScreen(0.03f, 0.5f, 0.5f));
  CHECK(step(touch, onScreen(0.03f, 0.5f, 0.5f)).cursorActive);

  VrPipBoyTouchInput input;
  CHECK(!touch.update(input).cursorActive);
  CHECK(!step(touch, onScreen(0.3f, 0.5f, 0.5f)).cursorActive);
}
