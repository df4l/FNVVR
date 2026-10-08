#include "test.h"

#include "../vr_gamepad_head.h"

using namespace dxvk;

namespace {
  constexpr uint16_t Chord = VrGamepadHeadButton::LeftShoulder | VrGamepadHeadButton::RightShoulder;
}


TEST_CASE(stick_dead_zone_gives_zero) {
  CHECK(vrStickAxis(0) == 0.0f);
  CHECK(vrStickAxis(5000) == 0.0f);
  CHECK(vrStickAxis(-5000) == 0.0f);
}


TEST_CASE(stick_full_deflection_gives_one) {
  CHECK(vrStickAxis(32767) == 1.0f);
  CHECK(vrStickAxis(-32768) == -1.0f);
}


TEST_CASE(without_the_chord_nothing_is_taken) {
  VrGamepadHead head;
  VrGamepadSample sample;
  sample.buttons     = VrGamepadHeadButton::LeftShoulder;
  sample.thumbLeftY  = 32767;

  head.apply(sample);

  CHECK(sample.buttons == VrGamepadHeadButton::LeftShoulder);
  CHECK(sample.thumbLeftY == 32767);
  CHECK(head.input().moveForward == 0.0f);
}


TEST_CASE(chord_moves_the_head_and_blanks_the_sample) {
  VrGamepadHead head;
  VrGamepadSample sample;
  sample.buttons      = Chord | VrGamepadHeadButton::DpadUp;
  sample.thumbLeftX   = 32767;
  sample.thumbLeftY   = -32768;
  sample.thumbRightX  = 32767;
  sample.thumbRightY  = 32767;

  head.apply(sample);
  VrEmulatorInput input = head.input();

  CHECK(input.moveRight == 1.0f);
  CHECK(input.moveForward == -1.0f);
  CHECK(input.moveUp == 1.0f);
  CHECK(input.mouseDeltaX == VrGamepadHead::TurnPixelsPerFrame);
  CHECK(input.mouseDeltaY == -VrGamepadHead::TurnPixelsPerFrame);
  CHECK(!input.recenter);
  CHECK(sample.buttons == 0);
  CHECK(sample.thumbLeftX == 0);
  CHECK(sample.thumbRightY == 0);
}


TEST_CASE(y_recentres_and_releasing_the_chord_stops_the_head) {
  VrGamepadHead head;
  VrGamepadSample sample;
  sample.buttons = Chord | VrGamepadHeadButton::Y;
  head.apply(sample);
  CHECK(head.input().recenter);

  VrGamepadSample released;
  head.apply(released);
  CHECK(!head.input().recenter);
}


TEST_CASE(merged_axes_stay_in_range) {
  VrEmulatorInput a, b;
  a.moveRight = 1.0f;
  b.moveRight = 1.0f;
  a.mouseDeltaX = 6.0f;
  b.mouseDeltaX = 6.0f;
  b.recenter = true;

  VrEmulatorInput merged = vrMergeInput(a, b);

  CHECK(merged.moveRight == 1.0f);
  CHECK(merged.mouseDeltaX == 12.0f);
  CHECK(merged.recenter);
}
