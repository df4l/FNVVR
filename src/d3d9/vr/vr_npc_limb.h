#pragma once

#include <cstdint>

#include "vr_grab.h"

namespace dxvk {

  /// Most bodies of a ragdoll whose limbs the hands can loosen
  constexpr uint32_t VrRagdollMaxBodies = 64;

  /// Share of the drive towards the animation a body on the way of the hand's
  /// pull keeps (hkaKeyFrameHierarchyUtility bone weights, findings/grab.md
  /// §19.11): none on a limb that only follows the hand, half on a body other
  /// limbs hang from or that hangs from the limb held, so that the rest of
  /// the actor keeps its pose.
  constexpr float VrLimbFreeWeight = 0.0f;
  constexpr float VrLimbSoftWeight = 0.5f;

  /// Seconds a limb let go of takes to give itself back to the animation
  constexpr float VrLimbReturnTime = 0.4f;

  /**
   * \brief Lowers the drive weights of the bodies a held limb moves
   *
   * Keeps the lowest weight, for a second hand on the same ragdoll. Keyframed
   * bodies keep theirs: without a drive they would stop following the animation.
   * \param [in] count Number of bodies
   * \param [in] parents Parent of each body, out of range for none
   * \param [in] dynamic Whether each body is simulated
   * \param [in] held The body held
   * \param [in,out] weights Drive weight of each body
   */
  void vrLimbLoosen(
          uint32_t              count,
    const uint16_t*             parents,
    const bool*                 dynamic,
          uint32_t              held,
          float*                weights);

  /**
   * \brief Weight of a body while a limb goes back to its animation
   *
   * \param [in] weight Weight it had in the hand
   * \param [in] back Share of the way back, 0 to 1
   */
  float vrLimbReturnWeight(float weight, float back);

  /**
   * \brief The spring of a hand on a living limb
   *
   * Stiffer than on an object, which may lag: the limb is seen in the hand.
   * \param [in] spring The spring of a held object
   */
  VrGrabSpringConstants vrLimbSpring(const VrGrabSpringConstants& spring);

  /**
   * \brief Maximum relative force of the spring on a limb
   *
   * The spring's limit is an acceleration of the body held, while it also
   * has to carry what the limb drags along.
   * \param [in] fullAcceleration The game's maximum relative force
   * \param [in] branchMass Mass of the bodies the pull moves
   * \param [in] limbMass Mass of the body held
   */
  float vrLimbMaxAcceleration(float fullAcceleration, float branchMass, float limbMass);

}
