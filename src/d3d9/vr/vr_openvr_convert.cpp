#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_openvr_convert.h"

namespace dxvk {

  namespace {

    bool isPressed(uint64_t mask, uint32_t id) {
      return (mask & (uint64_t(1) << id)) != 0;
    }

  }


  VrPose vrPoseFromMatrix34(const float m[3][4]) {
    VrQuaternion q;
    float trace = m[0][0] + m[1][1] + m[2][2];

    if (trace > 0.0f) {
      float s = std::sqrt(trace + 1.0f) * 2.0f;
      q.w = 0.25f * s;
      q.x = (m[2][1] - m[1][2]) / s;
      q.y = (m[0][2] - m[2][0]) / s;
      q.z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
      float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
      q.w = (m[2][1] - m[1][2]) / s;
      q.x = 0.25f * s;
      q.y = (m[0][1] + m[1][0]) / s;
      q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
      float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
      q.w = (m[0][2] - m[2][0]) / s;
      q.x = (m[0][1] + m[1][0]) / s;
      q.y = 0.25f * s;
      q.z = (m[1][2] + m[2][1]) / s;
    } else {
      float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
      q.w = (m[1][0] - m[0][1]) / s;
      q.x = (m[0][2] + m[2][0]) / s;
      q.y = (m[1][2] + m[2][1]) / s;
      q.z = 0.25f * s;
    }

    VrPose pose;
    pose.orientation = vrNormalize(q);
    pose.position    = { m[0][3], m[1][3], m[2][3] };
    return pose;
  }


  VrFov vrFovFromProjectionRaw(float left, float right, float top, float bottom) {
    VrFov fov;
    fov.angleLeft  = std::atan(left);
    fov.angleRight = std::atan(right);
    fov.angleUp    = std::atan(-top);
    fov.angleDown  = std::atan(-bottom);
    return fov;
  }


  uint32_t vrButtonsFromOpenVr(uint64_t pressed) {
    uint32_t buttons = 0;

    if (isPressed(pressed, VrOpenVrButtonId::A))
      buttons |= uint32_t(VrButton::Primary);

    if (isPressed(pressed, VrOpenVrButtonId::ApplicationMenu))
      buttons |= uint32_t(VrButton::Secondary);

    if (isPressed(pressed, VrOpenVrButtonId::Axis0))
      buttons |= uint32_t(VrButton::Stick);

    return buttons;
  }


  uint16_t vrHapticMicroseconds(float amplitude, int64_t durationNs) {
    float strength = std::max(0.0f, std::min(1.0f, amplitude));
    float duration = float(std::max<int64_t>(0, durationNs)) * 1e-3f;

    float micros = std::min(duration * strength, float(VrOpenVrMaxHapticMicroseconds));
    return uint16_t(micros);
  }

}
