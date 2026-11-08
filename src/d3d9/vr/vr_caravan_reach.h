#pragma once

#include "vr_caravan_pick.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Rotations that bend a two-bone arm towards a point
   *
   * Both are rigid world transforms: \c upper turns the upper arm and all
   * below it about the shoulder, then \c fore turns the forearm and all
   * below it about the elbow \c upper has moved.
   */
  struct VrArmReach {
    VrCaravanTransform upper;
    VrCaravanTransform fore;
  };

  /**
   * \brief Bends an arm so that its wrist reaches a point
   *
   * The elbow stays on the side it bends to in the pose, and the arm stays
   * as it is when the point is the wrist. A point out of reach is reached
   * for with the arm straight.
   * \param [in] shoulder Position of the upper arm bone
   * \param [in] elbow Position of the forearm bone
   * \param [in] wrist Position of the hand bone
   * \param [in] target Where the wrist goes
   */
  VrArmReach vrArmReach(
    const VrVector3&      shoulder,
    const VrVector3&      elbow,
    const VrVector3&      wrist,
    const VrVector3&      target);

  /**
   * \brief Between two transforms, \c t from 0 (\c a) to 1 (\c b)
   */
  VrCaravanTransform vrCaravanBlend(
    const VrCaravanTransform& a,
    const VrCaravanTransform& b,
          float           t);

  /**
   * \brief Where the challenger's gesture playing a Caravan card stands
   *
   * The free hand goes to the fan, takes the card to the mat, then goes
   * back to the pose.
   */
  struct VrCaravanReachStep {
    /// How far the hand has left the pose for the card, 0 to 1
    float weight = 0.0f;
    /// The card's way from the fan (0) to the mat (1)
    float carry  = 0.0f;
    /// Whether the card has left the fan: it is in the hand or on its way
    bool  taken  = false;
    /// Whether the gesture is over
    bool  done   = true;
  };

  /// Seconds the hand takes to reach the fan, to carry the card, to come back
  constexpr float VrCaravanReachTime  = 0.4f;
  constexpr float VrCaravanCarryTime  = 0.55f;
  constexpr float VrCaravanReturnTime = 0.45f;

  /**
   * \brief The gesture's step at a time since it started
   */
  VrCaravanReachStep vrCaravanReachStep(float time);

}
