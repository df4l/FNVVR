#include <algorithm>
#include <cmath>

#include "vr_grab.h"
#include "vr_math.h"
#include "vr_melee.h"

namespace dxvk {

  std::array<VrGrabRay, VrGrabProbeRayCount> vrGrabProbeRays(
    const VrVector3&            centre,
          float                 radius) {
    constexpr float Corner = 0.57735027f;

    constexpr VrVector3 Directions[VrGrabProbeRayCount] = {
      {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
      {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
      {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
      {  Corner,  Corner,  Corner }, { -Corner,  Corner,  Corner },
      {  Corner, -Corner,  Corner }, { -Corner, -Corner,  Corner },
      {  Corner,  Corner, -Corner }, { -Corner,  Corner, -Corner },
      {  Corner, -Corner, -Corner }, { -Corner, -Corner, -Corner },
    };

    std::array<VrGrabRay, VrGrabProbeRayCount> rays;

    for (uint32_t i = 0; i < VrGrabProbeRayCount; i++)
      rays[i] = { centre + Directions[i] * radius, centre };

    return rays;
  }


  bool vrGrabClearOfPlayer(
    const VrVector3&            body,
    const VrVector3&            player,
          float                 distance) {
    float dx = body.x - player.x;
    float dy = body.y - player.y;
    return dx * dx + dy * dy >= distance * distance;
  }


  float vrGrabSpringLagSteps(const VrGrabSpringConstants& spring) {
    if (!(spring.elasticity > 0.0f))
      return 0.0f;

    return (1.0f - spring.objectDamping * (1.0f - spring.damping)) / spring.elasticity;
  }


  float vrGrabMaxAcceleration(
          float                 mass,
          uint32_t              hands,
          float                 fullAcceleration,
          float                 gravity) {
    // Enough to lift the body and still move it a little
    constexpr float GravityMargin = 1.5f;

    float minimum = std::min(GravityMargin * gravity, fullAcceleration);

    if (!(mass > 0.0f))
      return fullAcceleration;

    float acceleration = fullAcceleration * float(hands) * VrGrabLightMass / mass;
    return std::max(std::min(acceleration, fullAcceleration), minimum);
  }


  VrVector3 vrGrabLead(const VrVector3& playerStep, float frames) {
    // Faster than any run: the player was moved, not walking
    constexpr float MaxStep = 1.0f * VrGameUnitsPerMetre;
    constexpr float MaxLead = 0.6f * VrGameUnitsPerMetre;

    float step = vrLength(playerStep);

    if (step > MaxStep || !(frames > 0.0f))
      return VrVector3();

    VrVector3 lead = playerStep * frames;
    float length = step * frames;

    if (length > MaxLead)
      lead = lead * (MaxLead / length);

    return lead;
  }


  VrGrabFrame vrGrabHandFrame(const VrGameCameraPose& hand) {
    return { hand.forward, hand.up, hand.right };
  }


  VrGrabFrame vrGrabFrameToLocal(const VrGrabFrame& parent, const VrGrabFrame& frame) {
    auto toLocal = [&parent] (const VrVector3& axis) {
      return VrVector3 { vrDot(axis, parent.x), vrDot(axis, parent.y), vrDot(axis, parent.z) };
    };

    return { toLocal(frame.x), toLocal(frame.y), toLocal(frame.z) };
  }


  VrGrabFrame vrGrabFrameFromLocal(const VrGrabFrame& parent, const VrGrabFrame& local) {
    auto fromLocal = [&parent] (const VrVector3& axis) {
      return parent.x * axis.x + parent.y * axis.y + parent.z * axis.z;
    };

    return { fromLocal(local.x), fromLocal(local.y), fromLocal(local.z) };
  }


  VrVector3 vrGrabRotationBetween(const VrGrabFrame& from, const VrGrabFrame& to) {
    // m = to · fromᵀ, the rotation from one frame to the other
    const float t[3][3] = { { to.x.x, to.x.y, to.x.z }, { to.y.x, to.y.y, to.y.z }, { to.z.x, to.z.y, to.z.z } };
    const float f[3][3] = { { from.x.x, from.x.y, from.x.z }, { from.y.x, from.y.y, from.y.z }, { from.z.x, from.z.y, from.z.z } };
    float m[3][3];

    for (uint32_t r = 0; r < 3; r++) {
      for (uint32_t c = 0; c < 3; c++)
        m[r][c] = t[0][r] * f[0][c] + t[1][r] * f[1][c] + t[2][r] * f[2][c];
    }

    // Quaternion of the matrix from its largest component
    VrQuaternion q;
    float trace = m[0][0] + m[1][1] + m[2][2];

    if (trace > 0.0f) {
      float s = 2.0f * std::sqrt(trace + 1.0f);
      q = { (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25f * s };
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
      float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
      q = { 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s };
    } else if (m[1][1] > m[2][2]) {
      float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
      q = { (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s };
    } else {
      float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
      q = { (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s };
    }

    // The shortest way round
    if (q.w < 0.0f)
      q = { -q.x, -q.y, -q.z, -q.w };

    VrVector3 v = { q.x, q.y, q.z };
    float sine = vrLength(v);

    if (!(sine > 1e-6f))
      return v * 2.0f;

    return v * (2.0f * std::atan2(sine, q.w) / sine);
  }


  VrVector3 vrGrabTurnStep(
    const VrGrabSpringConstants& spring,
    const VrVector3&            angularVelocity,
    const VrVector3&            error,
          float                 dt,
          float                 maxAcceleration) {
    if (!(dt > 0.0f))
      return VrVector3();

    VrVector3 change = error * (spring.elasticity / dt) - angularVelocity * spring.damping;
    float length = vrLength(change);
    float limit  = maxAcceleration * dt;

    if (length > limit)
      change = change * (limit / length);

    return change;
  }


  VrGrabFrame vrGrabFrameInverse(const VrGrabFrame& local) {
    return {
      { local.x.x, local.y.x, local.z.x },
      { local.x.y, local.y.y, local.z.y },
      { local.x.z, local.y.z, local.z.z },
    };
  }


  namespace {

    // What the physics update a frame earlier leaves out
    constexpr float SettleDistance = 0.03f * VrGameUnitsPerMetre;
    constexpr float SettleAngle    = 0.15f;

    VrVector3 beyond(const VrVector3& v, float settle) {
      float length = vrLength(v);

      if (!(length > settle))
        return VrVector3();

      return v * ((length - settle) / length);
    }

    VrVector3 rotate(const VrVector3& rotation, const VrVector3& v) {
      float angle = vrLength(rotation);

      if (!(angle > 1e-6f))
        return v;

      return vrRotate(vrQuaternionFromAxisAngle(rotation * (1.0f / angle), angle), v);
    }

    VrGrabCorrection lerp(const VrGrabCorrection& a, const VrGrabCorrection& b, float t) {
      auto mix = [t] (const VrVector3& x, const VrVector3& y) { return x + (y - x) * t; };

      VrGrabCorrection result;
      result.rotation    = mix(a.rotation, b.rotation);
      result.translation = mix(a.translation, b.translation);
      result.palm        = mix(a.palm, b.palm);
      result.pivot       = mix(a.pivot, b.pivot);
      return result;
    }

  }


  VrGrabCorrection vrGrabHandCorrection(
    const VrGameCameraPose&     hand,
    const VrVector3&            offset,
    const VrVector3&            point,
    const VrGrabFrame*          onBody) {
    VrGrabCorrection correction;

    if (onBody)
      correction.rotation = beyond(vrGrabRotationBetween(vrGrabHandFrame(hand), *onBody), SettleAngle);

    // The hand turned the same way still holds the point at its offset
    VrVector3 turned = rotate(correction.rotation, vrPointFromHand(hand, offset) - hand.position);
    correction.translation = beyond(point - turned - hand.position, SettleDistance);
    return correction;
  }


  VrGameCameraPose vrGrabCorrectHand(const VrGameCameraPose& hand, const VrGrabCorrection& correction) {
    VrGameCameraPose result;
    result.forward  = rotate(correction.rotation, hand.forward);
    result.up       = rotate(correction.rotation, hand.up);
    result.right    = rotate(correction.rotation, hand.right);
    result.position = hand.position + correction.translation;

    // The palm's turn, from the hand's frame to the world
    VrVector3 palm = result.forward * correction.palm.x + result.up * correction.palm.y
      + result.right * correction.palm.z;

    if (vrLength(palm) > 1e-6f) {
      VrVector3 pivot = vrPointFromHand(result, correction.pivot);
      result.forward  = rotate(palm, result.forward);
      result.up       = rotate(palm, result.up);
      result.right    = rotate(palm, result.right);
      result.position = pivot + rotate(palm, result.position - pivot);
    }

    return result;
  }


  VrGrabCorrection VrGrabHandGlue::update(const VrGrabCorrection* target, float dt) {
    bool holding = target != nullptr;

    if (holding != m_holding) {
      m_from    = m_last;
      m_blend   = 0.0f;
      m_holding = holding;
    }

    m_blend = std::min(m_blend + dt / BlendTime, 1.0f);
    m_last  = lerp(m_from, holding ? *target : VrGrabCorrection(), m_blend);
    return m_last;
  }


  float vrGrabPullSpeed(float mass) {
    constexpr float MinScale = 0.35f;

    if (!(mass > VrGrabLightMass))
      return VrGrabHand::PullSpeed;

    return VrGrabHand::PullSpeed * std::max(std::sqrt(VrGrabLightMass / mass), MinScale);
  }


  VrGrabRequest VrGrabHand::update(const VrGrabHandInput& input) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    if (m_holding) {
      if (!input.hand || !input.grip || !input.free)
        return VrGrabRequest::Release;

      if (m_pulled)
        advancePull(input.dt);

      return VrGrabRequest::None;
    }

    if (!input.hand || !input.grip || !input.free) {
      m_window = 0.0f;
      return VrGrabRequest::None;
    }

    if (pressed)
      m_window = GrabWindow;

    if (m_window <= 0.0f)
      return VrGrabRequest::None;

    m_window -= input.dt;
    return VrGrabRequest::Grab;
  }


  void VrGrabHand::hold(const VrGameCameraPose& hand, const VrVector3& point) {
    m_holding = true;
    m_pulled  = false;
    m_window  = 0.0f;
    m_offset  = vrPointToHand(hand, point);
  }


  void VrGrabHand::pull(const VrGameCameraPose& hand, const VrVector3& point, float speed) {
    hold(hand, point);
    m_pulled    = true;
    m_pullSpeed = speed;
  }


  void VrGrabHand::drop() {
    m_holding = false;
    m_pulled  = false;
    m_window  = 0.0f;
  }


  void VrGrabHand::advancePull(float dt) {
    float distance = vrLength(m_offset);
    float step     = m_pullSpeed * dt;

    if (distance <= step) {
      m_offset = VrVector3();
      m_pulled = false;
      return;
    }

    m_offset = m_offset * ((distance - step) / distance);
  }


  VrVector3 VrGrabHand::target(const VrGameCameraPose& hand) const {
    return vrPointFromHand(hand, m_offset);
  }

}
