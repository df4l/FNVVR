#pragma once

#include "vr_weapon_aim.h"

namespace dxvk {

  /**
   * \brief Local placement of a node in its parent
   *
   * A point x of the node is at rotate * scale * x + translate in the parent.
   */
  struct VrNodePlacement {
    VrGameRotation rotate;
    VrVector3      translate;
    float          scale = 1.0f;
  };

  /**
   * \brief Returns the point of a line that is closest to a point
   *
   * \param [in] a First point of the line
   * \param [in] b Second point of the line, \c a itself is returned when
   *    both points are the same
   * \param [in] point Point to project
   */
  VrVector3 vrClosestPointOnLine(
    const VrVector3&            a,
    const VrVector3&            b,
    const VrVector3&            point);

  /**
   * \brief Turns a node about an axis of its parent's frame and scales it
   *
   * The node turns about the line through the pivot along the axis, by the
   * right-hand rule, then grows away from the pivot by the factor: the
   * pivot keeps its place in the parent.
   *
   * \param [in] node The node's own placement
   * \param [in] pivot Point of the axis, in the parent's frame
   * \param [in] axis Direction of the axis in the parent's frame, any length
   * \param [in] radians Angle to turn by, 0 to only scale
   * \param [in] factor Scale applied on top of the node's own
   */
  VrNodePlacement vrPlaceAboutAxis(
    const VrNodePlacement&      node,
    const VrVector3&            pivot,
    const VrVector3&            axis,
          float                 radians,
          float                 factor);

  /**
   * \brief Grows the Pip-Boy while it is open and shrinks it back once closed
   *
   * The size moves towards its target at a constant rate, so that it takes
   * \ref GrowTime to go from the game's size to the size picked by the
   * player, back, or to a size picked while it is open.
   */
  class VrPipBoyGrowth {

  public:

    /// Time to grow from the game's size to the picked size, or back, in seconds
    constexpr static float GrowTime = 0.25f;

    /**
     * \brief Moves the size towards its target for one frame
     *
     * \param [in] grown Whether the Pip-Boy is open, or opening
     * \param [in] scale Size picked by the player, as a factor of the game's size
     * \param [in] dt Time since the previous frame, in seconds
     * \returns The size for this frame, as a factor of the game's size
     */
    float update(bool grown, float scale, float dt);

    float factor() const {
      return m_factor;
    }

  private:

    float m_factor = 1.0f;
    float m_target = 1.0f;
    /// Change of size per second towards the target
    float m_rate   = 0.0f;

  };

}
