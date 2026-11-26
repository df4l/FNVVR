#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "vr_game_hotkeys.h"
#include "vr_game_images.h"
#include "vr_paint_panel.h"
#include "vr_quick_wheel_image.h"

namespace dxvk {

  /**
   * \brief What the hands do with the quick wheel in a frame
   *
   * Poses in tracking space.
   */
  struct VrQuickWheelInput {
    /// The wheel's control is held: the right stick pushed down
    bool             held = false;
    /// The head and the right controller, or \c nullptr if not tracked
    const VrPose*    head = nullptr;
    const VrPose*    hand = nullptr;
    /// The ammunition is changed on the magazine (VrManualReload): the
    /// ammunition cell only shows it
    bool             manualReload = false;
  };

  enum class VrQuickWheelEvent {
    None,
    /// The wheel opened
    Opened,
    /// Another cell is picked
    Picked,
    /// The wheel closed on a cell, which was used
    Used,
    /// The wheel closed on a cell the game refused, or on an empty one
    Failed,
    /// The wheel closed with no cell picked
    Closed,
  };

  /**
   * \brief The quick wheel of hotkeys, at the right hand
   *
   * See findings/quick-menu.md §6. Holding the wheel's control shows a
   * ring of eight cells where the right hand is, facing the head: the
   * seven hotkeys and, at the top, the ammunition. Moving the hand
   * towards a cell picks it; letting go of the control uses it (the game's
   * hotkey wheel equips, unequips or uses the item; the ammunition cell
   * swaps to the next type held, unless the ammunition is changed on the
   * magazine). Letting go with no cell picked does nothing. Hotkeys are
   * given in the Pip-Boy's inventory with the game's own gamepad wheel.
   *
   * A weapon is drawn when it is chosen: picking the weapon already
   * equipped draws it if it is holstered instead of unequipping it.
   *
   * Only used on the game's main thread, outside of the game's update.
   */
  class VrQuickWheel {

  public:

    VrQuickWheel(VrPainter& painter, IVRBackend& backend, VrGameImages& images);

    VrQuickWheelEvent update(const VrQuickWheelInput& input);

    /**
     * \brief Closes the wheel at once, using nothing
     */
    void close();

    bool isOpen() const {
      return m_open;
    }

    /**
     * \brief Takes the items consumed by hand
     *
     * \param [in] consume Called with an item chosen on the wheel before
     *    the game uses it; returns \c true if it took the item, which is not
     *    used then
     */
    void setConsumer(std::function<bool (uint32_t slot, const VrHotKeyItem& item)> consume) {
      m_consume = std::move(consume);
    }

    /**
     * \brief What the last closing on a cell asked of the weapon
     */
    bool drawsWeapon() const {
      return m_drawsWeapon;
    }

  private:

    VrPaintPanel      m_panel;
    VrPaintList       m_list;
    VrGameImages&     m_images;

    bool              m_open        = false;
    bool              m_held        = false;
    bool              m_failed      = false;
    bool              m_drawsWeapon = false;

    std::function<bool (uint32_t, const VrHotKeyItem&)> m_consume;

    // Where the wheel stands, and the hotkeys it shows
    VrPose            m_pose;
    std::array<VrHotKeyItem, VrGame::HotKeySlotCount> m_items;
    VrQuickWheelView  m_view;
    bool              m_drawn = false;

    // The ammunition cell swaps the ammunition
    bool              m_swapsAmmo = false;

    void open(const VrQuickWheelInput& input);

    void fillView(const VrQuickWheelInput& input);

    VrQuickWheelEvent choose(int32_t cell);

    void show();

  };

}
