#pragma once

#include <array>
#include <functional>

#include "vr_hand_fingers.h"
#include "vr_types.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  /**
   * \brief The finger bones of a hand of the first-person model, as needed
   *    to place the fingers without the model
   *
   * The finger bones hang below the hand bone in a fixed order (see
   * vrFindFingerBone): each finger's bones are a chain whose first bone's
   * parent is the hand bone. Only their rotations change with the curls
   * (vrPoseFingerBone).
   */
  struct VrFingerRig {
    /// The hand bone in its controller's frame (forward, up and right, as
    /// in vrCameraRotation), game units
    VrGameTransform hand;
    /// The hand bone's world scale
    float scale = 1.0f;
    /// Each finger bone's local translation and scale
    std::array<VrVector3, VrFingerBoneCount> translate = { };
    std::array<float, VrFingerBoneCount>     boneScale = { };
  };

  /**
   * \brief The joints of a finger's three segments and its tip, in the
   *    game world
   */
  using VrFingerJoints = std::array<VrVector3, 4>;

  /**
   * \brief Where a finger's joints are for a curl
   *
   * \param [in] rig The hand's finger bones
   * \param [in] hand The hand
   * \param [in] finger The finger
   * \param [in] curl The finger's curl, 0 open and 1 closed
   * \param [in] controller The hand's controller in the game world
   */
  VrFingerJoints vrFingerJoints(
    const VrFingerRig&          rig,
          VrHand                hand,
          VrFinger              finger,
          float                 curl,
    const VrGameCameraPose&     controller);

  /**
   * \brief Casts a ray in the world
   *
   * \returns \c true if the segment from one point to the other meets
   *    something
   */
  using VrFingerRayFn = std::function<bool (const VrVector3& from, const VrVector3& to)>;

  /**
   * \brief Closes each finger on what the hand holds
   *
   * Each finger closes from open in small steps; at each step its three
   * segments, and the way its tip went since the step before, are cast
   * as rays. The finger stops halfway into the first step that meets
   * something. A finger that meets nothing closes completely.
   *
   * \param [in] rig The hand's finger bones
   * \param [in] hand The hand
   * \param [in] controller The hand's controller in the game world, where
   *    the hand is drawn
   * \param [in] ray Casts a ray in the world
   * \returns How far each finger closes
   */
  VrFingerCurls vrFitFingers(
    const VrFingerRig&          rig,
          VrHand                hand,
    const VrGameCameraPose&     controller,
    const VrFingerRayFn&        ray);

  /**
   * \brief Direction the palm faces, in the game world
   *
   * Square to the knuckles of the index and the little finger and the
   * middle finger's metacarpal, towards the side the fingers close to.
   */
  VrVector3 vrPalmDirection(
    const VrFingerRig&          rig,
          VrHand                hand,
    const VrGameCameraPose&     controller);

  /**
   * \brief Turn that brings the palm towards a point the hand touches
   *
   * \param [in] palm The direction the palm faces, in the game world
   * \param [in] centre The hand's centre
   * \param [in] point The point touched
   * \param [in] maxAngle Largest turn, radians
   * \returns Axis times angle, in the game world; zero when the point is
   *    at the centre
   */
  VrVector3 vrPalmTurn(
    const VrVector3&            palm,
    const VrVector3&            centre,
    const VrVector3&            point,
          float                 maxAngle);

  /**
   * \brief Brings the drawn fingers onto a held object and back
   *
   * The player's own curls are kept up to the curls that fit the object:
   * a finger the player raises stays raised, a closed one rests on the
   * object. Without finger tracking the fingers take the fitted curls.
   * The change blends over a short time when the hand starts or stops
   * holding.
   */
  class VrGrabFingerBlend {

  public:

    /// Time to blend the fingers onto or off an object, in seconds
    constexpr static float BlendTime = 0.1f;

    /**
     * \param [in] tracked The player's curls, or \c nullptr without
     *    finger tracking
     * \param [in] fitted The curls that fit the held object, or
     *    \c nullptr while the hand holds nothing
     * \param [in] dt Time since the previous frame, in seconds
     * \param [out] curls The curls to draw the hand with
     * \returns \c false if the hand is drawn without curls (no tracking
     *    and nothing held)
     */
    bool update(
      const VrFingerCurls*        tracked,
      const VrFingerCurls*        fitted,
            float                 dt,
            VrFingerCurls&        curls);

  private:

    VrFingerCurls m_fitted = { };
    float         m_weight = 0.0f;

  };

}
