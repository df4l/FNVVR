#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

#include "vr_scope_reticle.h"

namespace dxvk {

  namespace {

    constexpr char SheetName[] = "dlcanchgaussscope01.dds";

    constexpr uint32_t ReticleSize = 1024;

    // In the units of dlcanchgaussscope01.nif, the view's centre at the origin,
    // y up. Its surround has a hole of this radius.
    constexpr float OpeningRadius = 342.0f;

    // Part of the image's half size where the opening's edge lies, as the dark
    // ring of the game's other reticles
    constexpr float OpeningInImage = 0.88f;

    // The rim's shading, from nothing to black
    constexpr float RimStart = 312.0f;

    constexpr float DotRadius = 2.0f;

    // The grid, one quarter of it in the sheet, its centre at the corner
    constexpr float GridHalf  = 364.12f;
    constexpr float GridU     = 0.502f;
    constexpr float GridUSize = 0.497f;
    constexpr float GridV     = 0.002f;
    constexpr float GridVSize = 0.997f;

    // The dark edge of the view, its centre on the column's left
    constexpr float MaskHalf  = 409.6f;
    constexpr float MaskU     = 0.252f;
    constexpr float MaskUSize = 0.247f;
    constexpr float MaskVSize = 0.49f;

    // The glass's glow, its centre on the column's right
    constexpr float GlowHalf  = 413.4f;
    constexpr float GlowUSize = 0.25f;

    // The numbers beside the grid: "+90" above and "-90" below
    constexpr float NumbersLeft[]   = { 152.9f, -179.6f };
    constexpr float NumbersWidth    = 23.8f;
    constexpr float NumbersNear     = 129.2f;
    constexpr float NumbersFar      = 144.4f;
    constexpr float NumbersU        = 0.507f;
    constexpr float NumbersUSize    = 0.065f;
    constexpr float NumbersPlusV    = 0.008f;
    constexpr float NumbersMinusV   = 0.099f;
    constexpr float NumbersVSize    = 0.083f;

    using Texel = std::array<float, 4>;

    constexpr uint32_t Alpha = 0;
    constexpr uint32_t Red   = 1;
    constexpr uint32_t Green = 2;
    constexpr uint32_t Blue  = 3;

    Texel texelAt(const VrImage& image, int32_t x, int32_t y) {
      uint32_t pixel = image.at(
        uint32_t(std::clamp(x, 0, int32_t(image.width)  - 1)),
        uint32_t(std::clamp(y, 0, int32_t(image.height) - 1)));

      return { float(pixel >> 24 & 0xFF) / 255.0f, float(pixel >> 16 & 0xFF) / 255.0f,
               float(pixel >>  8 & 0xFF) / 255.0f, float(pixel       & 0xFF) / 255.0f };
    }

    Texel sample(const VrImage& image, float u, float v) {
      float x = u * float(image.width)  - 0.5f;
      float y = v * float(image.height) - 0.5f;

      float left = std::floor(x);
      float top  = std::floor(y);
      float fx   = x - left;
      float fy   = y - top;

      Texel a = texelAt(image, int32_t(left),     int32_t(top));
      Texel b = texelAt(image, int32_t(left) + 1, int32_t(top));
      Texel c = texelAt(image, int32_t(left),     int32_t(top) + 1);
      Texel d = texelAt(image, int32_t(left) + 1, int32_t(top) + 1);

      Texel result;

      for (uint32_t i = 0; i < result.size(); i++)
        result[i] = (a[i] * (1.0f - fx) + b[i] * fx) * (1.0f - fy) + (c[i] * (1.0f - fx) + d[i] * fx) * fy;

      return result;
    }

    /**
     * \brief What a point of the reticle does to the view
     *
     * The view is multiplied by \c through, then \c added is added to it.
     */
    struct Layered {
      float through = 1.0f;
      float added[3] = { };
    };

    void numbers(const VrImage& sheet, float x, float y, Layered& point) {
      float height = std::abs(y);

      if (height < NumbersNear || height > NumbersFar)
        return;

      float v = (y > 0.0f ? NumbersPlusV : NumbersMinusV)
        + NumbersVSize * (y > 0.0f ? NumbersFar - height : height - NumbersNear) / (NumbersFar - NumbersNear);

      for (float left : NumbersLeft) {
        if (x >= left && x <= left + NumbersWidth)
          point.through *= sample(sheet, NumbersU + NumbersUSize * (x - left) / NumbersWidth, v)[Green];
      }
    }

    Layered layersAt(const VrImage& sheet, float x, float y) {
      Layered point;

      float radius = std::hypot(x, y);

      if (radius >= OpeningRadius) {
        point.through = 0.0f;
        return point;
      }

      float across = std::abs(x);
      float along  = std::abs(y);

      point.through *= sample(sheet,
        GridU + GridUSize * (1.0f - std::min(across / GridHalf, 1.0f)),
        GridV + GridVSize * (1.0f - std::min(along  / GridHalf, 1.0f)))[Green];

      numbers(sheet, x, y, point);

      if (radius > RimStart)
        point.through *= 1.0f - (radius - RimStart) / (OpeningRadius - RimStart);

      Texel mask = sample(sheet, MaskU + MaskUSize * across / MaskHalf, 0.5f - MaskVSize * y / MaskHalf);
      Texel glow = sample(sheet, GlowUSize * (1.0f - across / GlowHalf), 0.5f + 0.5f * y / GlowHalf);

      point.through *= 1.0f - mask[Alpha];

      for (uint32_t i = 0; i < 3; i++)
        point.added[i] = mask[Red + i] * mask[Alpha] + glow[Red + i] * glow[Alpha];

      if (radius <= DotRadius) {
        point.through  = 0.0f;
        point.added[0] = 1.0f;
        point.added[1] = 0.0f;
        point.added[2] = 0.0f;
      }

      return point;
    }

    uint32_t byteOf(float value) {
      return uint32_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

  }


  bool vrIsReticleSheet(const std::string& texture) {
    constexpr size_t NameSize = sizeof(SheetName) - 1;

    if (texture.size() < NameSize)
      return false;

    return std::equal(texture.end() - NameSize, texture.end(), SheetName, [] (char a, char b) {
      return std::tolower(static_cast<unsigned char>(a)) == b;
    });
  }


  VrImage vrComposeReticleSheet(const VrImage& sheet) {
    VrImage reticle;
    reticle.width  = ReticleSize;
    reticle.height = ReticleSize;
    reticle.pixels.resize(size_t(ReticleSize) * ReticleSize);

    float half = OpeningRadius / OpeningInImage;

    for (uint32_t row = 0; row < ReticleSize; row++) {
      float y = (1.0f - 2.0f * (float(row) + 0.5f) / float(ReticleSize)) * half;

      for (uint32_t column = 0; column < ReticleSize; column++) {
        float x = (2.0f * (float(column) + 0.5f) / float(ReticleSize) - 1.0f) * half;

        Layered point = layersAt(sheet, x, y);

        // The lens only blends with the alpha: a glow over a clear view hides
        // as little of the view as it adds
        float brightest = std::max({ point.added[0], point.added[1], point.added[2] });
        float alpha     = std::max(1.0f - point.through, brightest);
        float scale     = alpha > 0.0f ? 1.0f / alpha : 0.0f;

        reticle.pixels[size_t(row) * ReticleSize + column] = byteOf(alpha) << 24
          | byteOf(point.added[0] * scale) << 16
          | byteOf(point.added[1] * scale) << 8
          | byteOf(point.added[2] * scale);
      }
    }

    return reticle;
  }

}
