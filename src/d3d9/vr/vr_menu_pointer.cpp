#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_menu_pointer.h"

namespace dxvk {

  VrPanelHit vrIntersectPanel(const VrPose& ray, const VrPanelPlacement& panel) {
    VrPanelHit result;

    if (panel.width <= 0.0f || panel.height <= 0.0f)
      return result;

    // In the panel's frame the panel lies in the plane z = 0 and faces +Z
    VrPose toPanel = vrInverse(panel.pose);
    VrVector3 origin    = vrTransformPoint(toPanel, ray.position);
    VrVector3 direction = vrRotate(toPanel.orientation,
      vrRotate(ray.orientation, VrVector3 { 0.0f, 0.0f, -1.0f }));

    // Only from the front, towards the panel
    if (origin.z <= 0.0f || direction.z >= 0.0f)
      return result;

    float distance = -origin.z / direction.z;
    VrVector3 point = origin + direction * distance;

    float u = point.x / panel.width + 0.5f;
    float v = 0.5f - point.y / panel.height;

    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
      return result;

    result.hit      = true;
    result.uv       = { u, v };
    result.distance = distance;
    return result;
  }


  VrPointerState VrMenuPointer::update(const VrPointerInput& input) {
    VrPointerState state;

    std::array<VrPanelHit, VrHandCount> hits;
    std::array<bool, VrHandCount> pressed = { };

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrControllerState& controller = input.controllers[i];

      if (input.panel && controller.isActive)
        hits[i] = vrIntersectPanel(controller.aimPose, *input.panel);

      pressed[i] = input.triggers[i] && !m_triggers[i];
      m_triggers[i] = input.triggers[i];
    }

    // A press selects the hand, as long as it points at the panel
    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (pressed[i] && hits[i].hit)
        m_hand = VrHand(i);
    }

    uint32_t other = 1u - uint32_t(m_hand);

    if (!hits[uint32_t(m_hand)].hit && hits[other].hit)
      m_hand = VrHand(other);

    const VrPanelHit& hit = hits[uint32_t(m_hand)];

    if (!hit.hit) {
      m_cursorActive = false;
      m_hasAnchor    = false;
    } else if (input.padUsed) {
      // The laser has to move away from here to take the menus back
      m_cursorActive = false;
      m_hasAnchor    = true;
      m_anchor       = hit.uv;
    } else if (!m_cursorActive) {
      float moved = std::hypot(hit.uv.x - m_anchor.x, hit.uv.y - m_anchor.y);
      m_cursorActive = !m_hasAnchor || moved >= MoveThreshold || pressed[uint32_t(m_hand)];
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      state.beams[i].visible = hits[i].hit;
      state.beams[i].length  = hits[i].distance;
      state.beams[i].active  = m_cursorActive && VrHand(i) == m_hand;
    }

    state.cursorActive  = m_cursorActive;
    state.triggersTaken = m_cursorActive;

    if (m_cursorActive) {
      state.cursor = hit.uv;
      state.button = input.triggers[uint32_t(m_hand)];
      state.wheel  = updateWheel(input.wheel, input.dt);
    } else {
      m_wheelTimer = 0.0f;
    }

    return state;
  }


  int32_t VrMenuPointer::updateWheel(float stick, float dt) {
    if (std::abs(stick) < WheelThreshold) {
      m_wheelTimer = 0.0f;
      return 0;
    }

    // One notch at once, then one per repeat time while the stick is held
    m_wheelTimer -= dt;

    if (m_wheelTimer > 0.0f)
      return 0;

    m_wheelTimer = WheelRepeatTime;
    return stick > 0.0f ? WheelNotch : -WheelNotch;
  }

}
