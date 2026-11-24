#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dxvk {

  /**
   * \brief The game's text field, typed in without a keyboard
   *
   * The field the game asks the player's name with (TextEditMenu). Keys
   * are given to it where the interface reads the keyboard, one per frame,
   * through the call VrGameConsole redirects. See findings/name-entry.md.
   *
   * Only used on the game's main thread.
   */
  class VrGameTextEdit {

  public:

    /**
     * \brief Checks that the field can be read and typed in
     *
     * Needs the console's key input (VrGameConsole::install).
     *
     * \returns \c false if the executable is not the supported version
     */
    static bool install();

    /**
     * \brief Whether the field is open and takes keys
     */
    static bool isOpen();

    /**
     * \brief Reads the question and the text of the open field
     *
     * Both are Windows-1252, as the game holds them.
     */
    static void read(std::string& title, std::string& text);

    /**
     * \brief Queues keys for the field
     *
     * \param [in] keys Characters, or VrConsoleKey codes
     */
    static void pressKeys(const std::vector<uint32_t>& keys);

    /**
     * \brief Whether queued keys are still waiting for the game
     */
    static bool keysPending();

    /**
     * \brief Takes the next queued key for the interface
     *
     * Called where the interface reads its keys, when no real key was
     * typed and the console is closed. Keys are dropped once the field is
     * closed.
     *
     * \returns The key, or 0 if none
     */
    static uint32_t takeKey();

  };

}
