#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Converts text typed on the runtime's keyboard to the game's text
   *
   * The runtime's keyboard types UTF-8; the game's menus use Windows-1252.
   * Characters the game cannot hold are left out, and so are control
   * characters and \c |, which the game's text fields draw as their cursor.
   */
  std::string vrGameTextFromUtf8(const std::string& utf8);

  /**
   * \brief Converts the game's Windows-1252 text to UTF-8
   */
  std::string vrUtf8FromGameText(const std::string& text);

  /**
   * \brief Keys that replace the text of the game's text field and confirm it
   *
   * Goes to the end of the text, erases \c length characters, types
   * \c text (game text, see vrGameTextFromUtf8) and presses Enter. The keys
   * are characters or VrConsoleKey codes.
   *
   * \param [in] length Characters in the field
   * \param [in] text The new text
   */
  std::vector<uint32_t> vrTextEntryKeys(uint32_t length, const std::string& text);

  /**
   * \brief What the runtime's keyboard should do on a frame
   */
  enum class VrTextEntryStep : uint32_t {
    None,
    /// Show the keyboard with the field's text
    Show,
    /// Hide the keyboard: the game's text field closed
    Hide,
    /// Give the typed text to the game's text field
    Apply,
  };

  /**
   * \brief Keeps the runtime's keyboard shown while a game text field is open
   *
   * The keyboard opens shortly after the field does. Its text goes to the
   * field once it is confirmed; if the field is still open once the game
   * has taken it (the game refused the text), or if the keyboard is closed
   * without confirming, the keyboard opens again. A keyboard the runtime
   * cannot show is not asked for again until the next field.
   */
  class VrTextEntry {

  public:

    /// Seconds between a field opening and its keyboard
    constexpr static float OpenDelay   = 0.3f;

    /// Seconds before a keyboard closed without its text opens again
    constexpr static float ReopenDelay = 1.0f;

    /**
     * \brief Decides the step of a frame
     *
     * \param [in] fieldOpen Whether the game's text field is open
     * \param [in] keysPending Whether keys of an applied text are still
     *    waiting for the game
     * \param [in] event What happened to the keyboard since the last frame
     * \param [in] dt Duration of a frame in seconds
     */
    VrTextEntryStep update(bool fieldOpen, bool keysPending, VrKeyboardEvent event, float dt);

    /**
     * \brief Notes that the runtime could not show the keyboard
     */
    void refuse();

    /**
     * \brief Whether the keyboard is shown
     */
    bool shown() const {
      return m_state == State::Shown;
    }

  private:

    enum class State : uint32_t {
      Waiting,
      Shown,
      Applying,
      Refused,
    };

    State m_state = State::Waiting;
    float m_wait  = OpenDelay;

  };

}
