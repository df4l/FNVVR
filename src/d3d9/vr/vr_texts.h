#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace dxvk {

  /**
   * \brief The texts shown to the player, read from text files
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
   *   [text option.reload]
   *   Manual reload
   *
   * Reading a second file replaces the texts it holds and keeps the others,
   * so a partial translation falls back on the first file. The mod's own
   * labels have their English in the code, for when no file holds them.
   */
  class VrTexts {

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
     * \brief One of the mod's own texts
     *
     * The pointer stays valid as long as this object, unless a file is parsed.
     *
     * \param [in] name The text's name
     * \param [in] english What to show if no file holds it
     * \returns The text in the game's encoding
     */
    const char* text(const char* name, const char* english) const;

    size_t messageCount() const {
      return m_messages.size();
    }

    size_t controlCount() const {
      return m_controls.size();
    }

    size_t textCount() const {
      return m_texts.size();
    }

  private:

    std::map<uint32_t, std::string> m_messages;
    std::map<uint32_t, std::string> m_controls;
    std::map<std::string, std::string> m_texts;

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
   * \brief Checks the name of one of the game's string settings
   *
   * Reading a string from an INI file moves the setting's name to a new
   * block and turns its first letter into a capital 'S', so sLanguage:General
   * then reads SLanguage:General.
   *
   * \param [in] name The setting's name
   * \param [in] expected The name the game gave it, starting with 's'
   * \returns \c true if it is that setting, read from a file or not
   */
  bool vrIsStringSettingName(const char* name, const char* expected);


  /**
   * \brief Converts UTF-8 to the Windows-1252 encoding of the game's text
   *
   * \param [in] text The text in UTF-8
   * \returns The text in Windows-1252, '?' for what it cannot hold
   */
  std::string vrToGameEncoding(const std::string& text);

}
