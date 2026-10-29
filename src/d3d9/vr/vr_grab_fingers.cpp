#include <algorithm>
#include <cmath>

#include "vr_grab_fingers.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    // Steps of a finger closing from open to a fist
    constexpr uint32_t FitSteps = 8;

    // The tip lies past the last joint, by this share of the last segment
    constexpr float TipLength = 0.8f;

    // The bones of each finger's chain, in VrFindFingerBone's order: its
    // first bone hangs from the hand bone. The thumb has no metacarpal.
    struct FingerChain {
      uint32_t first;
      uint32_t count;
    };

    constexpr FingerChain Chains[VrFingerCount] = {
      {  0, 3 },
      {  3, 4 },
      {  7, 4 },
      { 11, 4 },
      { 15, 4 },
    };

    struct BoneFrame {
      VrGameRotation rotate;
      VrVector3      translate;
      float          scale = 1.0f;
    };

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    BoneFrame handBone(const VrFingerRig& rig, const VrGameCameraPose& controller) {
      VrGameTransform hand = vrComputeHeldTransform(controller, rig.hand);
      return { hand.rotate, hand.translate, rig.scale };
    }

    BoneFrame child(const VrFingerRig& rig, const BoneFrame& parent, uint32_t index, const VrGameRotation& rotate) {
      BoneFrame frame;
      frame.rotate    = parent.rotate * rotate;
      frame.translate = parent.translate + parent.rotate * (rig.translate[index] * parent.scale);
      frame.scale     = parent.scale * rig.boneScale[index];
      return frame;
    }

    /**
     * \brief Frames of a finger's bones for a curl, from the first one
     */
    std::array<BoneFrame, 4> fingerBones(
      const VrFingerRig&          rig,
            VrHand                hand,
            VrFinger              finger,
            float                 curl,
      const VrGameCameraPose&     controller) {
      VrFingerCurls curls = { };
      curls[uint32_t(finger)] = curl;

      const FingerChain& chain = Chains[uint32_t(finger)];
      std::array<BoneFrame, 4> bones;
      BoneFrame parent = handBone(rig, controller);

      for (uint32_t i = 0; i < chain.count; i++) {
        uint32_t index = chain.first + i;
        bones[i] = child(rig, parent, index, vrPoseFingerBone(index, hand, curls));
        parent = bones[i];
      }

      return bones;
    }

  }


  VrFingerJoints vrFingerJoints(
    const VrFingerRig&          rig,
          VrHand                hand,
          VrFinger              finger,
          float                 curl,
    const VrGameCameraPose&     controller) {
    const FingerChain& chain = Chains[uint32_t(finger)];
    std::array<BoneFrame, 4> bones = fingerBones(rig, hand, finger, curl, controller);

    // The three segments start at the last three bones, past a metacarpal
    uint32_t first = chain.count - 3;
    uint32_t last  = chain.first + chain.count - 1;
    const BoneFrame& end = bones[chain.count - 1];

    VrFingerJoints joints;

    for (uint32_t i = 0; i < 3; i++)
      joints[i] = bones[first + i].translate;

    // The tip goes on as the last segment came, turned by the last joint
    joints[3] = end.translate + end.rotate * (rig.translate[last] * (end.scale * TipLength));
    return joints;
  }


  VrFingerCurls vrFitFingers(
    const VrFingerRig&          rig,
          VrHand                hand,
    const VrGameCameraPose&     controller,
    const VrFingerRayFn&        ray) {
    VrFingerCurls curls;

    for (uint32_t f = 0; f < VrFingerCount; f++) {
      VrFinger finger = VrFinger(f);
      VrFingerJoints previous = vrFingerJoints(rig, hand, finger, 0.0f, controller);
      float curl = 1.0f;

      for (uint32_t step = 0; step <= FitSteps; step++) {
        float c = float(step) / float(FitSteps);
        VrFingerJoints joints = step ? vrFingerJoints(rig, hand, finger, c, controller) : previous;

        bool hit = ray(joints[0], joints[1]) || ray(joints[1], joints[2]) || ray(joints[2], joints[3])
          || (step && ray(previous[3], joints[3]));

        if (hit) {
          curl = std::max(c - 0.5f / float(FitSteps), 0.0f);
          break;
        }

        previous = joints;
      }

      curls[f] = curl;
    }

    return curls;
  }


  VrVector3 vrPalmDirection(
    const VrFingerRig&          rig,
          VrHand                hand,
    const VrGameCameraPose&     controller) {
    VrVector3 index  = fingerBones(rig, hand, VrFinger::Index,  0.0f, controller)[1].translate;
    VrVector3 little = fingerBones(rig, hand, VrFinger::Little, 0.0f, controller)[1].translate;
    std::array<BoneFrame, 4> middle = fingerBones(rig, hand, VrFinger::Middle, 0.0f, controller);

    VrVector3 across = little - index;
    VrVector3 along  = middle[1].translate - middle[0].translate;
    VrVector3 normal = cross(along, across);
    float length = vrLength(normal);

    if (!(length > 1e-6f))
      return VrVector3();

    normal = normal * (1.0f / length);

    // The fingers close towards the palm
    VrVector3 closed = vrFingerJoints(rig, hand, VrFinger::Middle, 1.0f, controller)[3];

    if (vrDot(closed - middle[1].translate, normal) < 0.0f)
      normal = normal * -1.0f;

    return normal;
  }


  VrVector3 vrPalmTurn(
    const VrVector3&            palm,
    const VrVector3&            centre,
    const VrVector3&            point,
          float                 maxAngle) {
    VrVector3 towards = point - centre;
    float distance = vrLength(towards);

    if (!(distance > 1e-3f) || !(vrLength(palm) > 0.5f))
      return VrVector3();

    towards = towards * (1.0f / distance);

    VrVector3 axis = cross(palm, towards);
    float sine   = vrLength(axis);
    float cosine = vrDot(palm, towards);
    float angle  = std::min(std::atan2(sine, cosine), maxAngle);

    // Straight away from the point: no axis to turn about
    if (!(sine > 1e-4f))
      return VrVector3();

    return axis * (angle / sine);
  }


  bool VrGrabFingerBlend::update(
    const VrFingerCurls*        tracked,
    const VrFingerCurls*        fitted,
          float                 dt,
          VrFingerCurls&        curls) {
    if (fitted)
      m_fitted = *fitted;

    float change = BlendTime > 0.0f ? dt / BlendTime : 1.0f;
    m_weight = fitted ? std::min(m_weight + change, 1.0f) : std::max(m_weight - change, 0.0f);

    if (!(m_weight > 0.0f)) {
      if (tracked)
        curls = *tracked;
      return tracked != nullptr;
    }

    for (uint32_t i = 0; i < VrFingerCount; i++) {
      float own  = tracked ? (*tracked)[i] : 0.0f;
      float held = tracked ? std::min(own, m_fitted[i]) : m_fitted[i];
      curls[i] = own + (held - own) * m_weight;
    }

    return true;
  }

}
