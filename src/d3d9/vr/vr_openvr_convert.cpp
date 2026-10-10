#include <cmath>

#include "vr_math.h"
#include "vr_openvr_convert.h"

namespace dxvk {

  namespace {

    // Indexed by VrAction. Must match actions.json.
    constexpr const char* ActionPaths[VrActionCount] = {
      "/actions/game/in/attack",
      "/actions/game/in/aim",
      "/actions/game/in/activate",
      "/actions/game/in/jump",
      "/actions/game/in/reload",
      "/actions/game/in/sneak",
      "/actions/game/in/pipboy",
      "/actions/game/in/vats",
      "/actions/game/in/pause",
      "/actions/game/in/grab",
      "/actions/menu/in/select",
      "/actions/menu/in/back",
      "/actions/menu/in/alternate",
      "/actions/menu/in/option",
      "/actions/menu/in/previous",
      "/actions/menu/in/next",
    };

  }


  VrPose vrPoseFromMatrix34(const float m[3][4]) {
    VrQuaternion q;
    float trace = m[0][0] + m[1][1] + m[2][2];

    if (trace > 0.0f) {
      float s = std::sqrt(trace + 1.0f) * 2.0f;
      q.w = 0.25f * s;
      q.x = (m[2][1] - m[1][2]) / s;
      q.y = (m[0][2] - m[2][0]) / s;
      q.z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
      float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
      q.w = (m[2][1] - m[1][2]) / s;
      q.x = 0.25f * s;
      q.y = (m[0][1] + m[1][0]) / s;
      q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
      float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
      q.w = (m[0][2] - m[2][0]) / s;
      q.x = (m[0][1] + m[1][0]) / s;
      q.y = 0.25f * s;
      q.z = (m[1][2] + m[2][1]) / s;
    } else {
      float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
      q.w = (m[1][0] - m[0][1]) / s;
      q.x = (m[0][2] + m[2][0]) / s;
      q.y = (m[1][2] + m[2][1]) / s;
      q.z = 0.25f * s;
    }

    VrPose pose;
    pose.orientation = vrNormalize(q);
    pose.position    = { m[0][3], m[1][3], m[2][3] };
    return pose;
  }


  void vrPoseToMatrix34(const VrPose& pose, float m[3][4]) {
    const VrQuaternion q = vrNormalize(pose.orientation);

    // Column c is the image of the c-th basis vector
    const VrVector3 columns[3] = {
      vrRotate(q, { 1.0f, 0.0f, 0.0f }),
      vrRotate(q, { 0.0f, 1.0f, 0.0f }),
      vrRotate(q, { 0.0f, 0.0f, 1.0f }),
    };

    for (uint32_t c = 0; c < 3; c++) {
      m[0][c] = columns[c].x;
      m[1][c] = columns[c].y;
      m[2][c] = columns[c].z;
    }

    m[0][3] = pose.position.x;
    m[1][3] = pose.position.y;
    m[2][3] = pose.position.z;
  }


  VrFov vrFovFromProjectionRaw(float left, float right, float top, float bottom) {
    VrFov fov;
    fov.angleLeft  = std::atan(left);
    fov.angleRight = std::atan(right);
    fov.angleUp    = std::atan(-top);
    fov.angleDown  = std::atan(-bottom);
    return fov;
  }


  const char* vrOpenVrActionPath(VrAction action) {
    return ActionPaths[uint32_t(action)];
  }


  VrInputContext vrOpenVrActionContext(VrAction action) {
    return uint32_t(action) >= uint32_t(VrAction::MenuSelect)
      ? VrInputContext::Menu
      : VrInputContext::Game;
  }


  const char* vrOpenVrStickPath(VrInputContext context, bool turn) {
    if (context == VrInputContext::Menu)
      return turn ? "/actions/menu/in/secondary" : "/actions/menu/in/navigate";

    return turn ? "/actions/game/in/turn" : "/actions/game/in/move";
  }


  const char* vrOpenVrHapticPath(VrHand hand) {
    return hand == VrHand::Left
      ? "/actions/game/out/haptic_left"
      : "/actions/game/out/haptic_right";
  }

}
