#include <algorithm>
#include <cmath>

#include "vr_weapon_aim.h"

namespace dxvk {

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
