#pragma once

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief Rotation in the game's layout
   *
   * Row-major 3x3, like the game's NiMatrix3: it maps a vector from a
   * node's frame to its parent's, and the columns are the node's axes.
   */
  struct VrGameRotation {
    float m[3][3] = {
      { 1.0f, 0.0f, 0.0f },
      { 0.0f, 1.0f, 0.0f },
      { 0.0f, 0.0f, 1.0f },
    };
  };

  /**
   * \brief Rigid transform in game units
   */
  struct VrGameTransform {
    VrGameRotation rotate;
    VrVector3      translate;
  };

  VrGameRotation operator * (const VrGameRotation& a, const VrGameRotation& b);

  VrVector3 operator * (const VrGameRotation& r, const VrVector3& v);

  VrGameRotation vrTranspose(const VrGameRotation& r);

  /**
   * \brief Rotation of a camera pose: forward, up and right as columns
   */
  VrGameRotation vrCameraRotation(const VrGameCameraPose& pose);

  /**
   * \brief Computes how to move the first-person model to the hand
   *
   * The game draws the weapon and the arms as one model held in front of
   * its camera. Moving the whole model by the returned transform keeps
   * what the game animates relative to its camera, the weapon's angle and
   * its recoil included, but relative to the hand: the weapon turns with
   * the rotation from the game's camera to the hand, and its node lands on
   * the hand.
   *
   * \param [in] gameCamera Camera the game placed the model for
   * \param [in] weaponPosition Position of the weapon node
   * \param [in] hand Hand pose: forward is where it points
   * \returns Transform to apply on the left of the model's root
   */
  VrGameTransform vrComputeRigToHand(
    const VrGameCameraPose& gameCamera,
    const VrVector3&        weaponPosition,
    const VrGameCameraPose& hand);

  /**
   * \brief Turns the hand holding the weapon towards the supporting hand
   *
   * With both hands on the weapon, the line between the hands decides
   * where it points. The hand is turned by the smallest rotation that
   * brings the point where the game animates the supporting hand onto the
   * line towards the other controller, so that the hand's roll is kept.
   *
   * \param [in] hand Pose of the hand holding the weapon
   * \param [in] support Offset from \p hand to where the supporting hand
   *    holds the weapon, with the weapon held by \p hand alone
   * \param [in] target Offset from \p hand to the supporting hand
   * \returns \p hand turned, or \p hand unchanged if either offset is
   *    zero or the two point in nearly the same or opposite directions
   */
  VrGameCameraPose vrComputeTwoHandedPose(
    const VrGameCameraPose& hand,
    const VrVector3&        support,
    const VrVector3&        target);

  /**
   * \brief Heading and pitch of a direction in the game's convention
   *
   * Heading 0 faces north (+Y) and grows clockwise, pitch is positive
   * downwards, both in radians, like the player's angles.
   *
   * \param [in] direction Direction in the game world, need not be unit length
   */
  VrHeadAngles vrComputeAimAngles(const VrVector3& direction);

}
