#include <algorithm>
#include <cmath>

#include "vr_caravan_reach.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return VrVector3 {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
    }


    VrVector3 normalized(const VrVector3& v) {
      float length = vrLength(v);
      return length > 0.0f ? v * (1.0f / length) : v;
    }


    float smooth(float t) {
      t = std::clamp(t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }


    /// Rotation about a pivot taking the direction \c from to \c to
    VrCaravanTransform turnAbout(const VrVector3& pivot, const VrVector3& from, const VrVector3& to) {
      VrVector3 a = normalized(from);
      VrVector3 b = normalized(to);
      VrVector3 axis = cross(a, b);
      float s = vrLength(axis);
      float c = vrDot(a, b);

      VrCaravanTransform turn;

      if (s > 1e-6f) {
        axis = axis * (1.0f / s);

        // Rodrigues' formula
        float t = 1.0f - c;
        const float k[3] = { axis.x, axis.y, axis.z };

        for (uint32_t i = 0; i < 3; i++) {
          for (uint32_t j = 0; j < 3; j++)
            turn.rotate[i][j] = t * k[i] * k[j] + (i == j ? c : 0.0f);
        }

        turn.rotate[0][1] -= s * axis.z;  turn.rotate[1][0] += s * axis.z;
        turn.rotate[0][2] += s * axis.y;  turn.rotate[2][0] -= s * axis.y;
        turn.rotate[1][2] -= s * axis.x;  turn.rotate[2][1] += s * axis.x;
      }

      turn.translate = pivot - vrCaravanTransformPoint(turn, pivot);
      return turn;
    }


    VrQuaternion toQuaternion(const float m[3][3]) {
      VrQuaternion q;
      float trace = m[0][0] + m[1][1] + m[2][2];

      if (trace > 0.0f) {
        float s = 2.0f * std::sqrt(trace + 1.0f);
        q.w = 0.25f * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
      } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
      } else if (m[1][1] > m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m[1][2] + m[2][1]) / s;
      } else {
        float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25f * s;
      }

      return vrNormalize(q);
    }

  }


  VrArmReach vrArmReach(
    const VrVector3&      shoulder,
    const VrVector3&      elbow,
    const VrVector3&      wrist,
    const VrVector3&      target) {
    VrArmReach reach;

    float upper = vrLength(elbow - shoulder);
    float fore  = vrLength(wrist - elbow);

    if (upper <= 0.0f || fore <= 0.0f)
      return reach;

    VrVector3 toTarget = target - shoulder;
    float distance = std::clamp(vrLength(toTarget), std::abs(upper - fore) + 1e-3f, (upper + fore) * 0.999f);

    if (vrLength(toTarget) <= 0.0f)
      return reach;

    VrVector3 along = normalized(toTarget);

    // The elbow keeps the side of the line it bends to in the pose
    VrVector3 bend = (elbow - shoulder) - along * vrDot(elbow - shoulder, along);

    if (vrLength(bend) < 1e-4f * upper)
      bend = cross(along, std::abs(along.z) < 0.9f ? VrVector3 { 0.0f, 0.0f, 1.0f } : VrVector3 { 1.0f, 0.0f, 0.0f });

    bend = normalized(bend);

    float cosine = std::clamp((upper * upper + distance * distance - fore * fore) / (2.0f * upper * distance), -1.0f, 1.0f);
    float sine   = std::sqrt(1.0f - cosine * cosine);
    VrVector3 newElbow = shoulder + along * (upper * cosine) + bend * (upper * sine);

    reach.upper = turnAbout(shoulder, elbow - shoulder, newElbow - shoulder);

    VrVector3 movedWrist = vrCaravanTransformPoint(reach.upper, wrist);
    VrVector3 newWrist   = shoulder + along * distance;
    reach.fore = turnAbout(newElbow, movedWrist - newElbow, newWrist - newElbow);
    return reach;
  }


  VrCaravanTransform vrCaravanBlend(
    const VrCaravanTransform& a,
    const VrCaravanTransform& b,
          float           t) {
    VrQuaternion q = vrSlerp(toQuaternion(a.rotate), toQuaternion(b.rotate), t);

    // The quaternion's rotation matrix
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    VrCaravanTransform blend;
    blend.rotate[0][0] = 1.0f - 2.0f * (yy + zz);
    blend.rotate[0][1] = 2.0f * (xy - wz);
    blend.rotate[0][2] = 2.0f * (xz + wy);
    blend.rotate[1][0] = 2.0f * (xy + wz);
    blend.rotate[1][1] = 1.0f - 2.0f * (xx + zz);
    blend.rotate[1][2] = 2.0f * (yz - wx);
    blend.rotate[2][0] = 2.0f * (xz - wy);
    blend.rotate[2][1] = 2.0f * (yz + wx);
    blend.rotate[2][2] = 1.0f - 2.0f * (xx + yy);

    blend.translate = a.translate + (b.translate - a.translate) * t;
    blend.scale     = a.scale + (b.scale - a.scale) * t;
    return blend;
  }


  VrCaravanReachStep vrCaravanReachStep(float time) {
    VrCaravanReachStep step;

    if (time < 0.0f)
      return step;

    step.done = false;

    if (time < VrCaravanReachTime) {
      step.weight = smooth(time / VrCaravanReachTime);
      return step;
    }

    time -= VrCaravanReachTime;
    step.taken = true;

    if (time < VrCaravanCarryTime) {
      step.weight = 1.0f;
      step.carry  = smooth(time / VrCaravanCarryTime);
      return step;
    }

    time -= VrCaravanCarryTime;
    step.carry = 1.0f;

    if (time < VrCaravanReturnTime) {
      step.weight = 1.0f - smooth(time / VrCaravanReturnTime);
      return step;
    }

    step.done = true;
    return step;
  }


  float vrCaravanPlayerCarry(float time) {
    return smooth(time / VrCaravanPlayerCarryTime);
  }

}
