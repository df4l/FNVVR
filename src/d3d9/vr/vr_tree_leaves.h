#pragma once

namespace dxvk {

  /**
   * \brief Keeps SpeedTree leaves still when the head turns
   *
   * Trees and some bushes are SpeedTree models whose leaves are flat cards
   * the vertex shader lays along the camera's up and right axes, set before
   * each leaf draw by SpeedTreeLeafShader::UpdateScaleAdjustment. In a
   * headset the cards then turned and rolled with the head. After the game's
   * function, the axes are replaced with axes facing the leaves to the
   * position of the camera being drawn with (vrLeafCardAxesFacing), so they
   * only turn as the eye moves around them. See findings/render.md.
   *
   * Only used on the game's render thread.
   */
  class VrTreeLeaves {

  public:

    /**
     * \brief Redirects the call that sets the leaf card axes
     * \returns \c false if the executable is not the supported version
     */
    static bool install();

  };

}
