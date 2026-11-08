#include <cfloat>
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


  float vrRayMeetsCard(
    const VrVector3&      origin,
    const VrVector3&      direction,
    const VrCaravanCard&  card) {
    if (card.scale <= 0.0f)
      return -1.0f;

    // Into the card's own space: the transposed rotation, then the scale
    const auto& r = card.rotate;
    VrVector3 f = { origin.x - card.translate.x, origin.y - card.translate.y, origin.z - card.translate.z };

    auto local = [&r, &card] (const VrVector3& v) {
      return VrVector3 {
        (r[0][0] * v.x + r[1][0] * v.y + r[2][0] * v.z) / card.scale,
        (r[0][1] * v.x + r[1][1] * v.y + r[2][1] * v.z) / card.scale,
        (r[0][2] * v.x + r[1][2] * v.y + r[2][2] * v.z) / card.scale };
    };

    VrVector3 o = local(f);
    VrVector3 d = local(direction);

    if (std::abs(d.z) < 1e-6f)
      return -1.0f;

    float t = -o.z / d.z;

    if (t <= 0.0f)
      return -1.0f;

    float x = o.x + t * d.x;
    float y = o.y + t * d.y;

    return std::abs(x) <= card.halfX && std::abs(y) <= card.halfY ? t : -1.0f;
  }


  int32_t vrFirstCardOnRay(
    const VrCaravanCard*  cards,
          uint32_t        count,
    const VrVector3&      origin,
    const VrVector3&      direction) {
    int32_t first   = -1;
    float   nearest = FLT_MAX;

    for (uint32_t i = 0; i < count; i++) {
      float t = vrRayMeetsCard(origin, direction, cards[i]);

      if (t >= 0.0f && t < nearest) {
        nearest = t;
        first   = int32_t(i);
      }
    }

    return first;
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



  VrCaravanCommand VrCaravanButtons::update(bool back, bool option, bool choosesSlot, float dt) {
    VrCaravanCommand command = VrCaravanCommand::None;

    if (option && !m_option)
      command = VrCaravanCommand::Cancel;

    m_option = option;

    if (back) {
      if (!m_back) {
        m_backHeld = 0.0f;
        m_backUsed = false;

        if (choosesSlot) {
          m_backUsed = true;
          command    = VrCaravanCommand::Cancel;
        }
      } else if (!m_backUsed) {
        m_backHeld += dt;

        if (m_backHeld >= HoldTime) {
          m_backUsed = true;
          command    = choosesSlot ? VrCaravanCommand::None : VrCaravanCommand::DiscardTrack;
        }
      }
    } else if (m_back && !m_backUsed && !choosesSlot) {
      command = VrCaravanCommand::DiscardCard;
    }

    m_back = back;
    return command;
  }

}
