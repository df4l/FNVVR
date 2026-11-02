#include <cmath>

#include "test.h"

#include "../vr_launcher_input.h"

using namespace dxvk;

namespace {

  using Kind = VrLauncherMouseEvent::Kind;

  VrPointerState cursorAt(float u, float v, bool button = false, int32_t wheel = 0) {
    VrPointerState state;
    state.cursorActive = true;
    state.cursor       = VrVector2 { u, v };
    state.button       = button;
    state.wheel        = wheel;
    return state;
  }

  VrScreenRect canvasAtOrigin() {
    VrScreenRect canvas;
    canvas.right  = VrLauncherCanvasWidth;
    canvas.bottom = VrLauncherCanvasHeight;
    return canvas;
  }

}


TEST_CASE(launcher_canvas_is_centred_on_the_main_window) {
  VrScreenRect window;
  window.left   = 1000;
  window.top    = 500;
  window.right  = 1794;
  window.bottom = 890;

  VrScreenRect canvas = vrLauncherCanvas(window);

  CHECK(canvas.right  - canvas.left == VrLauncherCanvasWidth);
  CHECK(canvas.bottom - canvas.top  == VrLauncherCanvasHeight);
  CHECK((canvas.left + canvas.right)  / 2 == (window.left + window.right)  / 2);
  CHECK((canvas.top  + canvas.bottom) / 2 == (window.top  + window.bottom) / 2);
}


TEST_CASE(launcher_points_stay_inside_the_canvas) {
  VrScreenRect canvas = vrLauncherCanvas(VrScreenRect { 100, 100, 300, 300 });

  VrScreenPoint first = vrLauncherPoint(canvas, VrVector2 { 0.0f, 0.0f });
  CHECK(first.x == canvas.left);
  CHECK(first.y == canvas.top);

  VrScreenPoint last = vrLauncherPoint(canvas, VrVector2 { 1.0f, 1.0f });
  CHECK(last.x == canvas.right  - 1);
  CHECK(last.y == canvas.bottom - 1);

  VrScreenPoint middle = vrLauncherPoint(canvas, VrVector2 { 0.5f, 0.5f });
  CHECK(middle.x == canvas.left + VrLauncherCanvasWidth  / 2);
  CHECK(middle.y == canvas.top  + VrLauncherCanvasHeight / 2);
}


TEST_CASE(launcher_panel_keeps_the_aspect_of_its_image) {
  VrPanelPlacement placement = vrLauncherPlacement(VrPose(), 2.0f);

  CHECK(std::abs(placement.width - 2.0f * float(VrLauncherCanvasWidth) / VrLauncherPixelsPerPanel) < 1e-5f);
  CHECK(std::abs(placement.width / placement.height
    - float(VrLauncherCanvasWidth) / float(VrLauncherCanvasHeight)) < 1e-5f);
}


TEST_CASE(launcher_click_moves_then_presses_then_releases) {
  VrLauncherMouse mouse;
  VrScreenRect canvas = canvasAtOrigin();

  auto events = mouse.update(cursorAt(0.5f, 0.5f, true), canvas);
  CHECK(events.size() == 2);
  CHECK(events[0].kind == Kind::Move);
  CHECK(!events[0].held);
  CHECK(events[1].kind == Kind::ButtonDown);
  CHECK(events[1].held);

  events = mouse.update(cursorAt(0.5f, 0.5f, false), canvas);
  CHECK(events.size() == 1);
  CHECK(events[0].kind == Kind::ButtonUp);
  CHECK(!events[0].held);
}


TEST_CASE(launcher_still_cursor_sends_nothing) {
  VrLauncherMouse mouse;
  VrScreenRect canvas = canvasAtOrigin();

  mouse.update(cursorAt(0.25f, 0.25f), canvas);
  CHECK(mouse.update(cursorAt(0.25f, 0.25f), canvas).empty());
}


TEST_CASE(launcher_drag_keeps_the_button_held) {
  VrLauncherMouse mouse;
  VrScreenRect canvas = canvasAtOrigin();

  mouse.update(cursorAt(0.25f, 0.25f, true), canvas);

  auto events = mouse.update(cursorAt(0.75f, 0.25f, true), canvas);
  CHECK(events.size() == 1);
  CHECK(events[0].kind == Kind::Move);
  CHECK(events[0].held);
}


TEST_CASE(launcher_held_button_is_released_when_the_cursor_leaves) {
  VrLauncherMouse mouse;
  VrScreenRect canvas = canvasAtOrigin();

  mouse.update(cursorAt(0.25f, 0.5f, true), canvas);

  auto events = mouse.update(VrPointerState(), canvas);
  CHECK(events.size() == 2);
  CHECK(events[0].kind == Kind::ButtonUp);
  CHECK(events[0].point.x == VrLauncherCanvasWidth / 4);
  CHECK(events[1].kind == Kind::Leave);

  CHECK(mouse.update(VrPointerState(), canvas).empty());
}


TEST_CASE(launcher_wheel_is_passed_on) {
  VrLauncherMouse mouse;
  VrScreenRect canvas = canvasAtOrigin();

  mouse.update(cursorAt(0.5f, 0.5f), canvas);

  auto events = mouse.update(cursorAt(0.5f, 0.5f, false, -120), canvas);
  CHECK(events.size() == 1);
  CHECK(events[0].kind == Kind::Wheel);
  CHECK(events[0].wheel == -120);
}
