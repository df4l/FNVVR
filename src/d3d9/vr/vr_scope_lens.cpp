#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "vr_scope_lens.h"

namespace dxvk {

  namespace {

    constexpr float Pi = 3.14159265f;

    // Half the side of the reticle's quad over half the height of the view at
    // 4:3, as the game draws it (HUDMainMenu::Create3DCamera, the model at
    // 380 units and scale 0.1, the quad's half side 435): the reticle covers
    // the same part of the world as in the game
    constexpr float ReticleOverImageHeight = 1.023f;

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

    VrScopeLensVertex lensVertex(
      const VrScopeLens&       lens,
      const VrGameCameraPose&  eye,
      const VrScopeImageScale& scale,
            float              depth,
      const VrVector3&         point) {
      VrVector3 fromEye = point - eye.position;

      VrScopeLensVertex vertex;
      vertex.x = vrDot(fromEye, eye.right);
      vertex.y = vrDot(fromEye, eye.up);
      vertex.z = vrDot(fromEye, eye.forward);
      vertex.u = 0.5f + 0.5f * scale.x * vrDot(fromEye, lens.eyepiece.right) / depth;
      vertex.v = 0.5f - 0.5f * scale.y * vrDot(fromEye, lens.eyepiece.up) / depth;
      vertex.reticleU = 0.5f + 0.5f * scale.reticle * vrDot(fromEye, lens.eyepiece.right) / depth;
      vertex.reticleV = 0.5f - 0.5f * scale.reticle * vrDot(fromEye, lens.eyepiece.up) / depth;
      return vertex;
    }

  }


  float vrComputeScopeMagnification(float sightFovDegrees) {
    // fDefaultWorldFOV's default
    constexpr float DefaultWorldFov = 75.0f;

    return std::tan(0.5f * DefaultWorldFov * Pi / 180.0f)
         / std::tan(0.5f * sightFovDegrees * Pi / 180.0f);
  }


  VrScopeImageScale vrComputeScopeImageScale(const VrFov& renderFov, float magnification) {
    VrScopeImageScale scale;
    scale.x = 1.0f / (magnification * std::tan(renderFov.angleRight));
    scale.y = 1.0f / (magnification * std::tan(renderFov.angleUp));
    scale.reticle = scale.y / ReticleOverImageHeight;
    return scale;
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
    camera.position = lens.eyepiece.position + lens.eyepiece.forward * lens.tubeLength;
    return camera;
  }


  bool vrComputeScopeLensDisc(
    const VrScopeLens&       lens,
    const VrGameCameraPose&  eye,
    const VrScopeImageScale& scale,
          VrScopeLensDisc&   disc) {
    // Along the axis the whole disc is at the same distance from the eye
    constexpr float MinDepth = 0.01f;

    float depth = vrDot(lens.eyepiece.position - eye.position, lens.eyepiece.forward);

    if (depth < MinDepth)
      return false;

    disc[0] = lensVertex(lens, eye, scale, depth, lens.eyepiece.position);

    for (uint32_t i = 0; i <= VrScopeLensSegments; i++) {
      float angle = 2.0f * Pi * float(i) / float(VrScopeLensSegments);

      disc[i + 1] = lensVertex(lens, eye, scale, depth, lens.eyepiece.position
        + lens.eyepiece.right * (lens.radius * std::cos(angle))
        + lens.eyepiece.up    * (lens.radius * std::sin(angle)));
    }

    return true;
  }


  std::array<float, 16> vrComputeEyeProjection(const VrFov& fov) {
    // In game units: the eyepiece is a few units from the eye
    constexpr float Near = 0.1f;
    constexpr float Far  = 1000.0f;

    float left   = std::tan(fov.angleLeft);
    float right  = std::tan(fov.angleRight);
    float up     = std::tan(fov.angleUp);
    float down   = std::tan(fov.angleDown);
    float depth  = Far / (Far - Near);

    return {
      2.0f / (right - left),           0.0f,                      0.0f,          0.0f,
      0.0f,                            2.0f / (up - down),        0.0f,          0.0f,
      -(right + left) / (right - left), -(up + down) / (up - down), depth,        1.0f,
      0.0f,                            0.0f,                      -Near * depth, 0.0f,
    };
  }

}
