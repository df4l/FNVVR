#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief Message boxes the mod shows on the main menu, one at a time
   *
   * Hooks the main menu's idle call once something has to be asked, so
   * nothing is patched otherwise. Prompts run in the order they were added,
   * the next one once the previous one has finished. See findings/laa.md.
   * Only used on the game's main thread.
   */
  class VrMainMenuPrompts {

  public:

    /**
     * \brief A prompt's step, called on every frame of the main menu
     *
     * \param [in] ready The menu has settled and has no message box
     * \returns \c false once the prompt has nothing more to show
     */
    using Prompt = bool (*)(bool ready);

    /**
     * \brief Adds a prompt
     *
     * \param [in] prompt The prompt's step
     * \returns \c false if the main menu was not found, the prompt never runs
     */
    static bool add(Prompt prompt);

    /**
     * \brief Opens a message box
     *
     * \param [in] text The text, which must outlive the message box
     * \param [in] callback Called when a button is clicked, may be null
     * \param [in] first The first button
     * \param [in] second The second button, may be null
     * \param [in] third The third button, may be null
     */
    static void show(
      const char*                 text,
            void                  (__cdecl* callback)(),
      const char*                 first,
      const char*                 second = nullptr,
      const char*                 third = nullptr);

    /**
     * \brief Reads which button was clicked, from a message box's callback
     *
     * \returns The button's index, from 0
     */
    static int32_t answer();

  };

}
