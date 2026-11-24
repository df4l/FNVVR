#include "vr_math.h"
#include "vr_tree_leaves_rules.h"

namespace dxvk {

  namespace {

    // Below this length a direction is taken as zero
    constexpr float MinLength = 1.0e-4f;

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return VrVector3 {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
    }

  }


  bool vrLeafCardAxesFacing(
    const VrVector3&            eye,
    const VrVector3&            center,
    const VrVector3&            cameraRight,
          VrLeafCardAxes&       axes) {
    VrVector3 toLeaves = center - eye;
    float distance = vrLength(toLeaves);

    if (distance < MinLength)
      return false;

    VrVector3 direction = toLeaves * (1.0f / distance);
    VrVector3 right = cross(direction, VrVector3 { 0.0f, 0.0f, 1.0f });
    float rightLength = vrLength(right);

    if (rightLength < MinLength) {
      // Straight above or below: keep the camera's heading, square to the view
      right = cameraRight - direction * vrDot(cameraRight, direction);
      rightLength = vrLength(right);

      if (rightLength < MinLength)
        return false;
    }

    axes.right = right * (1.0f / rightLength);
    axes.up    = cross(axes.right, direction);
    return true;
  }

}
