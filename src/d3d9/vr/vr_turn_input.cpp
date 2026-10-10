#include <algorithm>
#include <cmath>

#include "vr_turn_input.h"

namespace dxvk {

  namespace {

    constexpr float RadiansPerDegree = 0.017453293f;

  }


  VrTurnInput::VrTurnInput(const VrTurnConfig& config)
  : m_config(config) { }


  float VrTurnInput::update(float stickX, float dt) {
    float deflection = std::abs(stickX);

    if (m_config.smooth) {
      if (deflection <= DeadZone)
        return 0.0f;

      float scale = std::min(1.0f, (deflection - DeadZone) / (1.0f - DeadZone));
      return std::copysign(scale * m_config.smoothSpeed * RadiansPerDegree * dt, stickX);
    }

    if (deflection < SnapRelease) {
      m_armed = true;
      return 0.0f;
    }

    if (!m_armed || deflection < SnapPress)
      return 0.0f;

    m_armed = false;
    return std::copysign(m_config.snapAngle * RadiansPerDegree, stickX);
  }


  void VrTurnInput::reset() {
    m_armed = false;
  }

}
