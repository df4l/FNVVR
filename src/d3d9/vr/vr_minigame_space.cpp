#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_minigame_space.h"

namespace dxvk {

  namespace {

    constexpr float DegreesPerRadian = 57.2957795f;

    // Closer to the keyhole than this, the pin's direction is not known
    constexpr float MinPinReach = 0.03f;

    // The game's pin: the slider moves by v^3 * PinStep per frame, and it
    // ignores a stick under its dead zone (LockPickMenu::RotateLockpick)
    constexpr float PinStep = 80.0f;
    constexpr float PinStickMin = 7900.0f / 32767.0f;

    // The screwdriver's dead zone is wider (LockPickMenu::DoIdle)
    constexpr float TensionStickMin = 8800.0f / 32767.0f;

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return VrVector3 {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x };
    }

    VrVector3 toScene(const VrSceneSpace& space, const VrVector3& v) {
      return space.right * v.x + space.up * v.y + space.back * v.z;
    }

    VrVector3 fromScene(const VrSceneSpace& space, const VrVector3& v) {
      return VrVector3 { vrDot(v, space.right), vrDot(v, space.up), vrDot(v, space.back) };
    }

    // Rotation whose columns are the given orthonormal axes
    VrQuaternion fromAxes(const VrVector3& x, const VrVector3& y, const VrVector3& z) {
      float trace = x.x + y.y + z.z;
      VrQuaternion q;

      if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (y.z - z.y) * s;
        q.y = (z.x - x.z) * s;
        q.z = (x.y - y.x) * s;
      } else if (x.x > y.y && x.x > z.z) {
        float s = 2.0f * std::sqrt(1.0f + x.x - y.y - z.z);
        q.w = (y.z - z.y) / s;
        q.x = 0.25f * s;
        q.y = (y.x + x.y) / s;
        q.z = (z.x + x.z) / s;
      } else if (y.y > z.z) {
        float s = 2.0f * std::sqrt(1.0f + y.y - x.x - z.z);
        q.w = (z.x - x.z) / s;
        q.x = (y.x + x.y) / s;
        q.y = 0.25f * s;
        q.z = (z.y + y.z) / s;
      } else {
        float s = 2.0f * std::sqrt(1.0f + z.z - x.x - y.y);
        q.w = (x.y - y.x) / s;
        q.x = (z.x + x.z) / s;
        q.y = (z.y + y.z) / s;
        q.z = 0.25f * s;
      }

      return vrNormalize(q);
    }

    // A direction of a model in the world
    // Around a body's middle, in the units of the models, all at the world's
    // scale. A dealer in place stands 13 units beyond this from a table.
    constexpr float BodyRadius   = 6.0f;
    // A head this close to the player's goes through the view
    constexpr float SeatClearing = 25.0f;
    // Bodies on another floor are not in the way
    constexpr float BodyLowest   = -40.0f;
    constexpr float BodyHighest  = 140.0f;

    VrVector3 modelToWorld(const VrWorldObject& object, const VrVector3& v) {
      return object.x * v.x + object.y * v.y + object.z * v.z;
    }

  }


  VrGameCameraPose vrSeatCamera(
    const VrWorldObject&    object,
    const VrSceneFit&       fit,
    const VrPose&           headInReference,
          float             cameraHeight,
          float             unitsPerMetre) {
    VrVector3 seat   = object.position + modelToWorld(object, fit.seat) * object.scale;
    VrVector3 facing = modelToWorld(object, fit.facing);

    // The head may be turned from the reference: the camera is turned as
    // much the other way, for the head to look where it has to
    VrVector3 look = vrRotate(headInReference.orientation, VrVector3 { 0.0f, 0.0f, -1.0f });
    float turn = std::abs(look.x) + std::abs(look.z) > 1e-6f ? std::atan2(look.x, -look.z) : 0.0f;
    float c = std::cos(turn);
    float s = std::sin(turn);

    VrGameCameraPose camera;
    camera.forward = { facing.x * c - facing.y * s, facing.x * s + facing.y * c, 0.0f };
    camera.up      = { 0.0f, 0.0f, 1.0f };
    camera.right   = { camera.forward.y, -camera.forward.x, 0.0f };

    VrVector3 head = camera.right * headInReference.position.x
                   + camera.forward * -headInReference.position.z;

    camera.position = seat - head * unitsPerMetre;
    camera.position.z = cameraHeight;
    return camera;
  }


  VrSceneSpace vrSceneSpaceOnObject(
    const VrPose&           reference,
    const VrGameCameraPose& camera,
    const VrWorldObject&    object,
    const VrSceneFit&       fit,
          float             unitsPerMetre) {
    VrVector3 sceneX = modelToWorld(object, fit.sceneX);
    VrVector3 sceneY = modelToWorld(object, fit.sceneY);
    VrVector3 sceneZ = modelToWorld(object, fit.sceneZ);

    // The camera's axes, which are the reference's, in the scene
    auto inScene = [&] (const VrVector3& v) {
      return VrVector3 { vrDot(v, sceneX), vrDot(v, sceneY), vrDot(v, sceneZ) };
    };

    VrVector3 origin = object.position + modelToWorld(object, fit.origin) * object.scale;

    VrSceneSpace space;
    space.anchor.position    = vrTransformPoint(reference, vrComputeGamePointInReference(camera, origin, unitsPerMetre));
    space.anchor.orientation = reference.orientation;
    space.origin             = { 0.0f, 0.0f, 0.0f };
    space.right              = inScene(camera.right);
    space.up                 = inScene(camera.up);
    space.back               = inScene(camera.forward * -1.0f);
    space.unitsPerMetre      = unitsPerMetre / (fit.scale * object.scale);
    return space;
  }


  VrVector3 vrPointInObject(
    const VrWorldObject&    object,
    const VrVector3&        point) {
    VrVector3 offset = (point - object.position) * (1.0f / object.scale);
    return { vrDot(offset, object.x), vrDot(offset, object.y), vrDot(offset, object.z) };
  }


  bool vrBodyInTheWay(
    const VrSceneFit&       fit,
    const VrVector3&        body) {
    if (body.z < BodyLowest || body.z > BodyHighest)
      return false;

    float seatX = body.x - fit.seat.x;
    float seatY = body.y - fit.seat.y;

    if (seatX * seatX + seatY * seatY < SeatClearing * SeatClearing)
      return true;

    for (uint32_t i = 0; i < fit.areaCount; i++) {
      const VrModelArea& area = fit.areas[i];

      if (body.x > area.minX - BodyRadius && body.x < area.maxX + BodyRadius
       && body.y > area.minY - BodyRadius && body.y < area.maxY + BodyRadius)
        return true;
    }

    return false;
  }


  VrSceneSpace vrSceneSpaceAroundHead(
    const VrPose&           head,
    const VrVector3&        camera,
    const VrVector3&        forward,
          float             unitsPerMetre) {
    VrSceneSpace space;
    space.anchor        = vrMakeRecenterReference(head);
    space.origin        = camera;
    space.unitsPerMetre = unitsPerMetre;
    space.up            = { 0.0f, 0.0f, 1.0f };

    float length = std::hypot(forward.x, forward.y);

    // A camera looking straight down: the scene's +Y is ahead
    space.back = length > 1e-4f
      ? VrVector3 { -forward.x / length, -forward.y / length, 0.0f }
      : VrVector3 { 0.0f, -1.0f, 0.0f };

    space.right = cross(space.up, space.back);
    return space;
  }


  VrSceneSpace vrSceneSpaceInFront(
    const VrPose&           head,
    const VrVector3&        target,
          float             distance,
          float             drop,
          float             unitsPerMetre) {
    VrSceneSpace space;
    space.anchor          = vrMakeRecenterReference(head);
    space.anchor.position = vrTransformPoint(space.anchor, VrVector3 { 0.0f, -drop, -distance });
    space.origin          = target;
    space.unitsPerMetre   = unitsPerMetre;
    space.right           = { 0.0f, 0.0f, 1.0f };
    space.up              = { 0.0f, 1.0f, 0.0f };
    space.back            = { -1.0f, 0.0f, 0.0f };
    return space;
  }


  VrVector3 vrSceneToRoom(const VrSceneSpace& space, const VrVector3& point) {
    return vrTransformPoint(space.anchor,
      fromScene(space, point - space.origin) * (1.0f / space.unitsPerMetre));
  }


  VrVector3 vrRoomToScene(const VrSceneSpace& space, const VrVector3& point) {
    VrVector3 local = vrTransformPoint(vrInverse(space.anchor), point);
    return space.origin + toScene(space, local) * space.unitsPerMetre;
  }


  VrGameCameraPose vrSceneCameraPose(const VrSceneSpace& space, const VrPose& eye) {
    VrPose local = vrCompose(vrInverse(space.anchor), eye);

    VrGameCameraPose pose;
    pose.forward  = toScene(space, vrRotate(local.orientation, VrVector3 { 0.0f, 0.0f, -1.0f }));
    pose.up       = toScene(space, vrRotate(local.orientation, VrVector3 { 0.0f, 1.0f,  0.0f }));
    pose.right    = toScene(space, vrRotate(local.orientation, VrVector3 { 1.0f, 0.0f,  0.0f }));
    pose.position = space.origin + toScene(space, local.position) * space.unitsPerMetre;
    return pose;
  }


  VrPanelPlacement vrSceneSurface(
    const VrSceneSpace&     space,
    const VrVector3&        centre,
          float             sizeX,
          float             sizeY) {
    // The scene's axes as the anchor sees them
    VrVector3 x = fromScene(space, VrVector3 { 1.0f, 0.0f, 0.0f });
    VrVector3 y = fromScene(space, VrVector3 { 0.0f, 1.0f, 0.0f });
    VrVector3 z = fromScene(space, VrVector3 { 0.0f, 0.0f, 1.0f });

    VrPanelPlacement placement;
    placement.pose.position    = vrSceneToRoom(space, centre);
    placement.pose.orientation = vrNormalize(space.anchor.orientation * fromAxes(x, y, z));
    placement.width            = sizeX / space.unitsPerMetre;
    placement.height           = sizeY / space.unitsPerMetre;
    return placement;
  }


  VrVector3 vrSurfacePoint(
    const VrVector3&        centre,
          float             sizeX,
          float             sizeY,
    const VrVector2&        uv) {
    return VrVector3 {
      centre.x + (uv.x - 0.5f) * sizeX,
      centre.y + (0.5f - uv.y) * sizeY,
      centre.z };
  }


  VrPanelPlacement vrSceneViewRectangle(
    const VrSceneSpace&     space,
          float             distance,
    const VrGameFrustum&    frustum) {
    VrVector3 centre = {
      distance,
      0.5f * (frustum.top + frustum.bottom) * distance,
      0.5f * (frustum.left + frustum.right) * distance };

    VrPanelPlacement placement;
    placement.pose.position    = vrSceneToRoom(space, centre);
    placement.pose.orientation = space.anchor.orientation;
    placement.width            = (frustum.right - frustum.left) * distance / space.unitsPerMetre;
    placement.height           = (frustum.top - frustum.bottom) * distance / space.unitsPerMetre;
    return placement;
  }


  float vrLockPinAngle(const VrPose& lock, const VrVector3& hand, float previous) {
    VrVector3 local = vrTransformPoint(vrInverse(lock), hand);

    if (std::hypot(local.x, local.y) < MinPinReach)
      return previous;

    float angle = std::atan2(local.x, local.y) * DegreesPerRadian;
    return std::min(std::max(angle, -90.0f), 90.0f);
  }


  float vrLockPinSlider(float angle, float width) {
    return (angle / 180.0f + 0.5f) * width;
  }


  float vrLockPinStick(float slider, float target) {
    float delta = target - slider;
    float size  = std::abs(delta) / PinStep;

    if (size < PinStickMin * PinStickMin * PinStickMin)
      return 0.0f;

    float stick = std::min(std::cbrt(size), 1.0f);
    return delta < 0.0f ? -stick : stick;
  }


  float vrLockTensionStick(float tension) {
    if (tension <= 0.0f)
      return 0.0f;

    return -(TensionStickMin + (1.0f - TensionStickMin) * std::min(tension, 1.0f));
  }


  float VrLockTension::update(bool trigger, bool grip, const VrQuaternion& hand) {
    if (grip && !m_held)
      m_taken = hand;

    m_held = grip;

    if (trigger)
      return 1.0f;

    if (!grip)
      return 0.0f;

    // The angle of the rotation since the grip took it, about any axis: a
    // wrist's twist is the bulk of what a held hand can turn
    VrQuaternion turn = vrConjugate(m_taken) * hand;
    float twist = 2.0f * std::atan2(std::sqrt(turn.x * turn.x + turn.y * turn.y + turn.z * turn.z), std::abs(turn.w));

    return std::min(std::max((twist - DeadTwist) / (FullTwist - DeadTwist), 0.0f), 1.0f);
  }


  bool VrLeverPull::update(bool grip, const VrVector3& hand, const VrVector3& ball) {
    bool closes = grip && !m_gripWas;
    m_gripWas = grip;

    if (!grip) {
      m_holds  = false;
      m_pulled = false;
      return false;
    }

    if (closes && vrLength(hand - ball) <= Reach) {
      m_holds  = true;
      m_pulled = false;
      m_startY = hand.y;
    }

    if (!m_holds || m_pulled)
      return false;

    // The hand may have started above: a pull counts from its highest point
    m_startY = std::max(m_startY, hand.y);
    m_pulled = m_startY - hand.y >= PullDown;
    return m_pulled;
  }


  VrTableSignal VrTableSignals::update(const std::array<VrTableHand, VrHandCount>& hands, float dt) {
    m_cooldown = std::max(m_cooldown - dt, 0.0f);

    VrTableSignal signal = VrTableSignal::None;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrTableHand& hand = hands[i];
      HandTrack& track = m_hands[i];

      if (!hand.active || dt <= 0.0f) {
        track = HandTrack();
        continue;
      }

      if (track.known) {
        float down = (track.height - hand.height) / dt;

        if (hand.over && track.height > TapHeight && hand.height <= TapHeight && down >= TapSpeed)
          signal = VrTableSignal::Tap;

        float side = (hand.side - track.side) / dt;
        int32_t direction = std::abs(side) < WaveSpeed ? 0 : (side > 0.0f ? 1 : -1);

        track.sinceTurn += dt;

        if (track.sinceTurn > WaveWindow) {
          track.turns     = 0;
          track.direction = 0;
        }

        bool waves = hand.over && hand.height > TapHeight && hand.height < WaveHeight;

        if (!waves) {
          track.turns     = 0;
          track.direction = 0;
        } else if (direction && direction != track.direction) {
          if (track.direction)
            track.turns++;

          track.direction = direction;
          track.sinceTurn = 0.0f;

          if (track.turns >= WaveTurns && signal == VrTableSignal::None)
            signal = VrTableSignal::Wave;
        }
      }

      track.known  = true;
      track.height = hand.height;
      track.side   = hand.side;
    }

    if (signal == VrTableSignal::None)
      return signal;

    for (auto& track : m_hands) {
      track.turns     = 0;
      track.direction = 0;
    }

    if (m_cooldown > 0.0f)
      return VrTableSignal::None;

    m_cooldown = Cooldown;
    return signal;
  }

}
