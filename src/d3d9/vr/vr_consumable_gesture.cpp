#include <algorithm>
#include <cmath>

#include "vr_consumable_gesture.h"
#include "vr_math.h"

namespace dxvk {

  bool vrIsWhereConsumed(
          VrConsumeGesture      gesture,
    const VrGameCameraPose&     head,
    const VrVector3&            centre,
          float                 radius,
    const VrVector3&            otherHand,
          float                 margin,
    const VrConsumeZones&       zones) {
    constexpr float Unit = VrGameUnitsPerMetre;
    float bound = std::min(radius, zones.largestRadius * Unit);

    switch (gesture) {
      case VrConsumeGesture::Mouth: {
        VrVector3 mouth = head.position - head.up * (zones.mouthBelow * Unit) + head.forward * (zones.mouthAhead * Unit);
        return vrLength(centre - mouth) <= bound + (zones.mouthReach + margin) * Unit;
      }

      case VrConsumeGesture::Body: {
        if (vrLength(centre - otherHand) <= bound + (zones.armReach + margin) * Unit)
          return true;

        // Below the eyes in the world's vertical, near it horizontally
        VrVector3 offset = centre - head.position;
        float below = -offset.z;
        float around = std::sqrt(offset.x * offset.x + offset.y * offset.y);

        return below >= (zones.bodyBelowMin - margin) * Unit
            && below <= (zones.bodyBelowMax + margin) * Unit
            && around <= bound + (zones.bodyAround + margin) * Unit;
      }

      case VrConsumeGesture::Read: {
        VrVector3 offset = centre - head.position;
        float distance = vrLength(offset);

        if (distance < (zones.readNear - margin) * Unit || distance > (zones.readFar + margin) * Unit)
          return false;

        return vrDot(offset, head.forward) >= distance * std::cos(zones.readCone + margin);
      }

      case VrConsumeGesture::Instant:
        return true;
    }

    return false;
  }


  void VrConsumeGestureState::start(VrConsumeGesture gesture, bool hasCap, float useSeconds) {
    reset();
    m_gesture    = gesture;
    m_active     = true;
    m_hasCap     = hasCap;
    m_useSeconds = std::max(useSeconds, 0.0f);
  }


  void VrConsumeGestureState::reset() {
    *this = VrConsumeGestureState();
  }


  VrConsumeEvent VrConsumeGestureState::update(const VrConsumeInput& input, const VrConsumeZones& zones) {
    if (!m_active)
      return VrConsumeEvent::None;

    // The left hand holds the item while its grip is in
    if (input.grip) {
      m_gripped = true;
    } else if (m_gripped || (m_waited += input.dt) >= GripWait) {
      reset();
      return VrConsumeEvent::Cancelled;
    }

    bool otherPressed = input.otherGrip && !m_otherDown;
    m_otherDown = input.otherGrip;

    if (capOn()) {
      if (otherPressed && vrLength(input.otherHand - input.cap) <= zones.capReach * VrGameUnitsPerMetre) {
        m_capTaken = true;
        m_capHeld  = true;
        return VrConsumeEvent::CapTaken;
      }

      return VrConsumeEvent::None;
    }

    if (m_capHeld && !input.otherGrip) {
      m_capHeld = false;
      return VrConsumeEvent::CapDropped;
    }

    // A right hand still holding the cap is not the arm a needle goes into
    VrVector3 arm = input.otherHand;

    if (input.otherGrip)
      arm = arm + VrVector3 { 0.0f, 0.0f, 1.0e6f };

    float margin = m_atTarget ? zones.leaveMargin : 0.0f;
    bool there = vrIsWhereConsumed(m_gesture, input.head, input.centre, input.radius, arm, margin, zones);

    if (there != m_atTarget) {
      m_atTarget = there;
      return there ? VrConsumeEvent::Arrived : VrConsumeEvent::Left;
    }

    if (!there)
      return VrConsumeEvent::None;

    m_clock += input.dt;

    if (m_clock < m_useSeconds)
      return VrConsumeEvent::None;

    m_clock = m_useSeconds;
    m_active = false;
    return VrConsumeEvent::Consumed;
  }

}
