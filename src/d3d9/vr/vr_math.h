#pragma once

#include "vr_types.h"

#include "../../util/util_matrix.h"

namespace dxvk {

  VrVector3 operator + (const VrVector3& a, const VrVector3& b);
  VrVector3 operator - (const VrVector3& a, const VrVector3& b);
  VrVector3 operator * (const VrVector3& v, float s);

  float vrDot(const VrVector3& a, const VrVector3& b);
  float vrLength(const VrVector3& v);

  VrQuaternion vrNormalize(const VrQuaternion& q);
  VrQuaternion vrConjugate(const VrQuaternion& q);
  VrQuaternion operator * (const VrQuaternion& a, const VrQuaternion& b);

  /**
   * \brief Builds a rotation around the given axis
   *
   * \param [in] axis Rotation axis, does not need to be normalized
   * \param [in] radians Rotation angle
   */
  VrQuaternion vrQuaternionFromAxisAngle(const VrVector3& axis, float radians);

  /**
   * \brief Spherical interpolation along the shortest arc
   */
  VrQuaternion vrSlerp(const VrQuaternion& a, const VrQuaternion& b, float t);

  VrVector3 vrRotate(const VrQuaternion& q, const VrVector3& v);

  /**
   * \brief Composes two poses
   *
   * The result applies \c child first, then \c parent,
   * so that vrTransformPoint(vrCompose(a, b), p) equals
   * vrTransformPoint(a, vrTransformPoint(b, p)).
   */
  VrPose vrCompose(const VrPose& parent, const VrPose& child);
  VrPose vrInverse(const VrPose& pose);
  VrPose vrLerp(const VrPose& a, const VrPose& b, float t);
  VrVector3 vrTransformPoint(const VrPose& pose, const VrVector3& point);

  /**
   * \brief Converts a pose into a rigid-body matrix
   *
   * The matrix maps points from the pose's local space into its parent space.
   */
  Matrix4 vrPoseToMatrix(const VrPose& pose);

  /**
   * \brief Builds a view matrix for an eye
   *
   * \param [in] eyeInHead Eye pose relative to the head
   * \param [in] headInWorld Head pose in the tracking/world space
   * \returns Matrix mapping world-space points into the eye's space
   */
  Matrix4 vrComputeViewMatrix(const VrPose& eyeInHead, const VrPose& headInWorld);

  /**
   * \brief Builds an off-axis projection matrix for a D3D-style clip space
   *
   * Maps view space (right-handed, -Z forward) to clip space with
   * x and y in [-w, w] and depth in [0, w].
   *
   * \param [in] fov Eye field of view
   * \param [in] nearZ Distance to the near plane, positive
   * \param [in] farZ Distance to the far plane, positive
   */
  Matrix4 vrComputeProjectionMatrix(const VrFov& fov, float nearZ, float farZ);

  /**
   * \brief Computes the symmetric field of view covering both eyes
   *
   * The game renders with a symmetric frustum, so the render target must
   * cover the widest extent of either eye in each direction.
   */
  VrFov vrUnionFov(const VrFov& a, const VrFov& b);

  /**
   * \brief Sub-rectangle of a render target, in normalized texture coordinates
   *
   * The vertical axis grows downwards, as in image space.
   */
  struct VrTextureBounds {
    float uMin = 0.0f;
    float uMax = 1.0f;
    float vMin = 0.0f;
    float vMax = 1.0f;
  };

  /**
   * \brief Maps an eye's frustum onto a render target covering the union FOV
   *
   * Returns the part of the union render target that holds the eye's view.
   */
  VrTextureBounds vrComputeTextureBounds(const VrFov& eye, const VrFov& unionFov);

}
