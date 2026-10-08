#include <algorithm>
#include <cmath>

#include "vr_gamepad_head.h"

namespace dxvk {

  namespace {

    // Fraction of the stick range that is ignored around the centre
    constexpr float StickDeadZone = 0.2f;

    constexpr uint16_t HeadChord = VrGamepadHeadButton::LeftShoulder | VrGamepadHeadButton::RightShoulder;

    float clampAxis(float value) {
      return std::max(-1.0f, std::min(1.0f, value));
    }

  }


  float vrStickAxis(int16_t value) {
    float normalized = std::max(-1.0f, float(value) / 32767.0f);
    float magnitude  = std::fabs(normalized);

    if (magnitude <= StickDeadZone)
      return 0.0f;

    float scaled = (magnitude - StickDeadZone) / (1.0f - StickDeadZone);
    return normalized < 0.0f ? -scaled : scaled;
  }


  VrEmulatorInput vrMergeInput(const VrEmulatorInput& a, const VrEmulatorInput& b) {
    VrEmulatorInput result;
    result.moveRight    = clampAxis(a.moveRight + b.moveRight);
    result.moveUp       = clampAxis(a.moveUp + b.moveUp);
    result.moveForward  = clampAxis(a.moveForward + b.moveForward);
    result.mouseDeltaX  = a.mouseDeltaX + b.mouseDeltaX;
    result.mouseDeltaY  = a.mouseDeltaY + b.mouseDeltaY;
    result.leftTrigger  = a.leftTrigger || b.leftTrigger;
    result.rightTrigger = a.rightTrigger || b.rightTrigger;
    result.recenter     = a.recenter || b.recenter;
    return result;
  }


  void VrGamepadHead::apply(VrGamepadSample& sample) {
    VrEmulatorInput input;

    if ((sample.buttons & HeadChord) == HeadChord) {
      bool up   = (sample.buttons & VrGamepadHeadButton::DpadUp) != 0;
      bool down = (sample.buttons & VrGamepadHeadButton::DpadDown) != 0;

      input.moveRight   = vrStickAxis(sample.thumbLeftX);
      input.moveForward = vrStickAxis(sample.thumbLeftY);
      input.moveUp      = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
      input.mouseDeltaX = vrStickAxis(sample.thumbRightX) * TurnPixelsPerFrame;
      input.mouseDeltaY = -vrStickAxis(sample.thumbRightY) * TurnPixelsPerFrame;
      input.recenter    = (sample.buttons & VrGamepadHeadButton::Y) != 0;

      sample = VrGamepadSample();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_input = input;
  }


  VrEmulatorInput VrGamepadHead::input() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_input;
  }

}
