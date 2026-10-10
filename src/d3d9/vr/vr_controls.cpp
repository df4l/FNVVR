#include <algorithm>
#include <cmath>

#include "vr_controller_pad.h"
#include "vr_controls.h"
#include "vr_game_addresses.h"
#include "vr_gamepad_hook.h"
#include "vr_menu_cursor.h"

namespace dxvk {

  namespace {

    const uint8_t* gameBindings() {
      auto input = *reinterpret_cast<const uint8_t* const*>(VrGame::InputGlobals);
      return input ? input + VrGame::InputControllerBindings : nullptr;
    }

    // Deflection of the navigation stick that counts as gamepad navigation
    constexpr float NavigateThreshold = 0.5f;

    constexpr VrAction MenuButtons[] = {
      VrAction::MenuSelect,
      VrAction::MenuBack,
      VrAction::MenuAlternate,
      VrAction::MenuOption,
      VrAction::MenuPrevious,
      VrAction::MenuNext,
    };

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
    const VrPanelPlacement*     menuPanel,
          float                 dt) {
    // The actions were read in the context chosen on the previous frame
    VrActionState actions = m_filter.update(input.actions, m_context, dt);

    VrPointerState pointer = updatePointer(input, actions, menuPanel, dt);
    VrGamepadHook::setCursorMode(pointer.cursorActive);
    VrMenuCursor::update(pointer);

    VrPadState pad = vrComputePadState(actions, m_context, gameBindings());

    if (m_gameAim && m_context == VrInputContext::Game)
      vrPressGameControl(pad, VrGameControl::Aim, gameBindings());

    VrGamepadHook::setControllerPad(input.hasActions, pad);

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


  VrPointerState VrControls::updatePointer(
    const VrInputState&         input,
          VrActionState&        actions,
    const VrPanelPlacement*     menuPanel,
          float                 dt) {
    VrPointerInput pointerInput;
    pointerInput.controllers = input.controllers;
    pointerInput.dt          = dt;

    if (m_context == VrInputContext::Menu && input.hasActions && VrMenuCursor::pointerAllowed())
      pointerInput.panel = menuPanel;

    pointerInput.triggers[uint32_t(VrHand::Left)]  = actions.isPressed(VrAction::MenuLeftTrigger);
    pointerInput.triggers[uint32_t(VrHand::Right)] = actions.isPressed(VrAction::MenuRightTrigger);
    pointerInput.wheel = actions.turn.y;

    pointerInput.padUsed = std::max(std::abs(actions.move.x), std::abs(actions.move.y)) >= NavigateThreshold;

    for (VrAction action : MenuButtons)
      pointerInput.padUsed |= actions.isPressed(action);

    VrPointerState pointer = m_pointer.update(pointerInput);

    // The triggers click while the lasers drive the cursor
    if (pointer.triggersTaken) {
      actions.setPressed(VrAction::MenuLeftTrigger,  false);
      actions.setPressed(VrAction::MenuRightTrigger, false);
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrPointerBeam& beam = pointer.beams[i];

      if (!beam.visible) {
        m_backend.hidePointer(VrHand(i));
        continue;
      }

      VrPointerSubmission submission;
      submission.origin = input.controllers[i].aimPose;
      submission.length = beam.length;
      submission.active = beam.active;
      m_backend.showPointer(VrHand(i), submission);
    }

    return pointer;
  }


  bool VrControls::adjustCamera(VrGameCameraPose& pose) const {
    return m_headLook && m_headLook->adjustCamera(pose);
  }

}
