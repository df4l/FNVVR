#include <cmath>

#include "vr_scope_lens.h"

namespace dxvk {

  namespace {

    constexpr float Pi = 3.14159265f;

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
    return scale;
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
