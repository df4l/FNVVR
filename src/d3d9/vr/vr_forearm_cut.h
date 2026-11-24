#pragma once

#include <array>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Plane across a forearm where its sleeve is cut
   *
   * The plane is normal to the forearm's axis, which runs from the elbow
   * (the forearm bone) to the wrist (the hand bone). What lies on the
   * hand's side is kept. The sleeve's vertices that belong to other bones
   * are gathered at \c gather, on the side that is cut away.
   */
  struct VrForearmCut {
    VrVector3 point;
    VrVector3 normal;
    VrVector3 gather;
    bool      valid = false;
  };

  /**
   * \brief Places the cut across a forearm
   *
   * \param [in] elbow Position of the forearm bone
   * \param [in] wrist Position of the hand bone
   * \param [in] distance Distance of the cut from the wrist, towards the elbow
   * \param [in] elbowMargin Shortest distance kept between the cut and the
   *    elbow, so that the gathered vertices are always cut away
   * \returns An invalid cut when the elbow and the wrist are the same point
   */
  VrForearmCut vrComputeForearmCut(
    const VrVector3&            elbow,
    const VrVector3&            wrist,
          float                 distance,
          float                 elbowMargin);

  /**
   * \brief How far a point lies from the wrist towards the elbow
   *
   * Measured along the forearm's axis: negative beyond the wrist.
   */
  float vrDistanceTowardsElbow(
    const VrVector3&            elbow,
    const VrVector3&            wrist,
    const VrVector3&            point);

  /**
   * \brief Clip-space coefficients of a world plane, for SetClipPlane
   *
   * With a vertex shader, Direct3D 9 tests clip planes against the clip-space
   * position: a vertex is kept where the dot product of the coefficients
   * with that position is not negative. The game draws positions relative to
   * a draw origin and multiplies them, as row vectors, by its view-projection
   * matrix.
   *
   * \param [in] viewProj Row-major view-projection matrix (D3DMATRIX layout)
   * \param [in] origin World position the drawn positions are relative to
   * \param [in] point Point of the plane, in the world
   * \param [in] normal Normal of the plane, towards the side that is kept
   * \param [out] plane Coefficients of the plane in clip space
   * \returns \c false if the matrix cannot be inverted
   */
  bool vrComputeClipPlane(
    const float                 viewProj[4][4],
    const VrVector3&            origin,
    const VrVector3&            point,
    const VrVector3&            normal,
          std::array<float, 4>& plane);

}
