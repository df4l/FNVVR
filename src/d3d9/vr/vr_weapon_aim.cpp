#include <algorithm>
#include <cmath>

#include "vr_weapon_aim.h"

namespace dxvk {

  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return VrVector3 {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
    }

  }


  VrGameRotation operator * (const VrGameRotation& a, const VrGameRotation& b) {
    VrGameRotation result;

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++)
        result.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    }

    return result;
  }


  VrVector3 operator * (const VrGameRotation& r, const VrVector3& v) {
    return VrVector3 {
      r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z,
      r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
      r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z };
  }


  VrGameRotation vrTranspose(const VrGameRotation& r) {
    VrGameRotation result;

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++)
        result.m[i][j] = r.m[j][i];
    }

    return result;
  }


  VrGameRotation vrCameraRotation(const VrGameCameraPose& pose) {
    VrGameRotation result;
    const VrVector3* axes[3] = { &pose.forward, &pose.up, &pose.right };

    for (uint32_t j = 0; j < 3; j++) {
      result.m[0][j] = axes[j]->x;
      result.m[1][j] = axes[j]->y;
      result.m[2][j] = axes[j]->z;
    }

    return result;
  }


  VrGameTransform vrComputeRigToHand(
    const VrGameCameraPose& gameCamera,
    const VrVector3&        weaponPosition,
    const VrGameCameraPose& hand) {
    VrGameTransform result;
    result.rotate    = vrCameraRotation(hand) * vrTranspose(vrCameraRotation(gameCamera));
    result.translate = hand.position - result.rotate * weaponPosition;
    return result;
  }


  VrGameCameraPose vrComputeTwoHandedPose(
    const VrGameCameraPose& hand,
    const VrVector3&        support,
    const VrVector3&        target) {
    float supportLength = vrLength(support);
    float targetLength  = vrLength(target);

    if (supportLength <= 0.0f || targetLength <= 0.0f)
      return hand;

    VrVector3 from = support * (1.0f / supportLength);
    VrVector3 to   = target * (1.0f / targetLength);
    VrVector3 axis = cross(from, to);

    float s = vrLength(axis);
    float c = vrDot(from, to);

    if (s < 1e-4f)
      return hand;

    axis = axis * (1.0f / s);

    // Rodrigues' rotation formula
    auto rotate = [&] (const VrVector3& v) {
      return v * c + cross(axis, v) * s + axis * (vrDot(axis, v) * (1.0f - c));
    };

    VrGameCameraPose result = hand;
    result.forward = rotate(hand.forward);
    result.up      = rotate(hand.up);
    result.right   = rotate(hand.right);
    return result;
  }


  VrGameTransform vrInverse(const VrGameTransform& t) {
    VrGameTransform result;
    result.rotate    = vrTranspose(t.rotate);
    result.translate = (result.rotate * t.translate) * -1.0f;
    return result;
  }


  VrGameRotation vrComputeBoneMirror(
    const VrGameTransform&      bindHand,
    const VrGameTransform&      bindOtherHand) {
    VrVector3 normal = bindOtherHand.translate - bindHand.translate;
    float length = vrLength(normal);

    if (length <= 0.0f)
      return VrGameRotation();

    normal = normal * (1.0f / length);

    // Reflection across the plane halfway between the hands
    VrGameRotation reflection;
    const float n[3] = { normal.x, normal.y, normal.z };

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++)
        reflection.m[i][j] -= 2.0f * n[i] * n[j];
    }

    return vrTranspose(bindHand.rotate) * reflection * bindOtherHand.rotate;
  }


  VrGameTransform vrComputeMirroredHand(
    const VrGameCameraPose&     controller,
    const VrGameTransform&      handBone,
    const VrGameCameraPose&     otherController,
    const VrGameRotation&       boneMirror) {
    // Reflection across the controller's forward-up plane: its right axis,
    // the last column of vrCameraRotation, is flipped
    VrGameRotation flipRight;
    flipRight.m[2][2] = -1.0f;

    VrGameRotation mirror = vrCameraRotation(otherController) * flipRight
                          * vrTranspose(vrCameraRotation(controller));

    VrGameTransform result;
    result.rotate    = mirror * handBone.rotate * boneMirror;
    result.translate = otherController.position + mirror * (handBone.translate - controller.position);
    return result;
  }


  VrHeadAngles vrComputeAimAngles(const VrVector3& direction) {
    VrHeadAngles angles;
    float length = vrLength(direction);

    if (length <= 0.0f)
      return angles;

    angles.pitch = -std::asin(std::clamp(direction.z / length, -1.0f, 1.0f));

    if (std::abs(direction.x) + std::abs(direction.y) > 1e-6f * length)
      angles.yaw = vrWrapHeading(std::atan2(direction.x, direction.y));

    return angles;
  }

}
