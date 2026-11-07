#include <cmath>
#include <cstring>

#include "vr_caravan_pick.h"

namespace dxvk {

  int32_t vrNearestPoint(
    const VrVector2*  points,
          uint32_t    count,
    const VrVector2&  at,
          float       reach,
          int32_t     current,
          float       stickiness) {
    int32_t nearest  = -1;
    float   shortest = reach;

    for (uint32_t i = 0; i < count; i++) {
      float distance = std::hypot(points[i].x - at.x, points[i].y - at.y);

      if (int32_t(i) == current)
        distance -= stickiness;

      if (distance <= shortest) {
        shortest = distance;
        nearest  = int32_t(i);
      }
    }

    return nearest;
  }


  VrCaravanStep vrCaravanHandStep(int32_t target, int32_t current, int32_t size) {
    VrCaravanStep step;

    if (size <= 0 || target < 0 || target >= size || target == current)
      return step;

    step.selection = target > 0 ? target - 1 : size - 1;
    step.key       = VrCaravanKey::Right;
    return step;
  }


  VrCaravanStep vrCaravanSlotStep(uint32_t track, int32_t row) {
    VrCaravanStep step;
    step.selection = track < 3 ? row + 1 : row - 1;
    step.key       = VrCaravanKey::Up;
    return step;
  }


  VrCaravanKey vrCaravanTrackKey(int32_t target, int32_t current) {
    if (target > current)
      return VrCaravanKey::Right;

    if (target < current)
      return VrCaravanKey::Left;

    return VrCaravanKey::None;
  }


  bool vrIsCaravanRowModel(const char* name) {
    for (const char* side : { "Player_", "Opponent_" }) {
      size_t length = std::strlen(side);

      if (!std::strncmp(name, side, length)) {
        const char* rest = name + length;
        return rest[0] >= '1' && rest[0] <= '3' && rest[1] == '_'
            && rest[2] == '0' && rest[3] >= '1' && rest[3] <= '7' && !rest[4];
      }
    }

    return false;
  }

}
