#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "vr_scope_lens.h"

namespace dxvk {

  namespace {

    constexpr float Pi = 3.14159265f;

    std::string lowerCase(std::string text) {
      std::transform(text.begin(), text.end(), text.begin(),
        [] (unsigned char c) { return char(std::tolower(c)); });
      return text;
    }

    bool endsWith(const std::string& text, const char* end) {
      size_t length = std::strlen(end);
      return text.size() >= length && text.compare(text.size() - length, length, end) == 0;
    }

    // Reads a string of the model's file ending at an offset: its length
    // comes just before it
    std::string stringEndingAt(const uint8_t* data, size_t end) {
      constexpr size_t MaxLength = 260;

      for (size_t length = 5; length <= MaxLength && length + sizeof(uint32_t) <= end; length++) {
        uint32_t stored;
        std::memcpy(&stored, data + end - length - sizeof(stored), sizeof(stored));

        if (stored == length)
          return std::string(reinterpret_cast<const char*>(data + end - length), length);
      }

      return std::string();
    }

    // The eye's distance the scope is designed for: the dark ring of the
    // reticle then lies at the eyepiece's rim. About 13 cm, as a real scope's
    // eye relief, and as near as a headset lets a scope come in practice.
    constexpr float EyeRelief = 9.0f;

    // Radius of the dark ring of the game's reticles, as a part of the
    // texture's half side (findings/optics.md §6)
    constexpr float ReticleRingRadius = 0.88f;

    // A lens further than this is too small in the eye to be worth a render
    constexpr float MaxImageDistance = 70.0f;

    // Where the rings lie, as a part of the rim: closer together towards the
    // rim, where the glass changes
    constexpr std::array<float, VrScopeLensRings> RingRadii = {
      0.2f, 0.4f, 0.6f, 0.72f, 0.82f, 0.9f, 0.95f, 0.98f, 1.0f,
    };

    float smoothstep(float from, float to, float value) {
      float t = std::min(std::max((value - from) / (to - from), 0.0f), 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    uint32_t colourByte(float value) {
      return uint32_t(std::min(std::max(value, 0.0f), 1.0f) * 255.0f + 0.5f);
    }

    uint32_t packColour(float r, float g, float b, float a) {
      return colourByte(a) << 24 | colourByte(r) << 16 | colourByte(g) << 8 | colourByte(b);
    }

    // A point of the rim's shape, from its angle: a circle, or a rectangle
    // reached along the same directions
    void rimPoint(const VrScopeLens& lens, float angle, float& across, float& up) {
      float cosine = std::cos(angle);
      float sine   = std::sin(angle);

      if (lens.rectangular) {
        float largest = std::max(std::abs(cosine), std::abs(sine));
        cosine /= largest;
        sine   /= largest;
      }

      across = lens.halfWidth * cosine;
      up     = lens.halfHeight * sine;
    }

    VrScopeLensVertex lensVertex(
      const VrScopeLens&       lens,
      const VrGameCameraPose&  eye,
            float              depth,
            float              radial,
            float              across,
            float              up) {
      VrVector3 point = lens.eyepiece.position
        + lens.eyepiece.right * (radial * across)
        + lens.eyepiece.up    * (radial * up);

      VrVector3 fromEye = point - eye.position;

      VrScopeLensVertex vertex;
      vertex.x = vrDot(fromEye, eye.right);
      vertex.y = vrDot(fromEye, eye.up);
      vertex.z = vrDot(fromEye, eye.forward);
      vertex.u = 0.5f + 0.5f * vrDot(fromEye, lens.eyepiece.right) / (depth * lens.apparentTangent);
      vertex.v = 0.5f - 0.5f * vrDot(fromEye, lens.eyepiece.up) / (depth * lens.apparentTangent);

      VrScopeGlass glass = vrComputeScopeGlass(lens, radial, fromEye * (1.0f / vrLength(fromEye)));

      // The device multiplies these by four for night vision, see
      // VrPainter::drawLens: its image is amplified and green
      float red   = glass.transmission * glass.coverage;
      float green = red;
      float blue  = red;

      if (lens.nightVision) {
        red   *= 0.08f;
        green *= 0.6f;
        blue  *= 0.08f;
      }

      vertex.transmitted = packColour(red, green, blue, glass.coverage);
      vertex.reflected   = packColour(
        glass.reflection[0] * glass.coverage,
        glass.reflection[1] * glass.coverage,
        glass.reflection[2] * glass.coverage, 0.0f);
      return vertex;
    }

    VrScopeLensIndices makeIndices() {
      VrScopeLensIndices indices = { };
      uint32_t next = 0;

      for (uint32_t i = 0; i < VrScopeLensSegments; i++) {
        uint32_t j = (i + 1) % VrScopeLensSegments;

        indices[next++] = 0;
        indices[next++] = uint16_t(1 + i);
        indices[next++] = uint16_t(1 + j);

        for (uint32_t ring = 0; ring + 1 < VrScopeLensRings; ring++) {
          auto inner = [ring] (uint32_t segment) { return uint16_t(1 + ring * VrScopeLensSegments + segment); };
          auto outer = [ring] (uint32_t segment) { return uint16_t(1 + (ring + 1) * VrScopeLensSegments + segment); };

          indices[next++] = inner(i);
          indices[next++] = outer(i);
          indices[next++] = outer(j);
          indices[next++] = inner(i);
          indices[next++] = outer(j);
          indices[next++] = inner(j);
        }
      }

      return indices;
    }

  }


  float vrComputeScopeMagnification(float sightFovDegrees, float defaultFovDegrees) {
    return std::tan(0.5f * defaultFovDegrees * Pi / 180.0f)
         / std::tan(0.5f * sightFovDegrees * Pi / 180.0f);
  }


  float vrComputeScopeApparentTangent(float halfWidth, float halfHeight) {
    return std::max(halfWidth, halfHeight) / (EyeRelief * ReticleRingRadius);
  }


  VrScopeArea vrComputeScopeArea(uint32_t width, uint32_t height, uint32_t resolution) {
    constexpr uint32_t MinSide = 64;

    // Even, so that the square is centred on a target of even size
    uint32_t side = std::min(std::max(resolution, MinSide), std::min(width, height)) & ~1u;

    VrScopeArea area;
    area.left   = (width - side) / 2;
    area.top    = (height - side) / 2;
    area.right  = area.left + side;
    area.bottom = area.top + side;
    return area;
  }


  VrFov vrComputeScopeRenderFov(float magnification, float apparentTangent,
      uint32_t width, uint32_t height, const VrScopeArea& area) {
    float perPixel   = 2.0f * apparentTangent / (magnification * float(area.right - area.left));
    float halfWidth  = 0.5f * perPixel * float(width);
    float halfHeight = 0.5f * perPixel * float(height);

    VrFov fov;
    fov.angleLeft  = -std::atan(halfWidth);
    fov.angleRight =  std::atan(halfWidth);
    fov.angleUp    =  std::atan(halfHeight);
    fov.angleDown  = -std::atan(halfHeight);
    return fov;
  }


  std::string vrFindScopeReticleTexture(const uint8_t* data, size_t size) {
    constexpr size_t ExtensionSize = 4;

    for (size_t end = ExtensionSize; end <= size; end++) {
      if (std::tolower(data[end - 1]) != 's' || std::tolower(data[end - 2]) != 'd'
       || std::tolower(data[end - 3]) != 'd' || data[end - 4] != '.')
        continue;

      std::string path = stringEndingAt(data, end);
      std::string name = lowerCase(path);

      if (!path.empty() && !endsWith(name, "\\scope01.dds") && !endsWith(name, "\\scope03_n.dds"))
        return path;
    }

    return std::string();
  }


  VrGameCameraPose vrComputeScopeCamera(const VrScopeLens& lens) {
    VrGameCameraPose camera = lens.eyepiece;
    camera.position = lens.objective;
    return camera;
  }


  bool vrScopeLensShowsImage(const VrScopeLens& lens, const VrGameCameraPose& eye, const VrFov& fov) {
    VrVector3 fromEye = lens.eyepiece.position - eye.position;

    float depth = vrDot(fromEye, lens.eyepiece.forward);

    if (depth < 0.01f || depth > MaxImageDistance)
      return false;

    // The lens in the eye's view: its nearest side to the view against the
    // view's edges, from its furthest distance, which needs no care behind the eye
    float reach    = std::max(lens.halfWidth, lens.halfHeight);
    float furthest = vrDot(fromEye, eye.forward) + reach;

    if (furthest <= 0.0f)
      return false;

    float across = vrDot(fromEye, eye.right);
    float upward = vrDot(fromEye, eye.up);

    if ((across - reach) / furthest > std::tan(fov.angleRight) || (across + reach) / furthest < std::tan(fov.angleLeft)
     || (upward - reach) / furthest > std::tan(fov.angleUp)    || (upward + reach) / furthest < std::tan(fov.angleDown))
      return false;

    // Where the eyepiece opens in the image: this box against the image's edge
    float centreAcross = vrDot(fromEye, lens.eyepiece.right) / depth;
    float centreUp     = vrDot(fromEye, lens.eyepiece.up) / depth;

    return std::abs(centreAcross) - lens.halfWidth / depth < lens.apparentTangent
        && std::abs(centreUp) - lens.halfHeight / depth < lens.apparentTangent;
  }


  VrScopeGlass vrComputeScopeGlass(const VrScopeLens& lens, float radial, const VrVector3& view) {
    constexpr float Transmission = 0.94f;
    constexpr float RimShade     = 0.75f;

    // A thin coating's tint, more of it towards the rim
    constexpr float CoatingColour[3] = { 0.030f, 0.026f, 0.046f };
    constexpr float SkyColour[3]     = { 0.55f, 0.62f, 0.72f };

    constexpr float FresnelScale   = 0.45f;
    constexpr float HighlightPower = 24.0f;
    constexpr float HighlightScale = 0.25f;

    VrScopeGlass glass;

    glass.coverage = 1.0f - smoothstep(0.98f, 1.0f, radial);

    float rim = smoothstep(0.82f, 1.0f, radial);
    glass.transmission = Transmission * (1.0f - RimShade * rim * rim);

    // Fresnel: glass seen at a grazing angle mirrors the sky
    float facing  = std::max(vrDot(view, lens.eyepiece.forward), 0.0f);
    float fresnel = FresnelScale * std::pow(1.0f - facing, 5.0f);

    // A soft light above and behind the player, mirrored in the glass: it
    // moves over the lens as the head or the weapon moves
    constexpr VrVector3 WorldUp = { 0.0f, 0.0f, 1.0f };

    VrVector3 light     = WorldUp * 0.8f - lens.eyepiece.forward * 0.6f;
    VrVector3 reflected = view - lens.eyepiece.forward * (2.0f * vrDot(view, lens.eyepiece.forward));

    float alignment = std::max(vrDot(reflected, light), 0.0f);
    float highlight = HighlightScale * std::pow(alignment, HighlightPower);

    float sheen = 0.6f + 0.4f * radial * radial;

    for (uint32_t i = 0; i < 3; i++)
      glass.reflection[i] = CoatingColour[i] * sheen + SkyColour[i] * (fresnel + highlight);

    return glass;
  }


  bool vrComputeScopeLensMesh(
    const VrScopeLens&       lens,
    const VrGameCameraPose&  eye,
          VrScopeLensMesh&   mesh) {
    // Along the axis the whole lens is at the same distance from the eye
    constexpr float MinDepth = 0.01f;

    float depth = vrDot(lens.eyepiece.position - eye.position, lens.eyepiece.forward);

    if (depth < MinDepth)
      return false;

    mesh[0] = lensVertex(lens, eye, depth, 0.0f, 0.0f, 0.0f);

    for (uint32_t segment = 0; segment < VrScopeLensSegments; segment++) {
      float across, up;
      rimPoint(lens, 2.0f * Pi * float(segment) / float(VrScopeLensSegments), across, up);

      for (uint32_t ring = 0; ring < VrScopeLensRings; ring++)
        mesh[1 + ring * VrScopeLensSegments + segment] = lensVertex(lens, eye, depth, RingRadii[ring], across, up);
    }

    return true;
  }


  const VrScopeLensIndices& vrScopeLensIndices() {
    static const VrScopeLensIndices indices = makeIndices();
    return indices;
  }


  bool vrComputeScopeLensBounds(const VrScopeLensMesh& mesh, const VrFov& fov, VrScopeLensBounds& bounds) {
    // A pixel or two around the lens, for its filtering
    constexpr float Margin  = 0.003f;
    constexpr float MinDepth = 0.05f;

    float left  = std::tan(fov.angleLeft);
    float right = std::tan(fov.angleRight);
    float up    = std::tan(fov.angleUp);
    float down  = std::tan(fov.angleDown);

    VrScopeLensBounds box;
    box.left  = box.top    =  2.0f;
    box.right = box.bottom = -2.0f;

    // The rim alone encloses the lens
    for (uint32_t segment = 0; segment < VrScopeLensSegments; segment++) {
      const VrScopeLensVertex& vertex = mesh[1 + (VrScopeLensRings - 1) * VrScopeLensSegments + segment];

      if (vertex.z < MinDepth) {
        bounds = VrScopeLensBounds();
        return true;
      }

      float x = (vertex.x / vertex.z - left) / (right - left);
      float y = (up - vertex.y / vertex.z) / (up - down);

      box.left   = std::min(box.left, x);
      box.right  = std::max(box.right, x);
      box.top    = std::min(box.top, y);
      box.bottom = std::max(box.bottom, y);
    }

    bounds.left   = std::max(box.left - Margin, 0.0f);
    bounds.top    = std::max(box.top - Margin, 0.0f);
    bounds.right  = std::min(box.right + Margin, 1.0f);
    bounds.bottom = std::min(box.bottom + Margin, 1.0f);

    return bounds.left < bounds.right && bounds.top < bounds.bottom;
  }


  std::array<float, 16> vrComputeEyeProjection(const VrFov& fov, float nearDistance, float farDistance) {
    float left   = std::tan(fov.angleLeft);
    float right  = std::tan(fov.angleRight);
    float up     = std::tan(fov.angleUp);
    float down   = std::tan(fov.angleDown);
    float depth  = farDistance / (farDistance - nearDistance);

    return {
      2.0f / (right - left),            0.0f,                       0.0f,                  0.0f,
      0.0f,                             2.0f / (up - down),         0.0f,                  0.0f,
      -(right + left) / (right - left), -(up + down) / (up - down), depth,                 1.0f,
      0.0f,                             0.0f,                       -nearDistance * depth, 0.0f,
    };
  }

}
