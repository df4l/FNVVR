#include <algorithm>
#include <cmath>

#include "vr_foveation_map.h"

namespace dxvk {

  namespace {

    constexpr float RadiansPerDegree = 0.0174532925f;

    struct ViewTangents {
      float left;
      float right;
      float up;
      float down;
    };

    ViewTangents tangents(const VrFov& fov) {
      return ViewTangents {
        std::tan(fov.angleLeft), std::tan(fov.angleRight),
        std::tan(fov.angleUp),   std::tan(fov.angleDown) };
    }

    // Distance from the map's centre line to the nearest edge of a texel
    // column or row, in texels: 0 for the two that touch the centre
    float nearestDistance(uint32_t index, uint32_t centre) {
      if (index + 1u <= centre)
        return float(centre - (index + 1u));

      return float(index - centre);
    }

  }


  bool vrLocateGazeTexel(
    const VrFov&              fov,
    const VrGaze&             gaze,
          VrExtent            target,
          uint32_t            texelSize,
          VrGazeTexel&        texel) {
    ViewTangents view = tangents(fov);

    if (!texelSize || view.right <= view.left || view.up <= view.down)
      return false;

    // Tangents are linear across the image: the image is the view's plane
    float u = (gaze.tanX - view.left) / (view.right - view.left);
    float v = (view.up - gaze.tanY)   / (view.up - view.down);

    if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f))
      return false;

    texel.x = u * float(target.width)  / float(texelSize);
    texel.y = v * float(target.height) / float(texelSize);
    return true;
  }


  VrFoveationMap::VrFoveationMap(
    const VrFov&              fov,
          VrExtent            target,
          uint32_t            texelSize,
    const VrFoveationZones&   zones) {
    m_imageSize.width  = (target.width  + texelSize - 1u) / texelSize;
    m_imageSize.height = (target.height + texelSize - 1u) / texelSize;

    ViewTangents view = tangents(fov);

    // Tangent covered by a texel along each axis
    float texelX = (view.right - view.left) * float(texelSize) / float(target.width);
    float texelY = (view.up - view.down)    * float(texelSize) / float(target.height);

    float full = std::tan(std::clamp(zones.fullDegrees, 0.0f, 89.0f) * RadiansPerDegree);
    float half = std::tan(std::clamp(std::max(zones.halfDegrees, zones.fullDegrees), 0.0f, 89.0f) * RadiansPerDegree);

    uint32_t width  = rowLength();
    uint32_t height = 2u * m_imageSize.height;

    m_texels.resize(size_t(width) * height);

    for (uint32_t y = 0; y < height; y++) {
      float dy = nearestDistance(y, m_imageSize.height) * texelY;

      for (uint32_t x = 0; x < width; x++) {
        float dx = nearestDistance(x, m_imageSize.width) * texelX;
        float distance = std::sqrt(dx * dx + dy * dy);

        VrShadingRate rate = distance <= full ? VrShadingRate::Full
                           : distance <= half ? VrShadingRate::Half
                           : VrShadingRate::Quarter;

        m_texels[size_t(y) * width + x] = uint8_t(rate);
      }
    }
  }


  size_t VrFoveationMap::windowOffset(const VrGazeTexel& gaze) const {
    // The window's texel under the gaze is the map's centre
    float x = std::round(float(m_imageSize.width)  - gaze.x);
    float y = std::round(float(m_imageSize.height) - gaze.y);

    uint32_t left = uint32_t(std::clamp(x, 0.0f, float(m_imageSize.width)));
    uint32_t top  = uint32_t(std::clamp(y, 0.0f, float(m_imageSize.height)));

    return size_t(top) * rowLength() + left;
  }


  const std::array<VrGaze, VrEyeCount>* VrGazeHold::update(
    const std::array<VrGaze, VrEyeCount>* gaze,
          float                           seconds) {
    if (gaze) {
      m_gaze        = *gaze;
      m_hasGaze     = true;
      m_lostSeconds = 0.0f;
      return &m_gaze;
    }

    if (!m_hasGaze)
      return nullptr;

    m_lostSeconds += seconds;

    if (m_lostSeconds > HoldSeconds) {
      m_hasGaze = false;
      return nullptr;
    }

    return &m_gaze;
  }


  void VrGazeHold::reset() {
    m_hasGaze     = false;
    m_lostSeconds = 0.0f;
  }

}
