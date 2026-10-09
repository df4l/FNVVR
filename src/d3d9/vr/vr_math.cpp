#include <algorithm>
#include <cmath>

#include "vr_math.h"

namespace dxvk {

  VrVector3 operator + (const VrVector3& a, const VrVector3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
  }


  VrVector3 operator - (const VrVector3& a, const VrVector3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
  }


  VrVector3 operator * (const VrVector3& v, float s) {
    return { v.x * s, v.y * s, v.z * s };
  }


  float vrDot(const VrVector3& a, const VrVector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }


  float vrLength(const VrVector3& v) {
    return std::sqrt(vrDot(v, v));
  }


  VrQuaternion vrNormalize(const VrQuaternion& q) {
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);

    if (len <= 0.0f)
      return VrQuaternion();

    float inv = 1.0f / len;
    return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
  }


  VrQuaternion vrConjugate(const VrQuaternion& q) {
    return { -q.x, -q.y, -q.z, q.w };
  }


  VrQuaternion operator * (const VrQuaternion& a, const VrQuaternion& b) {
    return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
  }


  VrQuaternion vrQuaternionFromAxisAngle(const VrVector3& axis, float radians) {
    float len = vrLength(axis);

    if (len <= 0.0f)
      return VrQuaternion();

    float s = std::sin(radians * 0.5f) / len;
    return { axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f) };
  }


  VrQuaternion vrSlerp(const VrQuaternion& a, const VrQuaternion& b, float t) {
    float cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    // Take the shortest arc
    VrQuaternion target = b;

    if (cosine < 0.0f) {
      cosine = -cosine;
      target = { -b.x, -b.y, -b.z, -b.w };
    }

    float wa, wb;

    if (cosine > 0.9995f) {
      // Nearly parallel, a normalized lerp is accurate enough
      wa = 1.0f - t;
      wb = t;
    } else {
      float angle = std::acos(cosine);
      float sine  = std::sin(angle);
      wa = std::sin((1.0f - t) * angle) / sine;
      wb = std::sin(t * angle) / sine;
    }

    return vrNormalize({
      a.x * wa + target.x * wb,
      a.y * wa + target.y * wb,
      a.z * wa + target.z * wb,
      a.w * wa + target.w * wb });
  }


  VrVector3 vrRotate(const VrQuaternion& q, const VrVector3& v) {
    VrQuaternion p = { v.x, v.y, v.z, 0.0f };
    VrQuaternion r = q * p * vrConjugate(q);
    return { r.x, r.y, r.z };
  }


  VrQuaternion vrHeading(const VrQuaternion& q) {
    VrVector3 forward = vrRotate(q, { 0.0f, 0.0f, -1.0f });
    float yaw = std::atan2(-forward.x, -forward.z);
    return vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yaw);
  }


  VrPose vrComputePanelPose(const VrPose& head, float distance) {
    VrPose panel;
    panel.orientation = vrHeading(head.orientation);
    panel.position    = head.position + vrRotate(panel.orientation, { 0.0f, 0.0f, -distance });
    return panel;
  }


  VrPose vrCompose(const VrPose& parent, const VrPose& child) {
    VrPose result;
    result.orientation = vrNormalize(parent.orientation * child.orientation);
    result.position    = parent.position + vrRotate(parent.orientation, child.position);
    return result;
  }


  VrPose vrInverse(const VrPose& pose) {
    VrPose result;
    result.orientation = vrConjugate(pose.orientation);
    result.position    = vrRotate(result.orientation, pose.position) * -1.0f;
    return result;
  }


  VrPose vrLerp(const VrPose& a, const VrPose& b, float t) {
    VrPose result;
    result.orientation = vrSlerp(a.orientation, b.orientation, t);
    result.position    = a.position + (b.position - a.position) * t;
    return result;
  }


  VrVector3 vrTransformPoint(const VrPose& pose, const VrVector3& point) {
    return pose.position + vrRotate(pose.orientation, point);
  }


  Matrix4 vrPoseToMatrix(const VrPose& pose) {
    const VrQuaternion q = vrNormalize(pose.orientation);

    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    // Matrix4 stores columns, so each row of data is one basis vector
    return Matrix4(
      Vector4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f),
      Vector4(2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f),
      Vector4(2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f),
      Vector4(pose.position.x, pose.position.y, pose.position.z, 1.0f));
  }


  Matrix4 vrComputeViewMatrix(const VrPose& eyeInHead, const VrPose& headInWorld) {
    return vrPoseToMatrix(vrInverse(vrCompose(headInWorld, eyeInHead)));
  }


  Matrix4 vrComputeProjectionMatrix(const VrFov& fov, float nearZ, float farZ) {
    const float tanLeft  = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanUp    = std::tan(fov.angleUp);
    const float tanDown  = std::tan(fov.angleDown);

    const float width  = tanRight - tanLeft;
    const float height = tanUp - tanDown;
    const float range  = farZ - nearZ;

    return Matrix4(
      Vector4(2.0f / width, 0.0f, 0.0f, 0.0f),
      Vector4(0.0f, 2.0f / height, 0.0f, 0.0f),
      Vector4((tanRight + tanLeft) / width, (tanUp + tanDown) / height, -farZ / range, -1.0f),
      Vector4(0.0f, 0.0f, -farZ * nearZ / range, 0.0f));
  }


  VrFov vrUnionFov(const VrFov& a, const VrFov& b) {
    const float horizontal = std::max({ -std::tan(a.angleLeft), std::tan(a.angleRight),
                                        -std::tan(b.angleLeft), std::tan(b.angleRight) });
    const float vertical   = std::max({ -std::tan(a.angleDown), std::tan(a.angleUp),
                                        -std::tan(b.angleDown), std::tan(b.angleUp) });

    VrFov result;
    result.angleLeft  = -std::atan(horizontal);
    result.angleRight =  std::atan(horizontal);
    result.angleDown  = -std::atan(vertical);
    result.angleUp    =  std::atan(vertical);
    return result;
  }


  VrTextureBounds vrComputeTextureBounds(const VrFov& eye, const VrFov& unionFov) {
    const float unionLeft  = std::tan(unionFov.angleLeft);
    const float unionWidth = std::tan(unionFov.angleRight) - unionLeft;
    const float unionUp    = std::tan(unionFov.angleUp);
    const float unionHeight = unionUp - std::tan(unionFov.angleDown);

    VrTextureBounds bounds;
    bounds.uMin = (std::tan(eye.angleLeft)  - unionLeft) / unionWidth;
    bounds.uMax = (std::tan(eye.angleRight) - unionLeft) / unionWidth;
    bounds.vMin = (unionUp - std::tan(eye.angleUp))   / unionHeight;
    bounds.vMax = (unionUp - std::tan(eye.angleDown)) / unionHeight;
    return bounds;
  }

}
