#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Gamepad state reported to the game, as in XINPUT_GAMEPAD
   */
  struct VrPadState {
    uint16_t buttons      = 0;
    uint8_t  leftTrigger  = 0;
    uint8_t  rightTrigger = 0;
    int16_t  thumbLeftX   = 0;
    int16_t  thumbLeftY   = 0;
    int16_t  thumbRightX  = 0;
    int16_t  thumbRightY  = 0;
  };

  /**
   * \brief A mini-game's physical controls, as gamepad input
   *
   * See VrMinigames. What is driven replaces what the sticks and triggers of
   * the controllers would give; buttons are added.
   */
  struct VrMinigamePad {
    bool     active      = false;
    /// Buttons pressed by a gesture
    uint16_t buttons     = 0;
    /// Buttons of the controllers kept from the game, which the mini-game
    /// reads itself
    uint16_t noButtons   = 0;
    /// Left and right stick X, in [-1, 1]
    bool     driveLeftX  = false;
    float    leftX       = 0.0f;
    bool     driveRightX = false;
    float    rightX      = 0.0f;
    /// Keeps the triggers from the game, which some of these menus bind to
    /// debug tiles
    bool     noTriggers  = false;
    /// Same for the shoulder buttons, which are the grips
    bool     noShoulders = false;
    /// Triggers pressed by a gesture, also with \c noTriggers
    bool     leftTrigger  = false;
    bool     rightTrigger = false;
    /// The mini-game knows whether the laser is over something to click,
    /// where the interface sees a target everywhere
    bool     decidesTarget = false;
    bool     overTarget    = true;
  };

  /**
   * \brief Game controls the VR actions stand for
   *
   * Values of the game's ControlCode, see findings/input.md. The game keeps
   * one gamepad binding per control.
   */
  namespace VrGameControl {
    constexpr uint32_t Attack     = 0x04;
    constexpr uint32_t Activate   = 0x05;
    constexpr uint32_t Aim        = 0x06;
    constexpr uint32_t ReadyItem  = 0x07;
    constexpr uint32_t Sneak      = 0x08;
    constexpr uint32_t Jump       = 0x0C;
    constexpr uint32_t MenuMode   = 0x0E;
    constexpr uint32_t SleepWait  = 0x0F;
    constexpr uint32_t Vats       = 0x10;
    constexpr uint32_t Grab       = 0x1B;

    /// Number of controls with a binding
    constexpr uint32_t Count      = 0x1C;
  }

  /**
   * \brief Gamepad input codes of the game's bindings, see findings/input.md
   */
  namespace VrGamepadInput {
    constexpr uint8_t X = 0x0C;
  }

  /**
   * \brief What the navigation stick does in menus
   */
  enum class VrMenuNavigation {
    /// Moves through lists: the D-pad's dominant direction and the left
    /// thumbstick
    List,
    /// Picks a slot on the Pip-Boy inventory's hotkey wheel: the D-pad
    /// with its diagonals, no thumbstick
    HotKeyWheel,
  };

  /**
   * \brief Turns the controller actions into a gamepad state for the game
   *
   * The game supports an Xbox gamepad, and in gamepad mode it also accepts
   * analog movement. In game, each action presses the gamepad input the
   * game's own bindings give to its control, so that the action does what
   * the control does whatever the bindings are. The move stick is the left
   * thumbstick. The turn stick is not passed on: turning is done by
   * VrHeadLook. The two-handed grip is not passed on either: it is used by
   * VrWeaponHand, which presses the game's aim control through
   * vrPressGameControl only while the left hand supports a one-handed
   * weapon. Pause is the Start button, which the game hardwires.
   *
   * In menus the actions press the buttons the game's menus use: A selects,
   * B goes back, X and Y are the alternate actions, LB and RB change tabs,
   * the triggers are LT and RT (V.A.T.S. targets the selected body part
   * with RT) and the navigation stick is the D-pad. Both sticks are also passed on
   * as the thumbsticks, for menus that read them (lockpicking).
   *
   * The Pip-Boy inventory's hotkey wheel (shown while RB is held) reads the
   * D-pad alone, diagonals included, and gives the highlighted item the
   * slot of the first direction pressed. While it is shown the navigation
   * stick presses the D-pad in eight directions and is not passed on as the
   * left thumbstick: the interface would move the list with it.
   *
   * \param [in] actions Actions sampled for this frame
   * \param [in] context Context the actions were read in
   * \param [in] bindings The game's gamepad binding of each control, as
   *    gamepad input codes (see findings/input.md), \ref VrGameControl::Count
   *    entries; 0xFF is unbound. May be \c nullptr in menus.
   * \param [in] navigation What the navigation stick does in menus
   */
  VrPadState vrComputePadState(
    const VrActionState&        actions,
          VrInputContext        context,
    const uint8_t*              bindings,
          VrMenuNavigation      navigation = VrMenuNavigation::List);

  /**
   * \brief Presses the gamepad input the game binds to a control
   *
   * \param [in,out] pad Gamepad state
   * \param [in] control One of \ref VrGameControl
   * \param [in] bindings See vrComputePadState, may be \c nullptr
   */
  void vrPressGameControl(
          VrPadState&           pad,
          uint32_t              control,
    const uint8_t*              bindings);

  /**
   * \brief Button image the HUD shows for a gamepad input
   *
   * The value of the "_xbox_button" trait the game gives the interaction
   * prompt for the input bound to Activate, see findings/hud.md §9.2.
   *
   * \param [in] input Gamepad input code, as in the game's bindings
   * \returns The image, or 0 if the game shows none for that input
   */
  uint32_t vrButtonIconForInput(uint8_t input);

  /**
   * \brief Adds a mini-game's controls to a gamepad state
   */
  void vrApplyMinigamePad(VrPadState& pad, const VrMinigamePad& minigame);

}
