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


  void VrControls::setTurnConfig(const VrTurnConfig& turn) {
    if (m_headLook)
      m_headLook->setTurnConfig(turn);
  }


  void VrControls::setSneak(std::unique_ptr<VrSneak> sneak) {
    m_sneak = std::move(sneak);
  }


  void VrControls::setSneakConfig(const VrSneakConfig& config) {
    if (m_sneak)
      m_sneak->setConfig(config);
  }


  void VrControls::update(
    const VrInputState&         input,
          VrGameStateKind       state,
    const VrPose*               reference,
    const VrPanelPlacement*     menuPanel,
          float                 dt) {
    // The actions were read in the context chosen on the previous frame
    VrActionState actions = m_filter.update(input.actions, m_context, dt);

    if (m_sneak) {
      bool inGame = state == VrGameStateKind::InGame && m_context == VrInputContext::Game;
      m_sneak->update(input.isHeadTracked ? &input.headPose.position.y : nullptr, inGame, dt);
    }

    if (m_terminalPanel && m_context == VrInputContext::Menu && input.hasActions)
      updateTerminal(input, actions, dt);
    else
      updateGameInput(input, actions, menuPanel, dt);

    if (m_headLook) {
      VrPose head;
      bool tracked = input.isHeadTracked && reference;

      if (tracked)
        head = vrComputeEyeInReference(*reference, input.headPose);

      float turn = m_context == VrInputContext::Game && !m_rightStickTaken ? input.actions.turn.x : 0.0f;
      m_headLook->update(tracked ? &head : nullptr, turn, dt);
    }

    m_context = state == VrGameStateKind::InGame
      ? VrInputContext::Game
      : VrInputContext::Menu;

    m_backend.setInputContext(m_context);
  }


  void VrControls::updateGameInput(
    const VrInputState&         input,
          VrActionState&        actions,
    const VrPanelPlacement*     menuPanel,
          float                 dt) {
    m_terminalPointer  = VrPointerState();
    m_terminalBack     = false;
    m_terminalBackDown = actions.isPressed(VrAction::MenuBack);

    // Pressed and released on alternate frames: a button held down does not
    // close the Pip-Boy until it is released
    m_menuBackDown = m_menuBack && !m_menuBackDown && m_context == VrInputContext::Menu;

    if (m_menuBackDown)
      actions.setPressed(VrAction::MenuBack, true);

    if ((m_swingsWithHand || m_handsFree) && m_context == VrInputContext::Game)
      actions.setPressed(VrAction::Attack, false);

    // The crouch sneaks instead of the button
    bool crouchSneaks = m_sneak && m_sneak->physical() && m_context == VrInputContext::Game;

    if (crouchSneaks)
      actions.setPressed(VrAction::Sneak, false);

    const VrPanelPlacement* panel = nullptr;

    if (m_context == VrInputContext::Menu && input.hasActions && VrMenuCursor::pointerAllowed())
      panel = menuPanel;

    bool padUsed = std::max(std::abs(actions.move.x), std::abs(actions.move.y)) >= NavigateThreshold;

    for (VrAction action : MenuButtons)
      padUsed |= actions.isPressed(action);

    VrPointerState pointer = updatePointer(input, actions, panel, padUsed, dt);
    VrGamepadHook::setCursorMode(pointer.cursorActive);
    VrMenuCursor::update(pointer);

    VrMenuNavigation navigation = m_hotKeyWheelShown ? VrMenuNavigation::HotKeyWheel : VrMenuNavigation::List;
    VrPadState pad = vrComputePadState(actions, m_context, gameBindings(), navigation);

    if (m_gameAim && m_context == VrInputContext::Game)
      vrPressGameControl(pad, VrGameControl::Aim, gameBindings());

    if (crouchSneaks && m_sneak->holdsSneakControl())
      vrPressGameControl(pad, VrGameControl::Sneak, gameBindings());

    // The game switches the light once its Pip-Boy control is held long
    // enough, and does not open the Pip-Boy on that release
    if (m_filter.holdsPipBoyControl() && m_context == VrInputContext::Game)
      vrPressGameControl(pad, VrGameControl::MenuMode, gameBindings());

    VrGamepadHook::setControllerPad(input.hasActions, pad);
  }


  void VrControls::updateTerminal(
    const VrInputState&         input,
          VrActionState&        actions,
          float                 dt) {
    bool back = actions.isPressed(VrAction::MenuBack);
    m_terminalBack     = back && !m_terminalBackDown;
    m_terminalBackDown = back;

    // The sticks and buttons never take the lasers off the terminal
    m_terminalPointer = updatePointer(input, actions, m_terminalPanel, false, dt);

    // The console has the input: the game's cursor rests and the game's
    // menus see a gamepad at rest
    VrGamepadHook::setCursorMode(false);
    VrMenuCursor::update(VrPointerState());
    VrGamepadHook::setControllerPad(input.hasActions, VrPadState());
  }


  VrPointerState VrControls::updatePointer(
    const VrInputState&         input,
          VrActionState&        actions,
    const VrPanelPlacement*     panel,
          bool                  padUsed,
          float                 dt) {
    VrPointerInput pointerInput;
    pointerInput.controllers = input.controllers;
    pointerInput.dt          = dt;
    pointerInput.panel       = panel;
    pointerInput.padUsed     = padUsed;

    pointerInput.triggers[uint32_t(VrHand::Left)]  = actions.isPressed(VrAction::MenuLeftTrigger);
    pointerInput.triggers[uint32_t(VrHand::Right)] = actions.isPressed(VrAction::MenuRightTrigger);
    pointerInput.wheel = m_rightStickTaken ? 0.0f : actions.turn.y;

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


  bool VrControls::updateCamera(VrGameCameraPose& pose, float dt) {
    bool vats = m_headLook && m_headLook->adjustVatsCamera(pose);
    bool adjusted = !vats && m_headLook && m_headLook->adjustCamera(pose);

    // Only a camera that follows the head is measured: not in V.A.T.S.,
    // on furniture or when dead, where it is not raised either
    if (m_sneak) {
      m_sneak->measureCamera(adjusted ? &pose : nullptr, dt);

      if (adjusted)
        m_sneak->adjustCamera(pose);
    }

    return vats || adjusted;
  }


  bool VrControls::adjustCamera(VrGameCameraPose& pose) const {
    if (m_headLook && m_headLook->adjustVatsCamera(pose))
      return true;

    bool adjusted = m_headLook && m_headLook->adjustCamera(pose);

    if (adjusted && m_sneak)
      m_sneak->adjustCamera(pose);

    return adjusted;
  }


  void* VrControls::takeDialogueSpeaker() {
    return m_headLook ? m_headLook->takeDialogueSpeaker() : nullptr;
  }

}
