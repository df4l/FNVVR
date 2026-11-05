#pragma once

#include <functional>

#include "vr_options.h"
#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief Adds a "VR" page to the game's Settings menu
   *
   * The game builds each page of its Settings menu (Gameplay, Display,
   * Audio, Controls) from a list of option objects. The page added here
   * is built the same way: an entry "VR" in the Settings list, after
   * Controls, that shows the options carrying the page's mask. The page
   * looks and works like the game's own pages, including the cursor
   * driven by the controllers' lasers. See findings/options-menu.md.
   *
   * The page has five options: snap or smooth turning, the speed of
   * smooth turning, how large the Pip-Boy grows while it is open,
   * whether the weapons are reloaded with the hands, and whether the
   * player sneaks with the stick click or by crouching.
   * After the VR entry, a Console entry opens the game's console; it is
   * only in the pause menu's Settings list, not in the main menu's.
   *
   * Only used on the game's main thread.
   */
  class VrOptionsMenu {

  public:

    /// Called when the player changes an option, with all the options
    using ChangedFn = std::function<void(const VrOptions&)>;

    /// Called when the player chooses the Console entry
    using ConsoleFn = std::function<void()>;

    /**
     * \brief Hooks the creation of the start menu's options
     *
     * The page shows the options it is given and tells \c onChanged what
     * the player picks. Nothing is stored here: the caller owns the
     * options and decides what a change does.
     *
     * \param [in] options Current values of the options
     * \param [in] onChanged Called after each change
     * \param [in] texts The labels of the page, copied
     * \param [in] onConsole Called when the Console entry is chosen
     * \returns \c false if the executable is not the supported version
     */
    static bool install(const VrOptions& options, const VrTexts& texts, ChangedFn onChanged, ConsoleFn onConsole);

  };

}
