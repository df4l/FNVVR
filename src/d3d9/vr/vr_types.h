#pragma once

#include <array>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Coordinate conventions used by the VR layer
   *
   * All poses and vectors exchanged through IVRBackend use the OpenXR
   * convention: right-handed, +X right, +Y up, -Z forward, in metres.
   * Conversion to the game's own axes is done by a separate layer.
   */
  struct VrVector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct VrVector2 {
    float x = 0.0f;
    float y = 0.0f;
  };

  /**
   * \brief Unit quaternion, identity by default
   */
  struct VrQuaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
  };

  struct VrPose {
    VrQuaternion orientation;
    VrVector3    position;
  };

  /**
   * \brief Field of view as angles in radians
   *
   * Left and down are negative, right and up are positive,
   * matching the OpenXR XrFovf convention.
   */
  struct VrFov {
    float angleLeft  = 0.0f;
    float angleRight = 0.0f;
    float angleUp    = 0.0f;
    float angleDown  = 0.0f;
  };

  enum class VrEye : uint32_t {
    Left  = 0,
    Right = 1,
  };

  enum class VrHand : uint32_t {
    Left  = 0,
    Right = 1,
  };

  constexpr uint32_t VrEyeCount  = 2;
  constexpr uint32_t VrHandCount = 2;

  /**
   * \brief Pose and field of view of one eye
   */
  struct VrEyeView {
    VrPose pose;
    VrFov  fov;
  };

  /**
   * \brief Pixel size of one eye's render target
   */
  struct VrExtent {
    uint32_t width  = 0;
    uint32_t height = 0;
  };

  enum class VrSessionState : uint32_t {
    Idle,
    Ready,
    Running,
    Stopping,
    Lost,
  };

  /**
   * \brief Timing information returned by IVRBackend::waitFrame
   */
  struct VrFrameTiming {
    bool     shouldRender         = false;
    int64_t  predictedDisplayTime = 0;
    int64_t  predictedPeriod      = 0;
  };

  /**
   * \brief Pose of one hand controller
   *
   * Poses are only meaningful while isActive is true.
   */
  struct VrControllerState {
    bool      isActive = false;
    VrPose    gripPose;
    VrPose    aimPose;
  };

  /**
   * \brief Which set of actions the controllers drive
   *
   * The same physical button usually means something different in game
   * and in a menu, so the backend binds them separately. The user can
   * change both sets of bindings in the runtime's own interface.
   */
  enum class VrInputContext : uint32_t {
    Game,
    Menu,
  };

  /**
   * \brief Digital actions the controllers can trigger
   *
   * Actions are named after what they do, not after a button. The backend
   * maps controller buttons to them, so that the bindings can be changed in
   * the runtime. The game actions are read in the Game context and the menu
   * actions in the Menu context.
   */
  enum class VrAction : uint32_t {
    Attack,
    Aim,
    Activate,
    Jump,
    Reload,
    Sneak,
    PipBoy,
    Vats,
    Pause,
    Grab,
    MenuSelect,
    MenuBack,
    MenuAlternate,
    MenuOption,
    MenuPrevious,
    MenuNext,
    MenuLeftTrigger,
    MenuRightTrigger,
  };

  constexpr uint32_t VrActionCount = 18;

  /**
   * \brief State of the actions for one frame
   *
   * In the Game context, \c move is the locomotion stick and \c turn the
   * turning stick. In the Menu context, \c move navigates and \c turn is
   * the secondary stick, which some menus read directly. Stick values are
   * in [-1, 1], +X right and +Y forward.
   */
  struct VrActionState {
    uint32_t  pressed = 0;
    VrVector2 move;
    VrVector2 turn;

    bool isPressed(VrAction action) const {
      return (pressed & (1u << uint32_t(action))) != 0;
    }

    void setPressed(VrAction action, bool down) {
      uint32_t bit = 1u << uint32_t(action);
      pressed = down ? (pressed | bit) : (pressed & ~bit);
    }
  };

  /**
   * \brief Head and controller state sampled for one frame
   *
   * \c hasActions is false when the backend has no controller input, for
   * example when no controller is connected.
   */
  struct VrInputState {
    bool              isHeadTracked = false;
    VrPose            headPose;
    std::array<VrControllerState, VrHandCount> controllers;
    bool              hasActions = false;
    VrActionState     actions;
  };

}
