#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace dxvk {

  /**
   * \brief A pop-up of the mod, in the game's encoding
   */
  struct VrTipText {
    std::string title;
    std::string body;
  };


  /**
   * \brief The VR wording of the game's tutorials, read from text files
   *
   * A file holds sections, one per text, in UTF-8:
   *
   *   # A comment
   *   [control Vats]
   *   the right stick (click)
   *
   *   [message 0x183]
   *   The whole text of the message,
   *   over as many lines as it needs.
   *
   *   [tip welcome]
   *   The title of a pop-up of the mod
   *   Its text, over as many lines as it needs.
   *
   * Reading a second file replaces the texts it holds and keeps the others,
   * so a partial translation falls back on the first file.
   */
  class VrTutorialTexts {

  public:

    /** Longest name of a control: the game copies it without a size */
    static constexpr size_t MaxControlName = 63;

    /**
     * \brief Reads the sections of a file
     *
     * \param [in] text The file's text, in UTF-8
     */
    void parse(const std::string& text);

    /**
     * \brief The text of a message
     *
     * \param [in] formId The message's form
     * \returns The text in the game's encoding, or \c nullptr if it has none
     */
    const std::string* message(uint32_t formId) const;

    /**
     * \brief The name of a control
     *
     * \param [in] action Index of the game's user action
     * \returns The name in the game's encoding, or \c nullptr if it has none
     */
    const std::string* control(uint32_t action) const;

    /**
     * \brief The text of a pop-up
     *
     * \param [in] name The pop-up's name
     * \returns Its text, or \c nullptr if it has none
     */
    const VrTipText* tip(const std::string& name) const;

    size_t messageCount() const {
      return m_messages.size();
    }

    size_t controlCount() const {
      return m_controls.size();
    }

  private:

    std::map<uint32_t, std::string> m_messages;
    std::map<uint32_t, std::string> m_controls;
    std::map<std::string, VrTipText> m_tips;

  };


  /**
   * \brief The message whose text stands for another one
   *
   * The game has a keyboard and a gamepad version of some messages and
   * chooses with a flag that depends on what the player last used. The
   * controllers read the same in both cases, so the keyboard version shows
   * the gamepad version's text.
   *
   * \param [in] formId The message the game asks for
   * \returns The message to read, \p formId if it stands for itself
   */
  uint32_t vrTutorialTextSource(uint32_t formId);


  /**
   * \brief Converts UTF-8 to the Windows-1252 encoding of the game's text
   *
   * \param [in] text The text in UTF-8
   * \returns The text in Windows-1252, '?' for what it cannot hold
   */
  std::string vrToGameEncoding(const std::string& text);

}
