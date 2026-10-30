#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

#include "vr_magazine.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    struct PartException {
      const char*      name;
      VrWeaponPartKind kind;
    };

    // Parts the keywords would get wrong, compared without case and
    // without the "##" prefix
    const PartException PartExceptions[] = {
      // The sniper rifle's second magazine model, for its other clip size
      { "srcliphigh",  VrWeaponPartKind::None },
      // The clamp over the Tesla cannon's cell (##TCECCell), which its
      // reload animation carries in
      { "tcclip",      VrWeaponPartKind::None },
      // The missile or mini-nuke in a launcher's tube
      { "nifround",    VrWeaponPartKind::Projectile },
    };

    // Also the ammunition boxes of the heavy weapons and the Red Glare's
    // rocket canister
    const char* const MagazineWords[] = { "clip", "magazine", "drum", "cell", "battery", "ammo", "canister" };

    // Parts next to the magazine, or magazines that do not come out
    const char* const NotMagazineWords[] = { "release", "latch", "lock", "spring", "peg", "bay", "assembly", "moon" };

    const char* const RackWords[] = { "slide", "bolt", "charginghandle", "lever", "bar" };

    // Barrels, and parts next to the rack part
    const char* const NotRackWords[] = { "barrel", "lock", "pin", "release", "trigger" };

    const char* const RoundWords[] = { "shell", "bullet", "round" };

    // Spent cases
    const char* const NotRoundWords[] = { "empty", "casing" };

    const char* const HingeWords[] = { "stock" };

    // The lock over a magazine, named with it
    const char* const LockWords[] = { "lock" };

    const char* const LoaderWords[] = { "speedloader" };

    // A loading gate, and the cap over the loading port of the Cowboy
    // repeater, the brush gun and the trail carbine
    const char* const GateWords[] = { "gate", "reloadcap" };

    // What a cylinder holds besides rounds: spent cases
    const char* const CartridgeWords[] = { "shell", "bullet", "casing" };

    template<size_t N>
    bool containsAny(const std::string& name, const char* const (&words)[N]) {
      for (const char* word : words) {
        if (name.find(word) != std::string::npos)
          return true;
      }

      return false;
    }

  }


  namespace {

    // The name without its "##" prefix and geometry suffix, in lower case,
    // or an empty name if it is not a part
    std::string partName(const char* name) {
      std::string part;

      if (!name || std::strncmp(name, "##", 2))
        return part;

      for (const char* c = name + 2; *c && *c != ':'; c++)
        part += char(std::tolower(static_cast<unsigned char>(*c)));

      return part;
    }

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

  }


  VrWeaponPartKind vrClassifyWeaponPart(const char* name) {
    std::string part = partName(name);

    if (part.empty())
      return VrWeaponPartKind::None;

    for (const PartException& exception : PartExceptions) {
      if (part == exception.name)
        return exception.kind;
    }

    if (containsAny(part, LoaderWords))
      return VrWeaponPartKind::Loader;

    if (containsAny(part, MagazineWords) && containsAny(part, LockWords))
      return VrWeaponPartKind::Lock;

    if (containsAny(part, MagazineWords) && !containsAny(part, NotMagazineWords))
      return VrWeaponPartKind::Magazine;

    if (containsAny(part, RackWords) && !containsAny(part, NotRackWords))
      return VrWeaponPartKind::Rack;

    if (containsAny(part, RoundWords) && !containsAny(part, NotRoundWords))
      return VrWeaponPartKind::Round;

    if (containsAny(part, HingeWords))
      return VrWeaponPartKind::Hinge;

    if (containsAny(part, GateWords))
      return VrWeaponPartKind::Gate;

    return VrWeaponPartKind::None;
  }


  bool vrIsCartridgePart(const char* name) {
    VrWeaponPartKind kind = vrClassifyWeaponPart(name);

    return kind == VrWeaponPartKind::Round
      || (kind == VrWeaponPartKind::None && containsAny(partName(name), CartridgeWords));
  }


  float vrDistanceToSegment(
    const VrVector3&            point,
    const VrVector3&            start,
    const VrVector3&            axis,
          float                 length) {
    float along = std::clamp(vrDot(point - start, axis), 0.0f, length);
    return vrLength(point - (start + axis * along));
  }


  bool vrIsAtMagazinePouch(
    const VrGameCameraPose&     head,
    const VrVector3&            hand,
    const VrMagazinePouch&      pouch) {
    // Level, forward follows the head's forward axis, and its up axis
    // as the head pitches towards straight up or down
    VrVector3 forward = {
      head.forward.x - head.forward.z * head.up.x,
      head.forward.y - head.forward.z * head.up.y,
      0.0f };

    float length = vrLength(forward);

    if (length < 1e-4f)
      return false;

    forward = forward * (1.0f / length);

    // The game's world is right-handed with Z up: facing north (+Y), east
    // (+X) is to the right
    VrVector3 right  = { forward.y, -forward.x, 0.0f };
    VrVector3 offset = (hand - head.position) * (1.0f / VrGameUnitsPerMetre);

    return -vrDot(offset, forward) >= pouch.behind
        &&  vrDot(offset, right)   <= pouch.right
        &&  offset.z               >= -pouch.below
        &&  vrLength(offset)       <= pouch.reach;
  }


  VrMagazineEvent VrMagazineHand::update(const VrMagazineInput& input) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    constexpr float M = VrGameUnitsPerMetre;

    VrVector3 fromWell = (input.held - input.well) * (1.0f / M);
    float along = vrDot(fromWell, input.axis);

    // The mouth of the well, where a magazine enters it, and the way into
    // it from there
    VrVector3 mouth = input.well + input.axis * (m_reach.depth * M);
    float toWay     = vrDistanceToSegment(input.held, mouth, input.axis, m_reach.approach * M) / M;

    switch (m_state) {
      case VrMagazineState::InWeapon:
        if (input.active && pressed && !input.blocked && ejectDistance(input) <= m_reach.eject) {
          seat(input, along);
          m_canInsert = false;
          return VrMagazineEvent::Ejected;
        }
        break;

      case VrMagazineState::Seating: {
        if (!input.grip || !input.active)
          return drop();

        VrVector3 across = fromWell - input.axis * along;
        float slide = along - m_alongStart;
        m_seat = std::max(slide, 0.0f);

        if (m_seat >= m_reach.armed)
          m_canInsert = true;

        if (m_seat > m_reach.depth + m_reach.slack || vrLength(across - m_acrossStart) > m_reach.leave) {
          m_state    = VrMagazineState::InHand;
          m_canEnter = false;
          return VrMagazineEvent::Cleared;
        }

        if (m_canInsert && slide <= 0.0f) {
          m_state = VrMagazineState::InWeapon;
          return VrMagazineEvent::Inserted;
        }
      } break;

      case VrMagazineState::InHand:
        if (!input.grip || !input.active)
          return drop();

        if (toWay > m_reach.capture + m_reach.rearm)
          m_canEnter = true;

        // A magazine coming from beyond the mouth starts at the mouth
        if (m_canEnter && !input.closed && toWay <= m_reach.capture) {
          seat(input, std::max(along - m_reach.depth, 0.0f));
          m_canInsert = true;
          return VrMagazineEvent::Engaged;
        }
        break;

      case VrMagazineState::Out:
        return take(input, pressed, toWay);
    }

    return VrMagazineEvent::None;
  }


  VrMagazineEvent VrMagazineHand::take(const VrMagazineInput& input, bool pressed, float toWay) {
    if (!input.active || !pressed || !vrIsAtMagazinePouch(input.head, input.hand, m_reach.pouch))
      return VrMagazineEvent::None;

    if (!input.roundsHeld)
      return VrMagazineEvent::NoRounds;

    m_state    = VrMagazineState::InHand;
    m_canEnter = toWay > m_reach.capture + m_reach.rearm;
    return VrMagazineEvent::Taken;
  }


  VrMagazineEvent VrMagazineHand::drop() {
    m_state = VrMagazineState::Out;
    return VrMagazineEvent::Dropped;
  }


  void VrMagazineHand::seat(const VrMagazineInput& input, float start) {
    VrVector3 fromWell = (input.held - input.well) * (1.0f / VrGameUnitsPerMetre);
    float along = vrDot(fromWell, input.axis);

    m_state       = VrMagazineState::Seating;
    m_alongStart  = start;
    m_acrossStart = fromWell - input.axis * along;
    m_seat        = std::max(along - start, 0.0f);
  }


  void VrMagazineHand::reset() {
    m_state     = VrMagazineState::InWeapon;
    m_canInsert = false;
    m_canEnter  = false;
    m_seat      = 0.0f;
  }


  void VrMagazineHand::empty() {
    reset();
    m_state = VrMagazineState::Out;
  }


  VrRoundEvent VrRoundLoader::update(const VrRoundInput& input, const VrRoundLoopTiming& loop) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    float toPort = vrLength(input.round - input.port) / VrGameUnitsPerMetre;

    switch (m_state) {
      case VrRoundState::Empty:
        if (!input.active || !pressed || !vrIsAtMagazinePouch(input.head, input.hand, m_reach.pouch))
          return VrRoundEvent::None;

        if (!input.roundsHeld)
          return VrRoundEvent::NoRounds;

        m_state    = VrRoundState::Holding;
        m_canStart = toPort > m_reach.capture + m_reach.rearm;
        return VrRoundEvent::Taken;

      case VrRoundState::Holding:
        if (!input.grip || !input.active) {
          m_state = VrRoundState::Empty;
          return VrRoundEvent::Dropped;
        }

        if (toPort > m_reach.capture + m_reach.rearm)
          m_canStart = true;

        if (m_canStart && !input.closed && toPort <= m_reach.capture && loop.length > 0.0f) {
          m_state    = VrRoundState::Loading;
          m_placed   = false;
          m_time     = 0.0f;
          m_lastTime = 0.0f;
          return VrRoundEvent::Started;
        }
        break;

      case VrRoundState::Loading: {
        if (!input.grip || !input.active) {
          m_state = VrRoundState::Empty;
          return VrRoundEvent::Dropped;
        }

        if (input.closed || toPort > m_reach.leave) {
          stop(input);
          return VrRoundEvent::Stopped;
        }

        m_lastTime = m_time;
        m_time    += std::max(input.dt, 0.0f);

        // The next loop brings the next round
        if (m_time >= loop.length) {
          m_time   = std::fmod(m_time, loop.length);
          m_placed = false;
        }

        if (!m_placed && m_time >= loop.placed) {
          m_placed = true;
          return VrRoundEvent::Placed;
        }
      } break;
    }

    return VrRoundEvent::None;
  }


  void VrRoundLoader::refuse() {
    m_state    = VrRoundState::Holding;
    m_canStart = false;
  }


  void VrRoundLoader::reset() {
    m_state    = VrRoundState::Empty;
    m_canStart = false;
    m_placed   = false;
    m_time     = 0.0f;
    m_lastTime = 0.0f;
  }


  void VrRoundLoader::stop(const VrRoundInput& input) {
    m_state    = VrRoundState::Holding;
    m_canStart = vrLength(input.round - input.port) / VrGameUnitsPerMetre > m_reach.capture + m_reach.rearm;
  }


  float VrMagazineHand::ejectDistance(const VrMagazineInput& input) const {
    constexpr float M = VrGameUnitsPerMetre;
    return vrDistanceToSegment(input.hand, input.well, input.axis, m_reach.below * M) / M;
  }


  VrRackEvent VrRackHand::update(const VrRackInput& input) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    VrVector3 offset = (input.hand - input.rack) * (1.0f / VrGameUnitsPerMetre);
    float along      = vrDot(offset, input.axis);
    VrVector3 across = offset - input.axis * along;

    if (!m_held) {
      if (!input.active || !pressed || input.blocked || grabDistance(input) > m_reach.grab)
        return VrRackEvent::None;

      // The part is taken where it rests: locked back, all the way back
      m_held        = true;
      m_pullStart   = std::clamp(input.rest, 0.0f, input.travel);
      m_full        = m_pullStart >= m_reach.full * input.travel;
      m_pull        = m_pullStart;
      m_alongStart  = along;
      m_acrossStart = across;
      return VrRackEvent::Grabbed;
    }

    if (!input.grip || !input.active || vrLength(across - m_acrossStart) > m_reach.leave) {
      bool racked = m_full;
      reset();
      return racked ? VrRackEvent::Racked : VrRackEvent::Released;
    }

    m_pull = std::clamp(m_pullStart + along - m_alongStart, 0.0f, input.travel);

    if (m_pull >= m_reach.full * input.travel)
      m_full = true;

    return VrRackEvent::None;
  }


  void VrRackHand::reset() {
    m_held = false;
    m_full = false;
    m_pull = 0.0f;
  }


  float VrRackHand::grabDistance(const VrRackInput& input) const {
    constexpr float M = VrGameUnitsPerMetre;
    return vrDistanceToSegment(input.hand, input.rack, input.axis, input.travel * M) / M;
  }


  VrBreakEvent VrBreakHand::update(const VrBreakInput& input) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    if (!input.active || !pressed || input.blocked || workDistance(input) > m_reach.reach)
      return VrBreakEvent::None;

    m_open = !m_open;
    return m_open ? VrBreakEvent::Opened : VrBreakEvent::Closed;
  }


  void VrBreakHand::reset() {
    m_open = false;
  }


  float VrBreakHand::workDistance(const VrBreakInput& input) const {
    constexpr float M = VrGameUnitsPerMetre;
    return vrDistanceToSegment(input.hand, input.breech, input.barrel, m_reach.length * M) / M;
  }


  VrLockEvent VrLockHand::update(const VrLockInput& input) {
    bool pressed = input.grip && !m_gripDown;
    m_gripDown = input.grip;

    if (!m_held) {
      if (!input.active || !pressed || input.blocked || grabDistance(input) > m_reach.grab)
        return VrLockEvent::None;

      m_held = true;
      return VrLockEvent::Grabbed;
    }

    // Let go of, it stays as it is, open or shut
    if (!input.grip || !input.active) {
      m_held = false;
      m_turn = m_open ? 1.0f : 0.0f;
      return VrLockEvent::None;
    }

    // The hand about the axis, from the lock shut towards open
    VrVector3 offset = input.hand - input.pivot;
    VrVector3 shut   = input.arm - input.axis * vrDot(input.arm, input.axis);
    float shutLength = vrLength(shut);

    if (shutLength <= 0.0f || input.angle <= 0.0f)
      return VrLockEvent::None;

    shut = shut * (1.0f / shutLength);
    VrVector3 side = cross(input.axis, shut);

    float angle = std::atan2(vrDot(offset, side), vrDot(offset, shut));
    m_turn = std::clamp(angle / input.angle, 0.0f, 1.0f);

    if (!m_open && m_turn >= m_reach.open) {
      m_open = true;
      return VrLockEvent::Opened;
    }

    if (m_open && m_turn <= m_reach.shut) {
      m_open = false;
      return VrLockEvent::Shut;
    }

    return VrLockEvent::None;
  }


  void VrLockHand::reset() {
    m_held = false;
    m_open = false;
    m_turn = 0.0f;
  }


  float VrLockHand::grabDistance(const VrLockInput& input) const {
    constexpr float M = VrGameUnitsPerMetre;
    float length = vrLength(input.arm);

    if (length <= 0.0f)
      return FLT_MAX;

    // Along the lock as it is turned now
    VrQuaternion turn = vrQuaternionFromAxisAngle(input.axis, m_turn * input.angle);
    VrVector3 arm = vrGameRotationFromQuaternion(turn) * input.arm;
    return vrDistanceToSegment(input.hand, input.pivot, arm * (1.0f / length), 2.0f * length) / M;
  }


  float vrTurnRate(
    const VrGameCameraPose&     from,
    const VrGameCameraPose&     to,
    const VrVector3&            axis,
          float                 dt) {
    if (dt <= 0.0f)
      return 0.0f;

    // Each axis of a frame turned by an angle about a unit axis, crossed
    // with where it was, adds up to twice the sine of the angle along it
    VrVector3 turn = cross(from.forward, to.forward) + cross(from.up, to.up) + cross(from.right, to.right);
    float s = std::clamp(0.5f * vrLength(turn), 0.0f, 1.0f);

    if (s <= 0.0f)
      return 0.0f;

    float angle = std::asin(s);
    return angle * vrDot(turn, axis) / (2.0f * s * dt);
  }


  VrBreakEvent VrFlickHand::update(const VrFlickInput& input) {
    float last = m_lastRoll;
    m_lastRoll = input.active ? input.roll : 0.0f;
    m_wait     = std::max(m_wait - input.dt, 0.0f);

    if (!input.active || input.blocked || m_wait > 0.0f)
      return VrBreakEvent::None;

    // The slower of the two frames, if they turn the same way
    float roll = last * m_lastRoll > 0.0f
      ? (m_lastRoll > 0.0f ? std::min(last, m_lastRoll) : std::max(last, m_lastRoll))
      : 0.0f;

    bool opens  = !m_open && roll >=  m_reach.rate;
    bool closes =  m_open && roll <= -m_reach.rate;

    if (!opens && !closes)
      return VrBreakEvent::None;

    m_open = opens;
    m_wait = m_reach.rest;
    return m_open ? VrBreakEvent::Opened : VrBreakEvent::Closed;
  }


  void VrFlickHand::reset() {
    m_open     = false;
    m_lastRoll = 0.0f;
    m_wait     = 0.0f;
  }


  bool vrFindMagazineTiming(
    const VrMagazineSample*     samples,
          size_t                count,
          VrMagazineTiming&     timing) {
    // Out of its place by more than this, the magazine has moved
    constexpr float Moved = 0.01f;
    // The new magazine is held this far out of its place before it goes in
    constexpr float Held  = 0.04f;

    size_t first = count;
    size_t last  = count;
    float  far   = 0.0f;

    for (size_t i = 0; i < count; i++) {
      float distance = vrLength(samples[i].offset);

      if (distance > Moved) {
        if (first == count)
          first = i;

        last = i;
        far  = std::max(far, distance);
      }
    }

    // The magazine must leave and come back before the animation ends
    if (first == count || last + 1 >= count)
      return false;

    float reach = std::min(Held, far * 0.75f);
    size_t grip = last;

    while (grip > first && vrLength(samples[grip].offset) < reach)
      grip -= 1;

    float length = vrLength(samples[grip].offset);

    timing.out  = samples[first].time;
    timing.in   = samples[last + 1].time;
    timing.grip = samples[grip].time;
    timing.axis = samples[grip].offset * (1.0f / length);
    return true;
  }


  bool vrFindRoundTiming(
    const VrMagazineSample*     samples,
          size_t                count,
          VrMagazineTiming&     timing,
          size_t&               place) {
    // Within this of its place the round is still, for this long
    constexpr float Still     = 0.003f;
    constexpr float StillTime = 0.1f;
    // It must have come from this far
    constexpr float Moved     = 0.03f;
    // It is held this far out of its place before it goes in
    constexpr float Held      = 0.04f;

    for (size_t i = 1; i < count; i++) {
      const VrVector3& at = samples[i].offset;

      // Still from here on for long enough
      size_t end = i;

      while (end + 1 < count && vrLength(samples[end + 1].offset - at) <= Still)
        end += 1;

      if (samples[end].time - samples[i].time < StillTime)
        continue;

      float far = 0.0f;

      for (size_t j = 0; j < i; j++)
        far = std::max(far, vrLength(samples[j].offset - at));

      if (far < Moved)
        continue;

      float reach = std::min(Held, far * 0.75f);
      size_t grip = i - 1;

      while (grip > 0 && vrLength(samples[grip].offset - at) < reach)
        grip -= 1;

      VrVector3 from = samples[grip].offset - at;

      timing.out  = samples[0].time;
      timing.in   = samples[i].time;
      timing.grip = samples[grip].time;
      timing.axis = from * (1.0f / vrLength(from));
      place = i;
      return true;
    }

    return false;
  }


  size_t vrFindMagazineHold(
    const VrGameTransform*      inHand,
          size_t                count) {
    // Moving with the hand, between two samples: metres and radians (2 degrees)
    constexpr float MaxStep = 0.003f;
    constexpr float MaxTurn = 0.035f;

    auto carried = [inHand] (size_t a, size_t b) {
      return vrLength(inHand[b].translate - inHand[a].translate) <= MaxStep
          && vrRotationAngle(inHand[a].rotate, inHand[b].rotate) <= MaxTurn;
    };

    size_t best = count;

    for (size_t i = 1; i + 1 < count; i++) {
      if (!carried(i - 1, i) || !carried(i, i + 1))
        continue;

      if (best == count || vrLength(inHand[i].translate) < vrLength(inHand[best].translate))
        best = i;
    }

    return best;
  }


  bool vrUprightMagazineHold(
    const VrGameRotation&       handInController,
    const VrVector3&            up,
    const VrVector3&            insertion,
    const VrVector3&            centre,
          VrGameTransform&      inHand,
          float&                angle) {
    VrVector3 top = handInController * (inHand.rotate * insertion);
    float lengths = vrLength(top) * vrLength(up);
    angle = 0.0f;

    if (lengths <= 0.0f)
      return false;

    float cosine = vrDot(top, up) / lengths;
    angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));

    // Pointing up or sideways, as the animation holds it
    if (cosine >= 0.0f)
      return false;

    // The least turn in the controller's frame, about the centre, in the
    // hand bone's frame
    VrGameRotation turn = vrTranspose(handInController) * vrComputeRotationBetween(top, up) * handInController;
    VrVector3 middle = inHand.translate + inHand.rotate * centre;

    inHand.rotate    = turn * inHand.rotate;
    inHand.translate = middle - inHand.rotate * centre;
    return true;
  }


  bool vrFindRackMotion(
    const VrMagazineSample*     samples,
          size_t                count,
          VrRackMotion&         motion) {
    // Pulled back by more than this, the part racks the weapon
    constexpr float Moved = 0.01f;
    // Within this of its place, the part is home
    constexpr float Home  = 0.003f;

    size_t back = count;
    float  far  = Moved;

    for (size_t i = 0; i < count; i++) {
      float distance = vrLength(samples[i].offset);

      if (distance > far) {
        back = i;
        far  = distance;
      }
    }

    if (back == count)
      return false;

    size_t start = back;

    while (start > 0 && vrLength(samples[start - 1].offset) > Home)
      start -= 1;

    size_t home = back;

    while (home + 1 < count && vrLength(samples[home].offset) > Home)
      home += 1;

    motion.start  = start;
    motion.back   = back;
    motion.home   = home;
    motion.axis   = samples[back].offset * (1.0f / far);
    motion.travel = far;
    return true;
  }


  int32_t vrFindNearestSound(
    const float*                times,
          size_t                count,
          float                 time,
          float                 window,
          int32_t               exclude) {
    return vrFindSoundDuring(times, count, time, time, window, exclude);
  }


  int32_t vrFindSoundDuring(
    const float*                times,
          size_t                count,
          float                 from,
          float                 to,
          float                 window,
          int32_t               exclude) {
    int32_t nearest = -1;
    float   best    = window;

    for (size_t i = 0; i < count; i++) {
      float distance = std::max({ from - times[i], times[i] - to, 0.0f });

      if (int32_t(i) != exclude && distance <= best) {
        nearest = int32_t(i);
        best    = distance;
      }
    }

    return nearest;
  }


  VrReloadParts vrPartsToWork(const VrReloadSituation& situation) {
    VrReloadParts parts;

    // The hand is busy with what it reloads the weapon with
    bool handBusy = situation.magazine == VrMagazineState::InHand
                 || situation.magazine == VrMagazineState::Seating;
    bool empty    = situation.clip <= 0;

    switch (situation.kind) {
      case VrReloadKind::Magazine: {
        if (situation.magazine != VrMagazineState::InWeapon)
          break;

        parts.rack = situation.hasRack && (situation.jammed || situation.pendingFill);

        // A lock is opened to take the magazine out, and shut over a
        // loaded one
        bool takeOut = !parts.rack && (empty || (situation.jammed && !situation.hasRack));
        bool locked  = situation.hasLock && !situation.lockOpen;

        parts.magazine = takeOut && !locked;
        parts.lock     = situation.hasLock && !parts.rack && (takeOut ? locked : situation.lockOpen);
      } break;

      case VrReloadKind::Rounds: {
        bool holding = situation.rounds != VrRoundState::Empty;

        parts.port = holding && !situation.full;
        parts.rack = !holding && situation.hasRack
          && (situation.jammed || (situation.unchambered && !empty));
      } break;

      case VrReloadKind::BreakAction:
        parts.hinge = situation.actionOpen
          ? situation.magazine == VrMagazineState::InWeapon && !empty
          : !handBusy && (empty || situation.jammed);
        break;

      case VrReloadKind::Cylinder:
      case VrReloadKind::None:
        break;
    }

    return parts;
  }

}
