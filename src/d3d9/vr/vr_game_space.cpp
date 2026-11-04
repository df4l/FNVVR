#include <algorithm>
#include <cmath>

#include "vr_game_space.h"

namespace dxvk {

  VrPose vrMakeRecenterReference(const VrPose& head) {
    VrPose reference;
    reference.orientation = vrHeading(head.orientation);
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


  VrVector3 vrComputeGamePointInReference(
    const VrGameCameraPose& base,
    const VrVector3&        point,
          float             unitsPerMetre) {
    // The base camera's axes are the reference's +X, +Y and -Z
    VrVector3 offset = (point - base.position) * (1.0f / unitsPerMetre);
    return { vrDot(offset, base.right), vrDot(offset, base.up), -vrDot(offset, base.forward) };
  }


  VrHeadAngles vrComputeHeadAngles(const VrPose& headInReference) {
    // Forward is -Z, right is +X and up is +Y in the reference frame
    VrVector3 forward = vrRotate(headInReference.orientation, { 0.0f, 0.0f, -1.0f });

    VrHeadAngles angles;
    angles.pitch = -std::asin(std::clamp(forward.y, -1.0f, 1.0f));

    if (std::abs(forward.x) + std::abs(forward.z) > 1e-6f)
      angles.yaw = std::atan2(forward.x, -forward.z);

    return angles;
  }


  VrGameCameraPose vrComputeBodyCameraPose(float heading, const VrVector3& position) {
    // Heading 0 faces north (+Y) and grows clockwise, towards east (+X)
    float s = std::sin(heading);
    float c = std::cos(heading);

    VrGameCameraPose pose;
    pose.forward  = { s, c, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { c, -s, 0.0f };
    pose.position = position;
    return pose;
  }


  float vrComputeCameraHeading(const VrGameCameraPose& pose) {
    return std::atan2(pose.forward.x, pose.forward.y);
  }


  VrVector3 vrComputeHeadViewPosition(const VrVector3& head, const float* rotation, float scale) {
    // From the head bone, at the base of the skull, to between the eyes
    constexpr float EyesAbove   = 6.0f;
    constexpr float EyesInFront = 8.0f;

    VrVector3 top   = { rotation[0], rotation[3], rotation[6] };
    VrVector3 front = { rotation[1], rotation[4], rotation[7] };

    return head + (top * EyesAbove + front * EyesInFront) * scale;
  }


  bool vrComputeHeadHeading(const float* rotation, float& heading) {
    constexpr float MinLevel = 0.2f;

    // Column 1 of the bone's rotation is where the face looks
    float x = rotation[1];
    float y = rotation[4];

    if (x * x + y * y < MinLevel * MinLevel)
      return false;

    heading = std::atan2(x, y);
    return true;
  }


  void VrViewHeading::start(float source, float view) {
    m_source = source;
    keep(view);
  }


  float VrViewHeading::update(float source, float softness, float dt) {
    constexpr float FullTurn = 6.2831853f;

    float left = softness > 0.0f ? std::exp(-dt / softness) : 0.0f;
    m_source += std::remainder(source - m_source, FullTurn) * (1.0f - left);
    return m_source + m_offset;
  }


  void VrViewHeading::keep(float view) {
    m_offset = view - m_source;
  }


  VrGameCameraPose VrCameraBlend::update(const VrGameCameraPose& target, bool changed, float dt) {
    // Time for what is left of the difference to fall to about a third
    constexpr float Softness    = 0.15f;
    // In game units, about three metres
    constexpr float MaxDistance = 200.0f;
    constexpr float Settled     = 1e-3f;

    if (changed && m_hasLast) {
      m_position = m_last.position - target.position;
      m_heading  = std::remainder(vrComputeCameraHeading(m_last) - vrComputeCameraHeading(target), 6.2831853f);

      if (vrLength(m_position) > MaxDistance) {
        m_position = { 0.0f, 0.0f, 0.0f };
        m_heading  = 0.0f;
      }
    }

    float left = std::exp(-dt / Softness);
    m_position = m_position * left;
    m_heading *= left;

    if (vrLength(m_position) < Settled && std::abs(m_heading) < Settled) {
      m_position = { 0.0f, 0.0f, 0.0f };
      m_heading  = 0.0f;
    }

    // Clockwise seen from above, as vrTurnAroundPivot
    float s = std::sin(m_heading);
    float c = std::cos(m_heading);

    auto turn = [s, c] (const VrVector3& v) {
      return VrVector3 { v.x * c + v.y * s, v.y * c - v.x * s, v.z };
    };

    VrGameCameraPose result;
    result.forward  = turn(target.forward);
    result.up       = turn(target.up);
    result.right    = turn(target.right);
    result.position = target.position + m_position;

    m_last    = result;
    m_hasLast = true;
    return result;
  }


  VrGameCameraPose vrComputeVatsCameraPose(const VrGameCameraPose& game, float headYaw) {
    // Looking straight up or down, the camera's up tells the heading
    VrVector3 facing = std::abs(game.forward.x) + std::abs(game.forward.y) > 1e-3f
      ? game.forward
      : game.up * (game.forward.z > 0.0f ? -1.0f : 1.0f);

    return vrComputeBodyCameraPose(std::atan2(facing.x, facing.y) - headYaw, game.position);
  }


  VrVector3 vrTurnAroundPivot(const VrVector3& point, const VrVector3& pivot, float headingChange) {
    float s = std::sin(headingChange);
    float c = std::cos(headingChange);

    float x = point.x - pivot.x;
    float y = point.y - pivot.y;

    // Clockwise seen from above: north (+Y) turns towards east (+X)
    return { pivot.x + x * c + y * s, pivot.y - x * s + y * c, point.z };
  }


  float vrWrapHeading(float angle) {
    constexpr float FullTurn = 6.2831853f;

    float wrapped = std::fmod(angle, FullTurn);

    if (wrapped < 0.0f)
      wrapped += FullTurn;

    // Rounding can bring a small negative angle up to exactly a full turn
    return wrapped < FullTurn ? wrapped : 0.0f;
  }


  VrGameFrustum vrComputeGameFrustum(const VrFov& fov) {
    VrGameFrustum frustum;
    frustum.left   = std::tan(fov.angleLeft);
    frustum.right  = std::tan(fov.angleRight);
    frustum.top    = std::tan(fov.angleUp);
    frustum.bottom = std::tan(fov.angleDown);
    return frustum;
  }


  VrGameCameraPose vrComputeMenuSceneCameraPose(
    const VrPose&           anchor,
    const VrPose&           eye,
    const VrVector3&        target,
          float             unitsPerMetre) {
    // The anchor's right, up and back axes are the menu's +Z, +Y and -X
    auto toMenu = [] (const VrVector3& v) {
      return VrVector3 { -v.z, v.y, v.x };
    };

    VrPose local = vrCompose(vrInverse(anchor), eye);

    VrGameCameraPose pose;
    pose.forward  = toMenu(vrRotate(local.orientation, VrVector3 { 0.0f, 0.0f, -1.0f }));
    pose.up       = toMenu(vrRotate(local.orientation, VrVector3 { 0.0f, 1.0f,  0.0f }));
    pose.right    = toMenu(vrRotate(local.orientation, VrVector3 { 1.0f, 0.0f,  0.0f }));
    pose.position = target + toMenu(local.position) * unitsPerMetre;
    return pose;
  }


  VrVector3 vrComputeMenuScenePointInRoom(
    const VrPose&           anchor,
    const VrVector3&        target,
          float             unitsPerMetre,
    const VrVector3&        point) {
    VrVector3 offset = (point - target) * (1.0f / unitsPerMetre);
    return vrTransformPoint(anchor, VrVector3 { offset.z, offset.y, -offset.x });
  }


  VrVector3 vrComputeMenuSceneDirectionInRoom(
    const VrPose&           anchor,
    const VrVector3&        direction) {
    // The menu's +Z, +Y and -X are the anchor's right, up and back axes
    return vrRotate(anchor.orientation, VrVector3 { direction.z, direction.y, -direction.x });
  }


  VrFov vrComputeCullingFov(const VrFov& left, const VrFov& right, float margin) {
    // Left and down are negative angles
    VrFov fov;
    fov.angleLeft  = std::max(std::min(left.angleLeft,  right.angleLeft)  - margin, -VrMaxCullingAngle);
    fov.angleRight = std::min(std::max(left.angleRight, right.angleRight) + margin,  VrMaxCullingAngle);
    fov.angleUp    = std::min(std::max(left.angleUp,    right.angleUp)    + margin,  VrMaxCullingAngle);
    fov.angleDown  = std::max(std::min(left.angleDown,  right.angleDown)  - margin, -VrMaxCullingAngle);
    return fov;
  }


  VrExtent vrComputeGameResolution(const VrExtent& eye, float minAspect) {
    // The tolerance keeps an exact ratio such as 1920 * 4/3 from being
    // rounded up by the float error of minAspect
    uint32_t minWidth = uint32_t(std::ceil(double(eye.height) * double(minAspect) - 0.01));

    VrExtent result;
    result.width  = std::max(eye.width, minWidth);
    result.height = eye.height;
    result.width += result.width & 1;
    return result;
  }

}
