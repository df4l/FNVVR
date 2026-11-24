#pragma once

#include <cstdint>
#include <vector>

namespace dxvk {

  /**
   * \brief A box along the world axes, in game units
   */
  struct VrOcclusionBox {
    float min[3] = { };
    float max[3] = { };

    bool contains(const VrOcclusionBox& other) const {
      for (uint32_t i = 0; i < 3; i++) {
        if (other.min[i] < min[i] || other.max[i] > max[i])
          return false;
      }

      return true;
    }
  };

  /**
   * \brief The view an eye's world was drawn with
   *
   * Matrices as the game's renderer keeps them (D3DMATRIX, row vectors):
   * the view is relative to \c posAdjust, which the drawn positions are
   * relative to too.
   */
  struct VrOcclusionView {
    float posAdjust[3] = { };
    float view[16]     = { };
    float proj[16]     = { };
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;
    float minZ   = 0.0f;
    float maxZ   = 1.0f;
  };

  /**
   * \brief A pre-transformed vertex (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
   */
  struct VrOcclusionVertex {
    float    x, y, z, rhw;
    uint32_t colour;
  };

  /// Vertices of a box's twelve triangles
  constexpr uint32_t VrOcclusionBoxVertices = 36;

  /**
   * \brief Projects a box's faces for an occlusion query
   *
   * Depths are clamped to the viewport's range, which can only bring a box
   * nearer to the eye or keep it in front of the cleared depth: a box can
   * only be found more visible, never less.
   *
   * \param [in] box The box
   * \param [in] view The eye's view
   * \param [out] vertices Its triangles
   * \returns \c false if a corner lies behind the eye or too close to it,
   *    the box cannot be tested
   */
  bool vrProjectOcclusionBox(const VrOcclusionBox& box, const VrOcclusionView& view,
    VrOcclusionVertex (&vertices)[VrOcclusionBoxVertices]);

  /**
   * \brief Whether the depth of a projection grows with distance
   *
   * \c false for a reversed depth range (LOD Flicker Fix), whose depth test
   * keeps greater values.
   */
  bool vrDepthGrowsWithDistance(const VrOcclusionView& view);

  /**
   * \brief Pixels per game unit at one unit from the eye, the smaller axis
   */
  float vrOcclusionFocal(const VrOcclusionView& view);

  /**
   * \brief Distance from a point to a box, 0 inside it
   */
  float vrDistanceToBox(const float* point, const VrOcclusionBox& box);

  /**
   * \brief Grows a box so it reaches past its edges by some pixels
   *
   * The margin is the one the pixels take at the box's farthest corner, so
   * every part of the box grows by at least that much on screen.
   *
   * \param [in] box The box
   * \param [in] eye The eye's position
   * \param [in] pixels Pixels to reach past the edges
   * \param [in] focal From vrOcclusionFocal
   */
  VrOcclusionBox vrGrowOcclusionBox(const VrOcclusionBox& box, const float* eye, float pixels, float focal);

  /**
   * \brief Pixels a move of the eye can slide an occluder's edge
   *
   * An answer is a frame old when it is used: an object stepping out from
   * behind an edge as the eye moves sideways shows a frame before its own
   * test says so. The boxes of covered objects grow by how far the move
   * slides an edge at \c revealDepth units, so they show a frame early.
   *
   * \param [in] focal From vrOcclusionFocal
   * \param [in] move Distance the eye moved since the last frame, in units
   * \param [in] revealDepth Distance of the edges considered, in units
   * \param [in] maxPixels Most pixels a box grows by
   */
  float vrRevealPixels(float focal, float move, float revealDepth, float maxPixels);

  /**
   * \brief Angle between two rotations (row-major 3x3), in degrees
   */
  float vrRotationDegrees(const float* a, const float* b);

  /**
   * \brief How soon a candidate needs a new occlusion test
   */
  enum class VrOcclusionUrgency : uint8_t {
    /// Its last answers found it covered: tested every frame, so that it
    /// comes back the frame after it shows a pixel
    Covered = 0,
    /// Never tested
    New     = 1,
    /// Found visible and due for a test again
    Visible = 2,
    /// Found visible recently
    NotDue  = 3,
  };

  /**
   * \brief Says how soon a candidate needs a test
   *
   * \param [in] answered It has an answer
   * \param [in] covered Its newest answers drew no pixels
   * \param [in] sinceTest Frames since its last test
   * \param [in] visibleRetest Frames between two tests of a visible one
   */
  VrOcclusionUrgency vrOcclusionUrgency(bool answered, bool covered, uint32_t sinceTest, uint32_t visibleRetest);

  /**
   * \brief A candidate waiting for a test
   */
  struct VrOcclusionTurn {
    VrOcclusionUrgency urgency;
    uint32_t           sinceTest;
    uint32_t           index;
  };

  /**
   * \brief Keeps the candidates a frame's budget can test
   *
   * Covered first, then new, then visible ones; among equals the one that
   * waited longest.
   *
   * \param [in,out] turns The candidates; on return the first ones, at most
   *    \c budget, are the ones to test, in no particular order
   * \param [in] budget Tests the frame may issue
   */
  void vrSelectOcclusionTurns(std::vector<VrOcclusionTurn>& turns, uint32_t budget);

}
