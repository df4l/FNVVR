#include <cfloat>
#include <cmath>
#include <cstring>

#include "vr_caravan_pick.h"
#include "vr_math.h"

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


  VrCaravanTransform vrCaravanFanInHand(
    const VrVector3&      centre,
    const VrVector3&      hold,
    const VrVector3&      up,
    const VrVector3&      face,
          float           scale) {
    VrVector3 u = up * (1.0f / vrLength(up));

    // The faces turn as far towards their direction as the tips let them
    VrVector3 n = face - u * vrDot(face, u);

    if (vrLength(n) < 1e-4f)
      n = std::abs(u.z) < 0.9f ? VrVector3 { 0.0f, 0.0f, 1.0f } - u * u.z : VrVector3 { 0.0f, 1.0f, 0.0f } - u * u.y;

    n = n * (1.0f / vrLength(n));

    VrVector3 r = {
      u.y * n.z - u.z * n.y,
      u.z * n.x - u.x * n.z,
      u.x * n.y - u.y * n.x };

    VrCaravanTransform fan;
    const VrVector3* axes[3] = { &r, &u, &n };

    for (uint32_t column = 0; column < 3; column++) {
      fan.rotate[0][column] = axes[column]->x;
      fan.rotate[1][column] = axes[column]->y;
      fan.rotate[2][column] = axes[column]->z;
    }

    fan.scale     = scale;
    fan.translate = { 0.0f, 0.0f, 0.0f };
    fan.translate = hold - vrCaravanTransformPoint(fan, centre);
    return fan;
  }


  VrCaravanTransform vrCaravanSpreadCard(
    const VrCaravanTransform& card,
    const VrVector3&      pivot,
          float           middle,
          float           factor) {
    float angle = std::atan2(-card.rotate[0][0], card.rotate[1][0]);
    float turn  = (factor - 1.0f) * (angle - middle);
    float c = std::cos(turn);
    float s = std::sin(turn);

    VrCaravanTransform spread;
    spread.rotate[0][0] = c;  spread.rotate[0][1] = -s;
    spread.rotate[1][0] = s;  spread.rotate[1][1] =  c;
    spread.translate = pivot - vrCaravanTransformPoint(spread, pivot);
    return spread;
  }


  VrVector3 vrCaravanTransformPoint(const VrCaravanTransform& transform, const VrVector3& point) {
    const auto& r = transform.rotate;
    VrVector3 p = point * transform.scale;

    return VrVector3 {
      r[0][0] * p.x + r[0][1] * p.y + r[0][2] * p.z + transform.translate.x,
      r[1][0] * p.x + r[1][1] * p.y + r[1][2] * p.z + transform.translate.y,
      r[2][0] * p.x + r[2][1] * p.y + r[2][2] * p.z + transform.translate.z };
  }


  VrCaravanTransform vrCaravanCompose(const VrCaravanTransform& outer, const VrCaravanTransform& inner) {
    VrCaravanTransform result;

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++) {
        result.rotate[i][j] = outer.rotate[i][0] * inner.rotate[0][j]
                            + outer.rotate[i][1] * inner.rotate[1][j]
                            + outer.rotate[i][2] * inner.rotate[2][j];
      }
    }

    result.translate = vrCaravanTransformPoint(outer, inner.translate);
    result.scale     = outer.scale * inner.scale;
    return result;
  }


  VrCaravanTransform vrCaravanInverse(const VrCaravanTransform& transform) {
    VrCaravanTransform inverse;

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++)
        inverse.rotate[i][j] = transform.rotate[j][i];
    }

    inverse.scale     = 1.0f / transform.scale;
    inverse.translate = { 0.0f, 0.0f, 0.0f };
    inverse.translate = vrCaravanTransformPoint(inverse, transform.translate) * -1.0f;
    return inverse;
  }


  VrCaravanTransform vrCaravanOpponentCard(
    const VrVector3&      hold,
    const VrVector3&      up,
    const VrVector3&      face,
          uint32_t        index,
          uint32_t        count,
          float           spread,
          float           reach,
          float           layer,
          float           scale) {
    // The middle card's frame, as the player's fan is held
    VrCaravanTransform fan = vrCaravanFanInHand(VrVector3 { 0.0f, 0.0f, 0.0f }, hold, up, face, 1.0f);
    VrVector3 tip  = { fan.rotate[0][1], fan.rotate[1][1], fan.rotate[2][1] };
    VrVector3 side = { fan.rotate[0][0], fan.rotate[1][0], fan.rotate[2][0] };
    VrVector3 back = { fan.rotate[0][2], fan.rotate[1][2], fan.rotate[2][2] };

    float angle = spread * (float(index) - 0.5f * float(count - 1));
    VrVector3 x = tip * std::cos(angle) + side * std::sin(angle);
    VrVector3 y = {
      back.y * x.z - back.z * x.y,
      back.z * x.x - back.x * x.z,
      back.x * x.y - back.y * x.x };

    VrCaravanTransform card;
    const VrVector3* axes[3] = { &x, &y, &back };

    for (uint32_t column = 0; column < 3; column++) {
      card.rotate[0][column] = axes[column]->x;
      card.rotate[1][column] = axes[column]->y;
      card.rotate[2][column] = axes[column]->z;
    }

    card.translate = hold + x * reach + back * (layer * float(index));
    card.scale     = scale;
    return card;
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


  uint32_t vrCaravanColumnNode(uint32_t track, uint32_t column) {
    return track < 3 ? column + 1 : 4 - column;
  }


  bool vrCaravanCardInGame(
          uint32_t          track,
          uint32_t          row,
          uint32_t          column,
          uint32_t          cardsOnRow,
    const VrCaravanPlacing& placing) {
    return column < cardsOnRow
      || (int32_t(track) == placing.track && int32_t(row) == placing.row && int32_t(column) == placing.column);
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


  bool VrCaravanDeckPicker::click(VrCaravanDeckRow row, int32_t slot, int32_t current, int32_t total) {
    int32_t card = current + slot - CurrentSlot;

    if (slot < 0 || slot >= Slots || card < 0 || card >= total)
      return false;

    m_target  = card;
    m_row     = row;
    m_last    = current;
    m_stalled = 0.0f;
    return true;
  }


  VrCaravanDeckAction VrCaravanDeckPicker::update(int32_t current, bool inDeck, bool idle, float dt) {
    VrCaravanDeckAction action;

    if (m_target < 0)
      return action;

    if (current != m_last) {
      m_last    = current;
      m_stalled = 0.0f;
    } else {
      m_stalled += dt;
    }

    if (m_stalled > StallTime) {
      reset();
      return action;
    }

    if (current != m_target) {
      action.stick = m_target > current ? 1 : -1;
      return action;
    }

    // The last step's animation ends before the menu takes a button
    if (!idle)
      return action;

    action.add    = m_row == VrCaravanDeckRow::Available && !inDeck;
    action.remove = m_row == VrCaravanDeckRow::Deck && inDeck;
    reset();
    return action;
  }

}
