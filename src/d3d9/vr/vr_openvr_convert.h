#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Converts an OpenVR 3x4 row-major transform to a pose
   *
   * OpenVR uses the same axes as the rest of the VR layer (right-handed,
   * +Y up, -Z forward, metres), so only the representation changes. The
   * rotation part is assumed to be a rotation without scale.
   */
  VrPose vrPoseFromMatrix34(const float m[3][4]);

  /**
   * \brief Converts a pose to an OpenVR 3x4 row-major transform
   *
   * Inverse of vrPoseFromMatrix34.
   */
  void vrPoseToMatrix34(const VrPose& pose, float m[3][4]);

  /**
   * \brief Converts the tangents from IVRSystem::GetProjectionRaw to angles
   *
   * Assumes OpenVR's convention that the vertical values are negated
   * compared to the usual one, so that \c top is the negative tangent of the
   * upward angle. This is not verified with a headset.
   */
  VrFov vrFovFromProjectionRaw(float left, float right, float top, float bottom);

  /**
   * \brief Paths of the OpenVR action sets, indexed by VrInputContext
   */
  constexpr const char* VrOpenVrActionSets[] = {
    "/actions/game",
    "/actions/menu",
  };

  /**
   * \brief Path of a digital action in the action manifest
   *
   * Game actions are in the game set and menu actions in the menu set,
   * see vrOpenVrActionContext.
   */
  const char* vrOpenVrActionPath(VrAction action);

  /**
   * \brief Context in which an action is read
   */
  VrInputContext vrOpenVrActionContext(VrAction action);

  /**
   * \brief Path of the stick action of a context
   *
   * \param [in] context Input context
   * \param [in] turn \c true for the second stick: turning in game, the
   *    secondary stick in menus (the lockpick's screwdriver, for example)
   */
  const char* vrOpenVrStickPath(VrInputContext context, bool turn);

  /**
   * \brief Path of the haptic output action of a hand
   */
  const char* vrOpenVrHapticPath(VrHand hand);

  /**
   * \brief Path of the action set read in every context
   *
   * It holds the hand poses, which are needed in game and in menus.
   */
  constexpr const char* VrOpenVrHandsActionSet = "/actions/hands";

  /**
   * \brief Path of the pose action a hand points with
   */
  const char* vrOpenVrAimPath(VrHand hand);

  /**
   * \brief Places a laser's overlay so that it faces the eye
   *
   * An overlay is a flat quad, so the laser is a narrow quad whose height
   * runs along the laser and which is turned around the laser towards the
   * eye. Without that turn it would be seen edge-on from some angles.
   *
   * \param [in] origin Start of the laser, which goes along its -Z axis
   * \param [in] length Length of the laser, in metres
   * \param [in] eye Position of the eye the laser faces
   * \param [out] m Transform of the overlay's centre
   */
  void vrComputeBeamTransform(const VrPose& origin, float length, const VrVector3& eye, float m[3][4]);

}
