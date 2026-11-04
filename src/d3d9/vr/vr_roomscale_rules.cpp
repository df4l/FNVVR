#include <cmath>

#include "vr_roomscale_rules.h"

namespace dxvk {

  namespace {

    // Below this a move is rounding, in game units
    constexpr float NoMove = 0.01f;

    float length(VrVector2 v) {
      return std::sqrt(v.x * v.x + v.y * v.y);
    }


    VrVector2 scaled(VrVector2 v, float factor) {
      return { v.x * factor, v.y * factor };
    }

  }


  VrVector2 vrRoomscaleWantedMove(VrVector2 offset, float deadZone) {
    float distance = length(offset);

    if (distance <= deadZone)
      return { };

    return scaled(offset, (distance - deadZone) / distance);
  }


  VrVector2 vrRoomscaleLimitMove(VrVector2 move, float maxStep) {
    float distance = length(move);

    if (distance <= maxStep)
      return move;

    return scaled(move, maxStep / distance);
  }


  VrVector2 vrRoomscaleAchievedMove(
          VrVector2             moved,
          VrVector2             gameMove,
          VrVector2             asked,
          bool                  following,
          float                 maxStep) {
    if (length(gameMove) <= NoMove) {
      if (!following || length(moved) > 2.0f * maxStep)
        return { };

      return moved;
    }

    // The walls may have stopped the stick's move rather than the head's,
    // which then must not read as the head's going backwards
    VrVector2 achieved = { moved.x - gameMove.x, moved.y - gameMove.y };

    if (achieved.x * asked.x + achieved.y * asked.y <= 0.0f)
      return { };

    return vrRoomscaleLimitMove(achieved, length(asked));
  }


  bool vrRoomscaleIsBlocked(VrVector2 achieved, VrVector2 asked) {
    float wanted = length(asked);
    return wanted > NoMove && length(achieved) < 0.5f * wanted;
  }


  VrVector2 vrRoomscalePushback(VrVector2 offset, float headRoom) {
    float distance = length(offset);

    if (distance <= headRoom)
      return { };

    return scaled(offset, (distance - headRoom) / distance);
  }

}
