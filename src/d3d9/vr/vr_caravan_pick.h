#pragma once

#include <array>
#include <cstdint>

#include "vr_menu_pointer.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The values of Caravan's six tracks, where they lie on the mat
   */
  struct VrCaravanBoard {
    constexpr static uint32_t Tracks      = 6;
    constexpr static uint32_t ImageSize   = 512;
    /// A track's value over this has bust
    constexpr static int32_t  MostValue   = 26;

    /// The square of the mat the values are drawn on, facing up
    VrPanelPlacement placement;
    /// Value of each track, -1 for a track without a card
    std::array<int32_t, Tracks>   values = { -1, -1, -1, -1, -1, -1 };
    /// Centre of each value in the image, (0, 0) top left, (1, 1) bottom right
    std::array<VrVector2, Tracks> spots  = { };
  };

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

  /**
   * \brief Checks whether a name is the model of one row of a track
   *
   * The game loads one model per track and row, Player_{track}_0{row} and
   * Opponent_{track}_0{row} (tracks 1 to 3, rows 1 to 7).
   */
  bool vrIsCaravanRowModel(const char* name);

}
