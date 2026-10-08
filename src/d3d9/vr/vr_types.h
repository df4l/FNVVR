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

  enum class VrButton : uint32_t {
    Primary   = 1u << 0,
    Secondary = 1u << 1,
    Menu      = 1u << 2,
    Stick     = 1u << 3,
  };

  /**
   * \brief State of one hand controller
   *
   * Poses are only meaningful while isActive is true.
   */
  struct VrControllerState {
    bool      isActive = false;
    VrPose    gripPose;
    VrPose    aimPose;
    float     trigger  = 0.0f;
    float     squeeze  = 0.0f;
    VrVector2 stick;
    uint32_t  buttons  = 0;

    bool isPressed(VrButton button) const {
      return (buttons & uint32_t(button)) != 0;
    }
  };

  /**
   * \brief Head and controller state sampled for one frame
   */
  struct VrInputState {
    bool              isHeadTracked = false;
    VrPose            headPose;
    std::array<VrControllerState, VrHandCount> controllers;
  };

}
