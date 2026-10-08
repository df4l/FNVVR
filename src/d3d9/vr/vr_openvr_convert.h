#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Button ids of the OpenVR legacy controller state
   *
   * Bit positions in VRControllerState_t::ulButtonPressed, as in
   * vr::EVRButtonId. They are repeated here so that the conversions below
   * need no OpenVR header and can be tested on their own.
   */
  namespace VrOpenVrButtonId {
    constexpr uint32_t ApplicationMenu = 1;
    constexpr uint32_t Grip            = 2;
    constexpr uint32_t A               = 7;
    constexpr uint32_t Axis0           = 32;
    constexpr uint32_t Axis1           = 33;
  }

  /// Longest haptic pulse that OpenVR accepts, in microseconds
  constexpr uint32_t VrOpenVrMaxHapticMicroseconds = 3999;

  /**
   * \brief Converts an OpenVR 3x4 row-major transform to a pose
   *
   * OpenVR uses the same axes as the rest of the VR layer (right-handed,
   * +Y up, -Z forward, metres), so only the representation changes. The
   * rotation part is assumed to be a rotation without scale.
   */
  VrPose vrPoseFromMatrix34(const float m[3][4]);

  /**
   * \brief Converts the tangents from IVRSystem::GetProjectionRaw to angles
   *
   * Assumes OpenVR's convention that the vertical values are negated
   * compared to the usual one, so that \c top is the negative tangent of the
   * upward angle. This is not verified with a headset.
   */
  VrFov vrFovFromProjectionRaw(float left, float right, float top, float bottom);

  /**
   * \brief Maps a pressed-button mask of the legacy controller state
   *
   * The primary button is the A button, the secondary one is the
   * application menu button and the stick button is the press of the first
   * axis. The legacy state has no separate menu button.
   */
  uint32_t vrButtonsFromOpenVr(uint64_t pressed);

  /**
   * \brief Length of the haptic pulse to request from OpenVR
   *
   * OpenVR's legacy pulse has a duration but no amplitude, so the
   * amplitude scales the duration. The result is limited to what OpenVR
   * accepts.
   */
  uint16_t vrHapticMicroseconds(float amplitude, int64_t durationNs);

}
