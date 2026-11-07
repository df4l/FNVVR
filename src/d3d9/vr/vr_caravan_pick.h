#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief A stick step Caravan's menu is asked to take
   */
  enum class VrCaravanKey : uint32_t {
    None,
    Left,
    Right,
    Up,
  };

  /**
   * \brief What to write in Caravan's menu so that one step of its own
   *    lands on a target
   *
   * The menu takes the step itself (sound, raised card, cursor, the check
   * of the placement): findings/caravan.md §3.3.
   */
  struct VrCaravanStep {
    /// Selection to write before the step: a hand card or a row
    int32_t      selection = 0;
    VrCaravanKey key       = VrCaravanKey::None;
  };

  /**
   * \brief Finds the point nearest to where the laser meets the table
   *
   * \param [in] points Points on the table, in its plane
   * \param [in] count Number of points
   * \param [in] at Where the laser meets the table
   * \param [in] reach Farthest a point may be from \c at
   * \param [in] current The point chosen so far, -1 if none: it keeps the
   *    laser until another is nearer by \c stickiness
   * \param [in] stickiness Distance the chosen point is favoured by
   * \returns Index of the point, -1 if none is within reach
   */
  int32_t vrNearestPoint(
    const VrVector2*  points,
          uint32_t    count,
    const VrVector2&  at,
          float       reach,
          int32_t     current,
          float       stickiness);

  /**
   * \brief The step from the card before the target to the target
   *
   * The menu steps right through its hand and wraps at the end.
   *
   * \param [in] target Hand card to select
   * \param [in] current Hand card selected now
   * \param [in] size Cards in the hand
   * \returns No key when nothing is to change
   */
  VrCaravanStep vrCaravanHandStep(int32_t target, int32_t current, int32_t size);

  /**
   * \brief The step from the row before the target to the target
   *
   * Up counts the player's rows down (towards the opponent) and the
   * opponent's rows up, and the track is kept.
   *
   * \param [in] track Track of the target, 0-2 the player's, 3-5 the opponent's
   * \param [in] row Row of the target
   */
  VrCaravanStep vrCaravanSlotStep(uint32_t track, int32_t row);

  /**
   * \brief The step towards a track while a whole track is discarded
   *
   * The menu moves one track per step, right towards the higher one.
   *
   * \param [in] target Track to select
   * \param [in] current Track selected now
   */
  VrCaravanKey vrCaravanTrackKey(int32_t target, int32_t current);

}
