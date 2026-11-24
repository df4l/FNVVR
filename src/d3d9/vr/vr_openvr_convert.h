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
   * OpenVR's \c top is the tangent of the downward angle (negative) and its
   * \c bottom the tangent of the upward angle, which is how SteamVR's own
   * OpenXR runtime converts them. Headsets with more field of view below
   * the eye than above (Quest 2 over Link, Quest 3) report \c -top larger
   * than \c bottom; reading \c top as the upward angle shifts the image
   * vertically, which looks like a pitch-dependent stretch and a shear
   * towards the edges.
   */
  VrFov vrFovFromProjectionRaw(float left, float right, float top, float bottom);

  /**
   * \brief Converts a foveation centre to a gaze
   *
   * IVRSystem::GetEyeTrackedFoveationCenter gives where an eye looks as a
   * point in the normalised device coordinates of the eye's projection
   * matrix, which OpenVR composes from the tangents of
   * IVRSystem::GetProjectionRaw: -1 to 1 maps \c left to \c right and
   * \c top to \c bottom (\c bottom being the upward tangent, see
   * vrFovFromProjectionRaw).
   *
   * The tangents are used instead of IVRSystem::GetProjectionMatrix, which
   * returns a struct: called directly from 32-bit GCC code, it gets \c this
   * and the return address swapped (see getEyeToHeadTransform in
   * vr_openvr_backend.cpp) and the runtime aborts the game.
   *
   * \param [in] left Left tangent from GetProjectionRaw
   * \param [in] right Right tangent from GetProjectionRaw
   * \param [in] top Top tangent from GetProjectionRaw
   * \param [in] bottom Bottom tangent from GetProjectionRaw
   * \param [in] ndcX Horizontal coordinate of the centre
   * \param [in] ndcY Vertical coordinate of the centre
   */
  VrGaze vrGazeFromFoveationCenter(float left, float right, float top, float bottom, float ndcX, float ndcY);

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
   * \brief Path of the skeleton action a hand's fingers are read from
   */
  const char* vrOpenVrSkeletonPath(VrHand hand);

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
