#include <algorithm>
#include <cmath>

#include "vr_render_scale_size.h"

namespace dxvk {

  namespace {

    constexpr uint32_t Alignment = 8;
    constexpr uint32_t MinSide   = 64;
    constexpr uint32_t MaxShift  = 2;

    uint32_t scaleSide(uint32_t side, float scale) {
      uint32_t scaled = uint32_t(std::lround(float(side) * scale / float(Alignment))) * Alignment;
      return std::min(side, std::max(scaled, MinSide));
    }

  }


  VrExtent vrScaledWorldExtent(VrExtent screen, float scale) {
    scale = std::clamp(scale, VrMinRenderScale, 1.0f);

    if (scale >= 1.0f)
      return screen;

    return VrExtent { scaleSide(screen.width, scale), scaleSide(screen.height, scale) };
  }


  bool vrScaleScreenTexture(VrExtent screen, VrExtent scaled, uint32_t& width, uint32_t& height) {
    if (scaled.width == screen.width && scaled.height == screen.height)
      return false;

    for (uint32_t shift = 0; shift <= MaxShift; shift++) {
      if (width == (screen.width >> shift) && height == (screen.height >> shift)) {
        width  = std::max(scaled.width  >> shift, 1u);
        height = std::max(scaled.height >> shift, 1u);
        return true;
      }
    }

    return false;
  }

}
