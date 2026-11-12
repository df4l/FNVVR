#pragma once

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Axes the game's SpeedTree leaf cards are laid along
   *
   * The leaf vertex shader places the corners of each leaf card along these
   * two world directions, see findings/render.md (SpeedTree leaves).
   */
  struct VrLeafCardAxes {
    VrVector3 up;
    VrVector3 right;
  };

  /**
   * \brief Turns leaf cards towards a viewpoint
   *
   * The game lays the cards along the camera's own up and right axes, so in a
   * headset they turn and roll with the head. These axes instead face the
   * cards to the viewpoint's position, with no roll: they only change when
   * the eye moves around the tree, as for an object standing in the world.
   *
   * Game coordinates: Z up, right = direction x up, as for the game's camera.
   *
   * \param [in] eye Position of the viewpoint
   * \param [in] center Centre of the leaves
   * \param [in] cameraRight Camera's right axis, used only when the leaves are
   *    straight above or below the eye, where any heading would do
   * \param [out] axes Axes for the leaf cards
   * \returns \c false if the eye is at the leaves' centre, \p axes unchanged
   */
  bool vrLeafCardAxesFacing(
    const VrVector3&            eye,
    const VrVector3&            center,
    const VrVector3&            cameraRight,
          VrLeafCardAxes&       axes);

}
