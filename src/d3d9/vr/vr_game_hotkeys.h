#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "vr_game_addresses.h"

namespace dxvk {

  /**
   * \brief An item of the player's inventory, as the quick wheel shows it
   */
  struct VrHotKeyItem {
    bool           present  = false;
    std::string    name;
    uint32_t       count    = 0;
    /// Path of its icon, relative to Data\Textures
    std::string    icon;
    bool           worn     = false;
    /// Its form (TESForm*), compared but not dereferenced, and its type
    const uint8_t* form     = nullptr;
    uint8_t        formType = 0;
  };

  /**
   * \brief The player's hotkeys, through the game's own hotkey wheel
   *
   * See findings/quick-menu.md §1-3. The game keeps the hotkeys in the
   * inventory; the HUD's wheel equips them as the gamepad's D-pad does, and
   * the Pip-Boy inventory's wheel assigns them (the game's own gamepad
   * path). Slot 1 is the ammunition swap, never a hotkey.
   *
   * Only used on the game's main thread, outside of the game's update.
   */
  class VrGameHotKeys {

  public:

    /**
     * \brief Whether the game's functions are the expected ones
     *
     * Checked once, logged when they are not.
     */
    static bool available();

    /**
     * \brief Reads the item of every slot
     *
     * \param [out] items Items by slot; slot 1 is left empty
     */
    static void read(std::array<VrHotKeyItem, VrGame::HotKeySlotCount>& items);

    /**
     * \brief Equips, unequips or uses the item of a slot, as the game's
     *    hotkey wheel does
     *
     * The game toggles an item: one equipped is unequipped. It refuses to
     * change a weapon or armour while the player's anim action runs (an
     * attack, a reload...), which is refused here first.
     *
     * \param [in] slot The slot
     * \returns \c false if nothing was done
     */
    static bool use(uint32_t slot);

    /**
     * \brief Reads an inventory entry
     */
    static VrHotKeyItem describe(uint8_t* entry);

    /**
     * \brief Whether the Pip-Boy's inventory shows its hotkey wheel
     *
     * The game shows it while the gamepad's hotkey button (RB) is held on
     * an item that can be given a hotkey; the D-pad then gives the item
     * the slot of its direction.
     */
    static bool inventoryWheelShown();

    /**
     * \brief Keeps the HUD's hotkey wheel from reading the D-pad
     *
     * The VR controllers press the D-pad only in menus, to move through
     * lists. The frame a menu closes the game can still see that press
     * outside menu mode, and the HUD's wheel then equips or unequips the
     * hotkey of that direction when it is released. The quick wheel uses
     * the hotkeys on its own, so the HUD's wheel always sees the D-pad at
     * rest. Installed once, with the VR controllers acting as the gamepad.
     *
     * \returns \c false if the game's call was not the expected one
     */
    static bool muteHudGamepadWheel();

  };

}
