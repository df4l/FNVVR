#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Shading rates of a fragment shading rate image texel
   *
   * The values of VK_KHR_fragment_shading_rate: the base-2 logarithm of
   * the fragment's width in bits 2 and 3, of its height in bits 0 and 1.
   */
  enum class VrShadingRate : uint8_t {
    /// Every pixel is shaded
    Full    = 0x0,
    /// One shade for 2x2 pixels
    Half    = 0x5,
    /// One shade for 4x4 pixels
    Quarter = 0xA,
  };

  /**
   * \brief Why eye-tracked foveation can or cannot be used
   */
  enum class VrFoveationSupport : uint32_t {
    Available,
    /// The GPU or its driver has no shading rate attachments
    NoGpuSupport,
    /// The headset gives no gaze
    NoEyeTracking,
  };

  /**
   * \brief How far from the gaze the world keeps its detail
   *
   * Angles are measured from the gaze on the view's plane (as tangents),
   * which matches the angle around the centre of the view and narrows a
   * little towards its edges.
   */
  struct VrFoveationZones {
    /// Every pixel is shaded within this angle, in degrees
    float fullDegrees = 20.0f;
    /// One shade per 2x2 pixels up to this angle, one per 4x4 beyond
    float halfDegrees = 35.0f;
  };

  /**
   * \brief Position on an eye's shading rate image, in texels
   */
  struct VrGazeTexel {
    float x = 0.0f;
    float y = 0.0f;
  };

  /**
   * \brief Finds where a gaze falls on an eye's shading rate image
   *
   * \param [in] fov The eye's field of view, the one its image is drawn with
   * \param [in] gaze Where the eye looks
   * \param [in] target Size of the eye's render target, in pixels
   * \param [in] texelSize Pixels covered by a texel of the image, per side
   * \param [out] texel The gaze's position, from the image's top left corner
   * \returns \c false if the gaze is outside the view
   */
  bool vrLocateGazeTexel(
    const VrFov&              fov,
    const VrGaze&             gaze,
          VrExtent            target,
          uint32_t            texelSize,
          VrGazeTexel&        texel);

  /**
   * \brief Whether a map made for a field of view still fits another one
   *
   * Some headsets (the Steam Frame) give a slightly different projection
   * nearly every frame. A map is kept while every edge of the view stays
   * within a quarter of a texel of the one it was made for: the zones then
   * move by less than that, which nobody sees.
   *
   * \param [in] built The field of view the map was made for
   * \param [in] current The eye's field of view now
   * \param [in] target Size of the eye's render target, in pixels
   * \param [in] texelSize Pixels covered by a texel, per side
   */
  bool vrFoveationMapFits(
    const VrFov&              built,
    const VrFov&              current,
          VrExtent            target,
          uint32_t            texelSize);

  /**
   * \brief Shading rates around a gaze, for one eye
   *
   * Holds an image twice the size of the eye's shading rate image in each
   * direction, with the gaze at its centre. The eye's image for a gaze
   * anywhere in the view is a window of it, which the GPU copies: nothing
   * is computed per texel once the map exists.
   *
   * A texel takes the rate of the nearest point of its square, so that a
   * texel the full-rate zone reaches into is shaded at full rate.
   */
  class VrFoveationMap {

  public:

    /**
     * \param [in] fov The eye's field of view
     * \param [in] target Size of the eye's render target, in pixels
     * \param [in] texelSize Pixels covered by a texel, per side
     * \param [in] zones Angles of the zones around the gaze
     */
    VrFoveationMap(
      const VrFov&              fov,
            VrExtent            target,
            uint32_t            texelSize,
      const VrFoveationZones&   zones);

    /**
     * \brief Size of the eye's shading rate image, in texels
     */
    VrExtent imageSize() const {
      return m_imageSize;
    }

    /**
     * \brief Texels per row of the map, twice the image's width
     */
    uint32_t rowLength() const {
      return 2u * m_imageSize.width;
    }

    /**
     * \brief Rates of the map, row by row from the top
     */
    const std::vector<uint8_t>& texels() const {
      return m_texels;
    }

    /**
     * \brief Rate of a texel of the map
     */
    VrShadingRate rate(uint32_t x, uint32_t y) const {
      return VrShadingRate(m_texels[size_t(y) * rowLength() + x]);
    }

    /**
     * \brief Where the eye's image starts in the map for a gaze
     *
     * \param [in] gaze The gaze's position on the eye's image
     * \returns Index of the window's top left texel in texels()
     */
    size_t windowOffset(const VrGazeTexel& gaze) const;

  private:

    VrExtent             m_imageSize;
    std::vector<uint8_t> m_texels;

  };

  /**
   * \brief Keeps the last gaze through short losses of the eye tracker
   *
   * A blink loses the eyes for a few frames, during which nothing is seen:
   * the last gaze stays until the loss lasts longer than a blink.
   */
  class VrGazeHold {

  public:

    /// How long the last gaze is kept, in seconds
    static constexpr float HoldSeconds = 0.5f;

    /**
     * \brief Takes the tracker's result for a frame
     *
     * \param [in] gaze The gaze of both eyes, or \c nullptr if the tracker
     *    gave none
     * \param [in] seconds Time since the previous frame
     * \returns The gaze to use, or \c nullptr if there is none
     */
    const std::array<VrGaze, VrEyeCount>* update(
      const std::array<VrGaze, VrEyeCount>* gaze,
            float                           seconds);

    /**
     * \brief Whether the gaze returned last was a held one
     */
    bool holding() const {
      return m_hasGaze && m_lostSeconds > 0.0f;
    }

    void reset();

  private:

    std::array<VrGaze, VrEyeCount> m_gaze = { };
    bool  m_hasGaze     = false;
    float m_lostSeconds = 0.0f;

  };

}
