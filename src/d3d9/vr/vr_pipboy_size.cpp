#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_pipboy_size.h"

namespace dxvk {

  VrVector3 vrClosestPointOnLine(
    const VrVector3&            a,
    const VrVector3&            b,
    const VrVector3&            point) {
    VrVector3 axis = b - a;
    float lengthSquared = vrDot(axis, axis);

    if (!(lengthSquared > 1e-12f))
      return a;

    return a + axis * (vrDot(point - a, axis) / lengthSquared);
  }


  VrNodePlacement vrPlaceAboutAxis(
    const VrNodePlacement&      node,
    const VrVector3&            pivot,
    const VrVector3&            axis,
          float                 radians,
          float                 factor) {
    // A point x of the node is at R * s * x + t in the parent. Turned by Q
    // and scaled about the pivot it goes to
    // pivot + factor * Q * (R * s * x + t - pivot).
    VrGameRotation turn = vrGameRotationFromQuaternion(vrQuaternionFromAxisAngle(axis, radians));

    VrNodePlacement result;
    result.rotate    = turn * node.rotate;
    result.translate = pivot + (turn * (node.translate - pivot)) * factor;
    result.scale     = node.scale * factor;
    return result;
  }


  float VrPipBoyGrowth::update(bool grown, float scale, float dt) {
    scale = std::max(scale, 1.0f);

    // The rate is set when the target changes, so that every change of
    // size takes the same time
    float target = grown ? scale : 1.0f;

    if (target != m_target) {
      m_target = target;
      m_rate   = std::fabs(target - m_factor) / GrowTime;
    }

    float step = m_rate * std::max(dt, 0.0f);

    if (m_factor < target)
      m_factor = std::min(m_factor + step, target);
    else
      m_factor = std::max(m_factor - step, target);

    return m_factor;
  }

}
