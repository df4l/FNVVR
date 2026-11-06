#include <algorithm>
#include <cmath>
#include <utility>

#include "vr_forearm_cut.h"
#include "vr_math.h"

namespace dxvk {

  VrForearmCut vrComputeForearmCut(
    const VrVector3&            elbow,
    const VrVector3&            wrist,
          float                 distance,
          float                 elbowMargin) {
    VrForearmCut cut;
    VrVector3 axis = wrist - elbow;
    float length = vrLength(axis);

    if (!(length > 0.0f))
      return cut;

    cut.normal = axis * (1.0f / length);

    // A forearm shorter than the margin is cut at the wrist
    float limit = std::max(length - elbowMargin, 0.0f);
    cut.point   = wrist - cut.normal * std::clamp(distance, 0.0f, limit);
    cut.gather  = elbow;
    cut.valid   = true;
    return cut;
  }


  float vrDistanceTowardsElbow(
    const VrVector3&            elbow,
    const VrVector3&            wrist,
    const VrVector3&            point) {
    VrVector3 axis = wrist - elbow;
    float length = vrLength(axis);

    if (!(length > 0.0f))
      return 0.0f;

    return vrDot(wrist - point, axis) * (1.0f / length);
  }


  bool vrComputeClipPlane(
    const float                 viewProj[4][4],
    const VrVector3&            origin,
    const VrVector3&            point,
    const VrVector3&            normal,
          std::array<float, 4>& plane) {
    // The world plane in the drawn positions' frame: n.p' + d >= 0
    float world[4] = { normal.x, normal.y, normal.z, vrDot(normal, origin - point) };

    // clip = [p' 1] * M, so the clip-space plane c gives the same test
    // when M * c = world (M taken as a matrix applied to a column)
    double m[4][5];

    for (uint32_t r = 0; r < 4; r++) {
      for (uint32_t c = 0; c < 4; c++)
        m[r][c] = viewProj[r][c];

      m[r][4] = world[r];
    }

    for (uint32_t c = 0; c < 4; c++) {
      uint32_t pivot = c;

      for (uint32_t r = c + 1; r < 4; r++) {
        if (std::fabs(m[r][c]) > std::fabs(m[pivot][c]))
          pivot = r;
      }

      if (std::fabs(m[pivot][c]) < 1e-12)
        return false;

      for (uint32_t k = 0; k < 5; k++)
        std::swap(m[c][k], m[pivot][k]);

      for (uint32_t r = 0; r < 4; r++) {
        if (r == c)
          continue;

        double f = m[r][c] / m[c][c];

        for (uint32_t k = c; k < 5; k++)
          m[r][k] -= f * m[c][k];
      }
    }

    for (uint32_t r = 0; r < 4; r++)
      plane[r] = float(m[r][4] / m[r][r]);

    return true;
  }

}
