#include <cmath>

#include "vr_game_space.h"

namespace dxvk {

  VrPose vrMakeRecenterReference(const VrPose& head) {
    // The head looks along -Z, project that direction onto the floor plane
    VrVector3 forward = vrRotate(head.orientation, { 0.0f, 0.0f, -1.0f });
    float yaw = std::atan2(-forward.x, -forward.z);

    VrPose reference;
    reference.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yaw);
    reference.position    = head.position;
    return reference;
  }


  VrPose vrComputeEyeInReference(const VrPose& reference, const VrPose& eyeInTracking) {
    return vrCompose(vrInverse(reference), eyeInTracking);
  }


  VrGameCameraPose vrComputeEyeCameraPose(
    const VrGameCameraPose& base,
    const VrPose&           eyeInReference,
          float             unitsPerMetre) {
    // OpenXR axes (+X right, +Y up, -Z forward) expressed in the base camera frame
    auto toGame = [&base] (const VrVector3& v) {
      return base.right * v.x + base.up * v.y + base.forward * -v.z;
    };

    VrGameCameraPose result;
    result.right    = toGame(vrRotate(eyeInReference.orientation, { 1.0f, 0.0f, 0.0f }));
    result.up       = toGame(vrRotate(eyeInReference.orientation, { 0.0f, 1.0f, 0.0f }));
    result.forward  = toGame(vrRotate(eyeInReference.orientation, { 0.0f, 0.0f, -1.0f }));
    result.position = base.position + toGame(eyeInReference.position) * unitsPerMetre;
    return result;
  }


  VrGameFrustum vrComputeGameFrustum(const VrFov& fov) {
    VrGameFrustum frustum;
    frustum.left   = std::tan(fov.angleLeft);
    frustum.right  = std::tan(fov.angleRight);
    frustum.top    = std::tan(fov.angleUp);
    frustum.bottom = std::tan(fov.angleDown);
    return frustum;
  }

}
