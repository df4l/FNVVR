#pragma once

#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief Offers to turn off yUI's Loot Menu
   *
   * The Loot Menu takes over the activation of containers and corpses and
   * reads the keyboard and the real gamepad, not the controllers. With
   * ShowOff it then refuses the activation, so they no longer open, and it
   * hides the interaction prompt on them. Nothing the mod can do from outside
   * lets it see the controllers. See findings/nvse-compat.md §5.6.
   *
   * When yUI is loaded with its Loot Menu on, the main menu asks the player.
   * If the player accepts, \c bLootMenu is set to 0 in yUI's settings file,
   * which yUI reads again when the first game is loaded. The player can also
   * decline for this run only, or for good, which writes
   * \c fnvvr\\no_yui_loot_menu_prompt.txt next to d3d9.dll. Without yUI nothing
   * happens. Only used on the game's main thread.
   */
  class VrYuiLootMenu {

  public:

    /**
     * \brief Adds the prompt if it has to be shown
     *
     * \param [in] texts The prompt's texts, which must outlive the game's menus
     * \returns \c true if the main menu will show the prompt
     */
    static bool install(const VrTexts& texts);

  };

}
