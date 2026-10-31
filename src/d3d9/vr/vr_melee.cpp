#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_melee.h"

namespace dxvk {

  namespace {

    // A weapon's bound outside this range is not a weapon's, in game units
    constexpr float MinBladeRadius = 2.0f;
    constexpr float MaxBladeRadius = 150.0f;

    // Guard pose, see vrIsGuardPose. Distances are in metres from the head.
    constexpr float GuardMaxSlope     = 0.57f;  // sine of 35 degrees from horizontal
    constexpr float GuardMinCrossing  = 0.64f;  // cosine of 50 degrees from the side
    constexpr float GuardMinAhead     = 0.10f;
    constexpr float GuardMaxAhead     = 0.75f;
    constexpr float GuardMinHeight    = -0.60f;
    constexpr float GuardMaxHeight    = 0.30f;
    constexpr float GuardMaxSideways  = 0.40f;

    // Fist, see vrFistBlade and vrIsFist: knuckles about 8 cm ahead of the
    // grip's centre, and how far each finger closes on the grip or trigger
    constexpr float FistReach = 0.10f * VrGameUnitsPerMetre;
    constexpr float FistCurl  = 0.6f;

    VrVector3 lerp(const VrVector3& a, const VrVector3& b, float t) {
      return a + (b - a) * t;
    }

    float clamp01(float v) {
      return std::min(std::max(v, 0.0f), 1.0f);
    }

  }


  VrVector3 vrPointFromHand(const VrGameCameraPose& hand, const VrVector3& local) {
    return hand.position + hand.forward * local.x + hand.up * local.y + hand.right * local.z;
  }


  VrVector3 vrPointToHand(const VrGameCameraPose& hand, const VrVector3& world) {
    VrVector3 offset = world - hand.position;
    return { vrDot(offset, hand.forward), vrDot(offset, hand.up), vrDot(offset, hand.right) };
  }


  bool vrMeasureBlade(
    const VrGameCameraPose&     hand,
    const VrVector3&            centre,
          float                 radius,
          VrBlade&              blade) {
    if (!(radius >= MinBladeRadius && radius <= MaxBladeRadius))
      return false;

    VrVector3 toCentre = centre - hand.position;
    float distance = vrLength(toCentre);

    // A bound centred on the hand gives no direction: the blade runs along
    // the hand's forward axis then
    VrVector3 direction = distance > 1e-3f ? toCentre * (1.0f / distance) : hand.forward;

    blade.base = { };
    blade.tip  = vrPointToHand(hand, hand.position + direction * (distance + radius));
    return true;
  }


  VrSegment vrBladeInWorld(const VrGameCameraPose& hand, const VrBlade& blade) {
    return { vrPointFromHand(hand, blade.base), vrPointFromHand(hand, blade.tip) };
  }


  VrBlade vrFistBlade() {
    VrBlade blade;
    blade.base = { };
    blade.tip  = { FistReach, 0.0f, 0.0f };
    return blade;
  }


  bool vrIsFist(const VrFingerCurls& curls) {
    for (uint32_t i = 1; i < VrFingerCount; i++) {
      if (curls[i] < FistCurl)
        return false;
    }

    return true;
  }


  VrSegmentClosest vrSegmentClosest(const VrSegment& s, const VrSegment& t) {
    // Closest points of two segments, after Ericson, Real-Time Collision Detection 5.1.9
    VrVector3 d1 = s.b - s.a;
    VrVector3 d2 = t.b - t.a;
    VrVector3 r  = s.a - t.a;

    float a = vrDot(d1, d1);
    float e = vrDot(d2, d2);
    float f = vrDot(d2, r);

    constexpr float Epsilon = 1e-8f;

    VrSegmentClosest result;

    if (a <= Epsilon && e <= Epsilon) {
      result.distance = vrLength(r);
      return result;
    }

    float u = 0.0f;
    float v = 0.0f;

    if (a <= Epsilon) {
      v = clamp01(f / e);
    } else {
      float c = vrDot(d1, r);

      if (e <= Epsilon) {
        u = clamp01(-c / a);
      } else {
        float b = vrDot(d1, d2);
        float denom = a * e - b * b;

        u = denom > Epsilon ? clamp01((b * f - c * e) / denom) : 0.0f;
        v = (b * u + f) / e;

        if (v < 0.0f) {
          v = 0.0f;
          u = clamp01(-c / a);
        } else if (v > 1.0f) {
          v = 1.0f;
          u = clamp01((b - c) / a);
        }
      }
    }

    result.distance = vrLength((s.a + d1 * u) - (t.a + d2 * v));
    result.s = u;
    result.t = v;
    return result;
  }


  float vrSegmentDistance(const VrSegment& s, const VrSegment& t) {
    return vrSegmentClosest(s, t).distance;
  }


  VrSweepContact vrSweptSegmentContact(
    const VrSegment&            from,
    const VrSegment&            to,
    const VrSegment&            fixed,
          uint32_t              steps,
          float                 radius) {
    steps = std::max(steps, 1u);

    VrSweepContact contact;

    for (uint32_t i = 1; i <= steps && !contact.touches; i++) {
      float time = float(i) / float(steps);
      VrSegment moving = { lerp(from.a, to.a, time), lerp(from.b, to.b, time) };
      VrSegmentClosest closest = vrSegmentClosest(moving, fixed);

      if (closest.distance > radius)
        continue;

      contact.touches     = true;
      contact.time        = time;
      contact.distance    = closest.distance;
      contact.fixedPoint  = lerp(fixed.a, fixed.b, closest.t);
      contact.movingPoint = lerp(moving.a, moving.b, closest.s);
      contact.motion      = lerp(to.a, to.b, closest.s) - lerp(from.a, from.b, closest.s);
    }

    return contact;
  }


  VrBladeHit vrComputeBladeHit(const VrSweepContact& contact, float radius) {
    constexpr float MinLength = 1e-4f;

    VrVector3 outward = contact.movingPoint - contact.fixedPoint;
    float outwardLength = vrLength(outward);

    VrBladeHit hit;
    hit.location = contact.fixedPoint;

    if (outwardLength > MinLength)
      hit.location = hit.location + outward * (std::min(radius, outwardLength) / outwardLength);

    float motionLength = vrLength(contact.motion);

    if (motionLength > MinLength)
      hit.direction = contact.motion * (1.0f / motionLength);
    else if (outwardLength > MinLength)
      hit.direction = outward * (-1.0f / outwardLength);
    else
      hit.direction = { 0.0f, 0.0f, -1.0f };

    return hit;
  }


  void VrSwingTracker::reset() {
    m_hasTip = false;
    m_speed  = 0.0f;
    m_hits.clear();
  }


  void VrSwingTracker::update(const VrVector3& tip, float dt) {
    bool wasSwinging = swinging();

    m_speed = m_hasTip && dt > 0.0f
      ? vrLength(tip - m_tip) / (dt * VrGameUnitsPerMetre)
      : 0.0f;

    m_tip    = tip;
    m_hasTip = true;

    if (swinging() && !wasSwinging)
      m_swing++;

    for (Hit& hit : m_hits)
      hit.age += dt;

    m_hits.erase(std::remove_if(m_hits.begin(), m_hits.end(),
      [this] (const Hit& hit) { return !blocks(hit); }), m_hits.end());
  }


  bool VrSwingTracker::hasHit(const void* target) const {
    return std::any_of(m_hits.begin(), m_hits.end(),
      [this, target] (const Hit& hit) { return hit.target == target && blocks(hit); });
  }


  void VrSwingTracker::addHit(const void* target) {
    if (!hasHit(target))
      m_hits.push_back({ target, m_swing, 0.0f });
  }


  bool VrSwingTracker::blocks(const Hit& hit) const {
    return (swinging() && hit.swing == m_swing) || hit.age < RehitTime;
  }


  bool vrIsGuardPose(const VrGameCameraPose& head, const VrSegment& blade) {
    VrVector3 along = blade.b - blade.a;
    float length = vrLength(along);

    if (length < 1e-3f)
      return false;

    along = along * (1.0f / length);

    // The head's heading, level with the ground; the game world has Z up
    VrVector3 forward = { head.forward.x, head.forward.y, 0.0f };
    float flat = vrLength(forward);

    if (flat < 1e-3f)
      return false;

    forward = forward * (1.0f / flat);
    VrVector3 right = { forward.y, -forward.x, 0.0f };

    if (std::fabs(along.z) > GuardMaxSlope)
      return false;

    if (std::fabs(vrDot(along, right)) < GuardMinCrossing)
      return false;

    VrVector3 middle = (blade.a + blade.b) * 0.5f - head.position;
    float ahead    = vrDot(middle, forward) / VrGameUnitsPerMetre;
    float sideways = vrDot(middle, right) / VrGameUnitsPerMetre;
    float height   = middle.z / VrGameUnitsPerMetre;

    return ahead >= GuardMinAhead && ahead <= GuardMaxAhead
        && height >= GuardMinHeight && height <= GuardMaxHeight
        && std::fabs(sideways) <= GuardMaxSideways;
  }


  void VrGuardTracker::update(bool pose, bool swinging, float dt) {
    if (pose && !swinging) {
      m_guarding = true;
      m_outTime  = 0.0f;
      return;
    }

    // A swing ends the guard at once, so that the blow is not blocked by it
    if (swinging) {
      reset();
      return;
    }

    m_outTime += dt;

    if (m_outTime >= ReleaseTime)
      reset();
  }


  void VrGuardTracker::reset() {
    m_guarding = false;
    m_outTime  = 0.0f;
  }

}
