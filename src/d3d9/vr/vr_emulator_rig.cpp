#include <algorithm>
#include <cmath>

#include "vr_emulator_rig.h"

namespace dxvk {

  VrEmulatorRig::VrEmulatorRig(const VrEmulatorRigConfig& config)
  : m_config(config) {
    m_position.y = config.standingHeight;
  }


  void VrEmulatorRig::update(const VrEmulatorInput& input, float dt) {
    if (input.recenter) {
      m_position = { 0.0f, m_config.standingHeight, 0.0f };
      m_yaw   = 0.0f;
      m_pitch = 0.0f;
    }

    // Positive mouse movement to the right turns the head to the right,
    // which is a negative rotation around the up axis.
    m_yaw   -= input.mouseDeltaX * m_config.radiansPerPixel;
    m_pitch -= input.mouseDeltaY * m_config.radiansPerPixel;
    m_pitch  = std::clamp(m_pitch, -m_config.maxPitch, m_config.maxPitch);

    const VrQuaternion yawRotation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, m_yaw);
    const VrVector3 right   = vrRotate(yawRotation, { 1.0f, 0.0f, 0.0f });
    const VrVector3 forward = vrRotate(yawRotation, { 0.0f, 0.0f, -1.0f });

    const float step = m_config.moveSpeed * dt;
    m_position = m_position
      + right * (input.moveRight * step)
      + forward * (input.moveForward * step)
      + VrVector3 { 0.0f, input.moveUp * step, 0.0f };
  }


  void VrEmulatorRig::setHeadPose(const VrPose& pose) {
    m_position = pose.position;

    // Forward is -Z. Yaw is the rotation around +Y that maps it onto the
    // pose's horizontal heading, pitch the elevation above the horizon.
    const VrVector3 forward = vrRotate(pose.orientation, { 0.0f, 0.0f, -1.0f });
    m_yaw   = std::atan2(-forward.x, -forward.z);
    m_pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
  }


  VrPose VrEmulatorRig::headPose() const {
    const VrQuaternion yaw   = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, m_yaw);
    const VrQuaternion pitch = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, m_pitch);

    VrPose pose;
    pose.orientation = vrNormalize(yaw * pitch);
    pose.position    = m_position;
    return pose;
  }


  VrPose VrEmulatorRig::handPose(VrHand hand) const {
    const VrVector3& offset = hand == VrHand::Left
      ? m_config.leftHandOffset
      : m_config.rightHandOffset;

    const VrQuaternion yaw = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, m_yaw);

    VrPose pose;
    pose.orientation = yaw;
    pose.position    = m_position + vrRotate(yaw, offset);
    return pose;
  }


  VrControllerState VrEmulatorRig::controllerState(VrHand hand) const {
    VrControllerState state;
    state.isActive = true;
    state.gripPose = handPose(hand);
    state.aimPose  = state.gripPose;
    return state;
  }

}
