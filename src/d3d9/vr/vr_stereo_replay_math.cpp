#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_stereo_replay_math.h"

namespace dxvk {

  namespace {

    using Matrix = std::array<std::array<double, 4>, 4>;

    /// Relative difference allowed between the renderer's values and ours
    constexpr double Tolerance = 1e-3;

    double at(const std::array<float, 16>& m, uint32_t row, uint32_t col) {
      return double(m[row * 4u + col]);
    }

    bool near(double a, double b) {
      return std::abs(a - b) <= Tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
    }

    Matrix multiply(const Matrix& a, const Matrix& b) {
      Matrix result = { };

      for (uint32_t i = 0u; i < 4u; i++) {
        for (uint32_t j = 0u; j < 4u; j++) {
          for (uint32_t k = 0u; k < 4u; k++)
            result[i][j] += a[i][k] * b[k][j];
        }
      }

      return result;
    }

    bool invert(Matrix m, Matrix& inverse) {
      inverse = { };

      for (uint32_t i = 0u; i < 4u; i++)
        inverse[i][i] = 1.0;

      for (uint32_t col = 0u; col < 4u; col++) {
        uint32_t pivot = col;

        for (uint32_t row = col + 1u; row < 4u; row++) {
          if (std::abs(m[row][col]) > std::abs(m[pivot][col]))
            pivot = row;
        }

        if (std::abs(m[pivot][col]) < 1e-12)
          return false;

        std::swap(m[col], m[pivot]);
        std::swap(inverse[col], inverse[pivot]);

        double scale = 1.0 / m[col][col];

        for (uint32_t k = 0u; k < 4u; k++) {
          m[col][k] *= scale;
          inverse[col][k] *= scale;
        }

        for (uint32_t row = 0u; row < 4u; row++) {
          if (row == col)
            continue;

          double factor = m[row][col];

          for (uint32_t k = 0u; k < 4u; k++) {
            m[row][k] -= factor * m[col][k];
            inverse[row][k] -= factor * inverse[col][k];
          }
        }
      }

      return true;
    }

    /// Direct3D's row-vector matrix as a column-vector one
    Matrix transposed(const std::array<float, 16>& m) {
      Matrix result = { };

      for (uint32_t i = 0u; i < 4u; i++) {
        for (uint32_t j = 0u; j < 4u; j++)
          result[i][j] = at(m, j, i);
      }

      return result;
    }

    std::array<VrVector3, 3> axes(const VrGameCameraPose& pose) {
      return { pose.right, pose.up, pose.forward };
    }

    /// Moves view-space positions of one camera into the view space of another
    Matrix viewToView(const VrGameCameraPose& from, const VrGameCameraPose& to) {
      auto fromAxes = axes(from);
      auto toAxes   = axes(to);

      VrVector3 offset = from.position - to.position;

      Matrix result = { };

      for (uint32_t i = 0u; i < 3u; i++) {
        for (uint32_t j = 0u; j < 3u; j++)
          result[i][j] = double(vrDot(toAxes[i], fromAxes[j]));

        result[i][3] = double(vrDot(toAxes[i], offset));
      }

      result[3][3] = 1.0;
      return result;
    }

    struct FrustumTerms {
      float _11, _22, _31, _32;
    };

    FrustumTerms frustumTerms(const VrGameFrustum& frustum) {
      double rml = double(frustum.right) - double(frustum.left);
      double rpl = double(frustum.right) + double(frustum.left);
      double tmb = double(frustum.top) - double(frustum.bottom);
      double tpb = double(frustum.top) + double(frustum.bottom);

      return { float(2.0 / rml), float(2.0 / tmb), float(-rpl / rml), float(-tpb / tmb) };
    }

    /// A projection with its frustum terms replaced by those of a frustum
    std::array<float, 16> withFrustum(std::array<float, 16> projection, const VrGameFrustum& frustum) {
      auto terms = frustumTerms(frustum);
      projection[0] = terms._11;
      projection[5] = terms._22;
      projection[8] = terms._31;
      projection[9] = terms._32;
      return projection;
    }

  }


  std::array<float, 16> vrComputeGameProjection(
    const VrGameFrustum&      frustum,
          float               nearPlane,
          float               farPlane) {
    double inv = 1.0 / (double(farPlane) - double(nearPlane));

    auto m = withFrustum({ }, frustum);
    m[10] = float(inv * double(farPlane));
    m[14] = float(-inv * double(nearPlane) * double(farPlane));
    m[11] = 1.0f;
    return m;
  }


  std::array<float, 16> vrComputeGameView(
    const VrGameCameraPose&   pose) {
    std::array<float, 16> m = { };

    auto columns = axes(pose);

    for (uint32_t col = 0u; col < 3u; col++) {
      m[0 * 4 + col] = columns[col].x;
      m[1 * 4 + col] = columns[col].y;
      m[2 * 4 + col] = columns[col].z;
    }

    m[15] = 1.0f;
    return m;
  }


  bool vrComputeStereoReplayMatrix(
    const VrRendererCamera&   renderer,
    const VrStereoEyeCamera&  left,
    const VrStereoEyeCamera&  right,
          std::array<float, 16>& rows,
          std::string&        mismatch) {
    const auto& projection = renderer.projection;

    // A perspective projection: w is the view depth. The depth terms are
    // _33 = f / (f - n), _43 = -n f / (f - n), or -n / (f - n) and
    // n f / (f - n) with a reversed depth range (LOD Flicker Fix)
    double depthScale  = at(projection, 2, 2);
    double depthOffset = at(projection, 3, 2);

    bool standardDepth = depthScale > 1.0 && depthOffset < 0.0;
    bool reversedDepth = depthScale < 0.0 && depthOffset > 0.0;

    if (!near(at(projection, 2, 3), 1.0) || !near(at(projection, 3, 3), 0.0)
     || (!standardDepth && !reversedDepth)) {
      mismatch = "the renderer's last projection is not a perspective one (_33 "
        + std::to_string(depthScale) + ", _34 " + std::to_string(at(projection, 2, 3))
        + ", _43 " + std::to_string(depthOffset) + ", _44 " + std::to_string(at(projection, 3, 3)) + ")";
      return false;
    }

    // The frustum terms of the left eye, the depth terms of the game
    auto expected = withFrustum(projection, left.frustum);

    for (uint32_t i : { 0u, 5u, 8u, 9u }) {
      if (!near(double(projection[i]), double(expected[i]))) {
        mismatch = "the renderer's last projection is not the left eye's (term " + std::to_string(i)
          + ": " + std::to_string(projection[i]) + ", " + std::to_string(expected[i]) + " expected)";
        return false;
      }
    }

    auto expectedView = vrComputeGameView(left.pose);

    for (uint32_t row = 0u; row < 3u; row++) {
      for (uint32_t col = 0u; col < 3u; col++) {
        if (!near(at(renderer.view, row, col), at(expectedView, row, col))) {
          uint32_t term = row * 4u + col;
          mismatch = "the renderer's last view is not turned as the left eye (term " + std::to_string(term)
            + ": " + std::to_string(renderer.view[term]) + ", " + std::to_string(expectedView[term]) + " expected)";
          return false;
        }
      }
    }

    Matrix leftProjection = transposed(projection);
    Matrix leftInverse;

    if (!invert(leftProjection, leftInverse)) {
      mismatch = "the renderer's last projection cannot be inverted";
      return false;
    }

    // The depth terms are the game's own, the same for both eyes
    auto rightTerms = withFrustum(projection, right.frustum);

    Matrix result = multiply(transposed(rightTerms),
      multiply(viewToView(left.pose, right.pose), leftInverse));

    for (uint32_t i = 0u; i < 4u; i++) {
      for (uint32_t j = 0u; j < 4u; j++)
        rows[i * 4u + j] = float(result[i][j]);
    }

    return true;
  }



  bool vrComputeStereoCullingView(
    const VrStereoEyeCamera&  left,
    const VrStereoEyeCamera&  right,
          VrGameCameraPose&   pose,
          VrGameFrustum&      frustum) {
    const VrGameFrustum& l = left.frustum;
    const VrGameFrustum& r = right.frustum;

    if (!(l.left < 0.0f && l.right > 0.0f && l.bottom < 0.0f && l.top > 0.0f)
     || !(r.left < 0.0f && r.right > 0.0f && r.bottom < 0.0f && r.top > 0.0f))
      return false;

    // Angle between the eyes' axes, zero for parallel eyes
    float alignment = std::min(std::min(
      vrDot(left.pose.forward, right.pose.forward),
      vrDot(left.pose.up,      right.pose.up)),
      vrDot(left.pose.right,   right.pose.right));
    double cant = std::acos(std::max(-1.0, std::min(1.0, double(alignment))));

    auto widen = [cant] (float tangent) {
      double angle = std::atan(double(tangent)) + (tangent < 0.0f ? -cant : cant);
      angle = std::max(-1.5, std::min(1.5, angle));
      return float(std::tan(angle));
    };

    frustum.left   = widen(std::min(l.left,   r.left));
    frustum.right  = widen(std::max(l.right,  r.right));
    frustum.top    = widen(std::max(l.top,    r.top));
    frustum.bottom = widen(std::min(l.bottom, r.bottom));

    // The right eye in the left eye's axes
    VrVector3 offset = right.pose.position - left.pose.position;
    double dx = vrDot(offset, left.pose.right);
    double dy = vrDot(offset, left.pose.up);
    double dz = vrDot(offset, left.pose.forward);

    // From between the eyes, back far enough that each eye's frustum,
    // starting at the eye, is inside the frustum starting at the apex
    double ax = 0.5 * dx;
    double ay = 0.5 * dy;
    double back = 0.0;

    struct Eye { double x, y, z; };
    const Eye eyes[2] = { { 0.0, 0.0, 0.0 }, { dx, dy, dz } };

    for (const Eye& eye : eyes) {
      double x = eye.x - ax;
      double y = eye.y - ay;

      back = std::max(back, x / double(frustum.right)  - eye.z);
      back = std::max(back, x / double(frustum.left)   - eye.z);
      back = std::max(back, y / double(frustum.top)    - eye.z);
      back = std::max(back, y / double(frustum.bottom) - eye.z);
    }

    pose = left.pose;
    pose.position = left.pose.position
      + left.pose.right   * float(ax)
      + left.pose.up      * float(ay)
      - left.pose.forward * float(back);
    return true;
  }

}
