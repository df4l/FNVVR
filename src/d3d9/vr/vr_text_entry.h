#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Converts the game's Windows-1252 text to UTF-8
   */
  std::string vrUtf8FromGameText(const std::string& text);

  /**
   * \brief Keys for the game's text field from the runtime keyboard's input
   *
   * The runtime's keyboard types UTF-8; the game's menus use Windows-1252.
   * \c '\b' and DEL become Backspace, \c '\n' and \c '\r' Enter. Characters
   * the game cannot hold are left out, and so are other control characters
   * and \c |, which the game's text fields draw as their cursor. The keys are
   * characters or VrConsoleKey codes.
   *
   * \param [in] typed What the keyboard sent, UTF-8
   */
  std::vector<uint32_t> vrTextEntryKeys(const std::string& typed);

  /**
   * \brief What the runtime's keyboard should do on a frame
   */
  enum class VrTextEntryStep : uint32_t {
    None,
    /// Show the keyboard with the field's text
    Show,
    /// Hide the keyboard: the game's text field closed
    Hide,
    /// Confirm the game's text field, as Enter does
    Confirm,
  };

  /**
   * \brief Keeps the runtime's keyboard shown while a game text field is open
   *
   * The keyboard opens shortly after the field does, and types in it while
   * it is shown. Confirming it confirms the field; if the field is still
   * open once the game has taken the keys (the game refused the text), or
   * if the keyboard is closed without confirming, the keyboard opens again. A keyboard the runtime
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
     * \param [in] keysPending Whether typed keys are still waiting for the
     *    game
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
