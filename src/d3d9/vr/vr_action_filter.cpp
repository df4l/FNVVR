#include "vr_action_filter.h"

namespace dxvk {

  VrActionState VrActionFilter::update(
    const VrActionState&        actions,
          VrInputContext        context,
          float                 dt) {
    if (context != m_context) {
      m_context    = context;
      m_blocked    = actions.pressed;
      m_pipBoyDown = false;
      m_tapPulse   = 0.0f;
      m_holdPulse  = 0.0f;
    }

    m_blocked &= actions.pressed;

    VrActionState result = actions;
    result.pressed &= ~m_blocked;

    if (context != VrInputContext::Game)
      return result;

    updatePipBoy(result.isPressed(VrAction::PipBoy), dt);

    result.setPressed(VrAction::PipBoy, m_tapPulse > 0.0f);
    result.setPressed(VrAction::Pause,  m_holdPulse > 0.0f || result.isPressed(VrAction::Pause));

    m_tapPulse  -= dt;
    m_holdPulse -= dt;
    return result;
  }


  void VrActionFilter::updatePipBoy(bool down, float dt) {
    if (down) {
      if (m_pipBoyDown) {
        m_heldTime += dt;
      } else {
        m_heldTime  = 0.0f;
        m_holdFired = false;
      }

      if (!m_holdFired && m_heldTime >= HoldTime) {
        m_holdFired = true;
        m_holdPulse = PulseTime;
      }
    } else if (m_pipBoyDown && !m_holdFired) {
      m_tapPulse = PulseTime;
    }

    m_pipBoyDown = down;
  }

}
