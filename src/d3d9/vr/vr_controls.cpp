#include "vr_controller_pad.h"
#include "vr_controls.h"
#include "vr_game_addresses.h"
#include "vr_gamepad_hook.h"

namespace dxvk {

  namespace {

    const uint8_t* gameBindings() {
      auto input = *reinterpret_cast<const uint8_t* const*>(VrGame::InputGlobals);
      return input ? input + VrGame::InputControllerBindings : nullptr;
    }

  }


  VrControls::VrControls(IVRBackend& backend)
  : m_backend(backend) { }


  void VrControls::setHeadLook(std::unique_ptr<VrHeadLook> headLook) {
    m_headLook = std::move(headLook);
  }


  void VrControls::update(
    const VrInputState&         input,
          VrGameStateKind       state,
    const VrPose*               reference,
          float                 dt) {
    // The actions were read in the context chosen on the previous frame
    VrActionState actions = m_filter.update(input.actions, m_context, dt);

    VrGamepadHook::setControllerPad(input.hasActions,
      vrComputePadState(actions, m_context, gameBindings()));

    if (m_headLook) {
      VrPose head;
      bool tracked = input.isHeadTracked && reference;

      if (tracked)
        head = vrComputeEyeInReference(*reference, input.headPose);

      float turn = m_context == VrInputContext::Game ? input.actions.turn.x : 0.0f;
      m_headLook->update(tracked ? &head : nullptr, turn, dt);
    }

    m_context = state == VrGameStateKind::InGame
      ? VrInputContext::Game
      : VrInputContext::Menu;

    m_backend.setInputContext(m_context);
  }


  bool VrControls::adjustCamera(VrGameCameraPose& pose) const {
    return m_headLook && m_headLook->adjustCamera(pose);
  }

}
