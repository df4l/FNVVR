#include <algorithm>
#include <cmath>

#include "vr_occlusion_rules.h"

namespace dxvk {

  namespace {

    /// Nearest a corner may be to the eye along its view, in units
    constexpr float MinW = 1.0f;

    constexpr float DegreesPerRadian = 57.2957795f;

    // Corner i takes min or max on each axis by its bits (x 1, y 2, z 4)
    constexpr uint8_t BoxTriangles[VrOcclusionBoxVertices] = {
      0, 1, 3,  0, 3, 2,    // z min
      4, 6, 7,  4, 7, 5,    // z max
      0, 4, 5,  0, 5, 1,    // y min
      2, 3, 7,  2, 7, 6,    // y max
      0, 2, 6,  0, 6, 4,    // x min
      1, 5, 7,  1, 7, 3,    // x max
    };

    // Row vector times a D3D matrix
    void transform(const float* v, const float* m, float* out) {
      for (uint32_t c = 0; c < 4; c++)
        out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + v[3] * m[12 + c];
    }

  }


  bool vrProjectOcclusionBox(const VrOcclusionBox& box, const VrOcclusionView& view,
      VrOcclusionVertex (&vertices)[VrOcclusionBoxVertices]) {
    VrOcclusionVertex corners[8];

    float lowZ  = std::min(view.minZ, view.maxZ);
    float highZ = std::max(view.minZ, view.maxZ);

    for (uint32_t i = 0; i < 8; i++) {
      float world[4] = {
        ((i & 1) ? box.max[0] : box.min[0]) - view.posAdjust[0],
        ((i & 2) ? box.max[1] : box.min[1]) - view.posAdjust[1],
        ((i & 4) ? box.max[2] : box.min[2]) - view.posAdjust[2],
        1.0f };

      float eye[4], clip[4];
      transform(world, view.view, eye);
      transform(eye, view.proj, clip);

      if (!(clip[3] >= MinW))
        return false;

      float rhw = 1.0f / clip[3];
      float x   = clip[0] * rhw;
      float y   = clip[1] * rhw;
      float z   = view.minZ + clip[2] * rhw * (view.maxZ - view.minZ);

      corners[i].x      = view.x + (x + 1.0f) * 0.5f * view.width;
      corners[i].y      = view.y + (1.0f - y) * 0.5f * view.height;
      corners[i].z      = std::clamp(z, lowZ, highZ);
      corners[i].rhw    = rhw;
      corners[i].colour = 0xFFFFFFFFu;
    }

    for (uint32_t i = 0; i < VrOcclusionBoxVertices; i++)
      vertices[i] = corners[BoxTriangles[i]];

    return true;
  }


  bool vrDepthGrowsWithDistance(const VrOcclusionView& view) {
    // _43 is -nf/(f-n) for the standard range, nf/(f-n) when reversed
    return view.proj[14] < 0.0f;
  }


  float vrOcclusionFocal(const VrOcclusionView& view) {
    return 0.5f * std::min(std::fabs(view.proj[0]) * view.width, std::fabs(view.proj[5]) * view.height);
  }


  float vrDistanceToBox(const float* point, const VrOcclusionBox& box) {
    float sum = 0.0f;

    for (uint32_t i = 0; i < 3; i++) {
      float d = std::max(std::max(box.min[i] - point[i], point[i] - box.max[i]), 0.0f);
      sum += d * d;
    }

    return std::sqrt(sum);
  }


  VrOcclusionBox vrGrowOcclusionBox(const VrOcclusionBox& box, const float* eye, float pixels, float focal) {
    if (!(pixels > 0.0f) || !(focal > 0.0f))
      return box;

    float far = 0.0f;

    for (uint32_t i = 0; i < 8; i++) {
      float sum = 0.0f;

      for (uint32_t a = 0; a < 3; a++) {
        float d = ((i >> a) & 1 ? box.max[a] : box.min[a]) - eye[a];
        sum += d * d;
      }

      far = std::max(far, sum);
    }

    float margin = pixels * std::sqrt(far) / focal;
    VrOcclusionBox grown = box;

    for (uint32_t a = 0; a < 3; a++) {
      grown.min[a] -= margin;
      grown.max[a] += margin;
    }

    return grown;
  }


  float vrRevealPixels(float focal, float move, float revealDepth, float maxPixels) {
    if (!(revealDepth > 0.0f) || !(move > 0.0f))
      return 0.0f;

    return std::min(focal * move / revealDepth, maxPixels);
  }


  float vrRotationDegrees(const float* a, const float* b) {
    // trace(A^T B) = 1 + 2 cos(angle)
    float trace = 0.0f;

    for (uint32_t i = 0; i < 9; i++)
      trace += a[i] * b[i];

    float c = std::clamp(0.5f * (trace - 1.0f), -1.0f, 1.0f);
    return std::acos(c) * DegreesPerRadian;
  }


  VrOcclusionUrgency vrOcclusionUrgency(bool answered, bool covered, uint32_t sinceTest, uint32_t visibleRetest) {
    if (!answered)
      return VrOcclusionUrgency::New;

    if (covered)
      return VrOcclusionUrgency::Covered;

    return sinceTest >= visibleRetest
      ? VrOcclusionUrgency::Visible
      : VrOcclusionUrgency::NotDue;
  }


  void vrSelectOcclusionTurns(std::vector<VrOcclusionTurn>& turns, uint32_t budget) {
    if (turns.size() <= budget)
      return;

    auto before = [] (const VrOcclusionTurn& a, const VrOcclusionTurn& b) {
      if (a.urgency != b.urgency)
        return a.urgency < b.urgency;

      return a.sinceTest > b.sinceTest;
    };

    std::nth_element(turns.begin(), turns.begin() + budget, turns.end(), before);
    turns.resize(budget);
  }

}
