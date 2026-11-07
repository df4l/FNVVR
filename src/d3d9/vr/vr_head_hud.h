#pragma once

#include <array>
#include <cstdint>

#include "vr_hand_hud.h"

namespace dxvk {

  /**
   * \brief Part of the HUD image copied into the head panel's image
   */
  struct VrHudCopy {
    VrInterfaceRect source;
    VrInterfaceRect target;
  };

  /**
   * \brief How the HUD image is rearranged into the head panel's image
   */
  struct VrHeadHudLayout {
    std::array<VrHudCopy, 3> copies;
    uint32_t                 count = 0;
  };

  /**
   * \brief Height of the radiation meter, in interface units
   *
   * Height of its bracket image (radiation_bracket in hud_main_menu.xml),
   * which holds the bar, its pointer and the rads per second. The XP meter
   * is placed this far below it. See findings/hud.md §12.
   */
  constexpr float VrRadiationMeterHeight = 120.0f;

  /**
   * \brief Crop of the radiation meter in the HUD image
   *
   * The bracket image is wider than the group and reaches past the right
   * side of the screen, so the crop does too.
   *
   * \param [in] group Rectangle of the RadiationMeter group, in interface units
   * \param [in] size Size of the interface, in interface units
   */
  VrInterfaceRect vrComputeRadiationMeterCrop(
    const VrInterfaceRect&      group,
    const VrVector2&            size);

  /**
   * \brief Crop of the XP meter in the HUD image
   *
   * The XPMeter group has no size of its own. Its bracket image, 256 units
   * high, starts at the group's position, and its texts, the "LEVEL UP"
   * line included, lie inside it. The bracket is wider than the space the
   * game leaves on the right, so the crop reaches the right side of the
   * screen. See findings/hud.md §11.
   *
   * \param [in] group Rectangle of the XPMeter group, in interface units
   * \param [in] size Size of the interface, in interface units
   */
  VrInterfaceRect vrComputeXpMeterCrop(
    const VrInterfaceRect&      group,
    const VrVector2&            size);

  /**
   * \brief Crop of the column of meters in the top right corner
   *
   * Covers the crops of the meters placed there.
   *
   * \param [in] radiation Rectangle of the RadiationMeter group in
   *    interface units, or \c nullptr if it was not placed there
   * \param [in] xp Rectangle of the XPMeter group in interface units, or
   *    \c nullptr if it was not placed there
   * \param [in] size Size of the interface, in interface units
   * \param [out] crop The column, in interface units
   * \returns \c false if neither meter was placed there
   */
  bool vrComputeHudMetersCrop(
    const VrInterfaceRect*      radiation,
    const VrInterfaceRect*      xp,
    const VrVector2&            size,
          VrInterfaceRect&      crop);

  /**
   * \brief Right side of the messages and objectives in the top left corner
   *
   * The text of a message starts 130 units into the Messages group and
   * wraps at 320 units, so it ends inside the group's width; the
   * objectives stay inside the QuestReminder group. See findings/hud.md §14.
   *
   * \param [in] messages Rectangle of the Messages group in interface
   *    units, or \c nullptr if it is unknown
   * \param [in] objectives Rectangle of the QuestReminder group in
   *    interface units, or \c nullptr if it is unknown
   * \returns The right side with a small gap, in interface units, or 0 if
   *    neither group is known
   */
  float vrComputeHudCornerRight(
    const VrInterfaceRect*      messages,
    const VrInterfaceRect*      objectives);

  /**
   * \brief Rearranges the HUD image for the panel in front of the head
   *
   * The messages and objectives are in the top left corner and the
   * subtitles at the bottom, in the centre. Seen from the head, a corner
   * is too far to the side to read comfortably, so the upper half of the
   * image moves towards the centre by \c shift and the lower half stays.
   * The radiation and XP meters, placed by the VR layer in a column in the
   * top right corner (see VrHudMeters), move towards the centre from the
   * other side, by the same distance at most: they stop where the moved
   * messages and objectives end, since a copy replaces what it covers. If
   * a long text, such as a quest's name, leaves less room than the shift,
   * the meters stay and the upper half moves only as far as the room
   * allows, so the text is not cut. See findings/hud.md §16.
   *
   * \param [in] size Size of the image, in pixels
   * \param [in] shift How far the corners move towards the centre, in pixels
   * \param [in] meters Crop of the meters' column in pixels, or \c nullptr
   *    if neither is shown in the top right corner
   * \param [in] cornerRight Right side of the messages and objectives
   *    before the move, in pixels, or 0 if unknown
   */
  VrHeadHudLayout vrComputeHeadHudLayout(
    const VrVector2&            size,
          float                 shift,
    const VrInterfaceRect*      meters,
          float                 cornerRight);

}
