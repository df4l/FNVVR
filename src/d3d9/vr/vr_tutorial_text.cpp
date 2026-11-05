#include <cstdlib>

#include "vr_tutorial_text.h"

namespace dxvk {

  namespace {

    // In the order of the game's psUserActions table
    constexpr const char* ActionNames[] = {
      "Forward", "Back", "Sldleft", "Sldright", "Use", "Activate", "Block",
      "Rdyitem", "Crouch", "Run", "Togglerun", "Automove", "Jump", "Togglepov",
      "Menumode", "Restmenu", "Vats", "HotKey1", "AmmoSwap", "HotKey3", "HotKey4",
      "HotKey5", "HotKey6", "HotKey7", "HotKey8", "Quicksave", "Quickload", "Grab",
    };

    struct TextSource {
      uint32_t message;
      uint32_t source;
    };

    constexpr TextSource TextSources[] = {
      { 0x0006D914, 0x000BB215 }, // HelpGeneral
      { 0x0000017D, 0x00000183 }, // HelpVATS
      { 0x000B5ED1, 0x0006D913 }, // HelpVATS2
      { 0x0000017C, 0x00000184 }, // HelpLockpicking
      { 0x0000018C, 0x0000018D }, // HelpAmmo
      { 0x0007234C, 0x000734EE }, // HelpHotkeys
      { 0x00072358, 0x00072359 }, // HelpHealingLimbs
      { 0x000ABF9A, 0x000ABF9B }, // CGTutorialAccessHelpPC
      { 0x0007E126, 0x00064399 }, // CGTutorialMovement
    };

    // Windows-1252's characters at 0x80 to 0x9F, 0 where it has none
    constexpr uint16_t HighControls[32] = {
      0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
    };

    char toGameCharacter(uint32_t character) {
      if (character < 0x80 || (character >= 0xA0 && character <= 0xFF))
        return char(character);

      for (uint32_t i = 0; i < 32; i++) {
        if (HighControls[i] == character)
          return char(0x80 + i);
      }

      return '?';
    }

    int actionIndex(const std::string& name) {
      int index = 0;

      for (const char* action : ActionNames) {
        if (name == action)
          return index;

        index++;
      }

      return -1;
    }

    std::string trimmed(const std::string& text) {
      constexpr const char* Blank = " \t\r\n";
      size_t first = text.find_first_not_of(Blank);

      if (first == std::string::npos)
        return std::string();

      return text.substr(first, text.find_last_not_of(Blank) - first + 1);
    }

  }


  void VrTutorialTexts::parse(const std::string& text) {
    enum class Section { None, Message, Control, Tip };

    Section section = Section::None;
    uint32_t key = 0;
    std::string tipName;
    std::string body;

    auto finish = [&] {
      std::string value = vrToGameEncoding(trimmed(body));
      body.clear();

      if (section == Section::Message && !value.empty())
        m_messages[key] = std::move(value);
      else if (section == Section::Control && !value.empty() && value.size() <= MaxControlName)
        m_controls[key] = std::move(value);
      else if (section == Section::Tip) {
        size_t end = value.find("\r\n");

        if (end != std::string::npos)
          m_tips[tipName] = { value.substr(0, end), trimmed(value.substr(end)) };
      }
    };

    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") ? 0 : 3;

    while (start < text.size()) {
      size_t end = text.find('\n', start);

      if (end == std::string::npos)
        end = text.size();

      std::string line = text.substr(start, end - start);
      start = end + 1;

      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      if (!line.empty() && line[0] == '#')
        continue;

      if (line.size() > 2 && line[0] == '[' && line.back() == ']') {
        finish();

        std::string header = line.substr(1, line.size() - 2);
        section = Section::None;

        if (!header.compare(0, 8, "message ")) {
          char* last = nullptr;
          unsigned long form = std::strtoul(header.c_str() + 8, &last, 16);

          if (last && !*last && last != header.c_str() + 8) {
            section = Section::Message;
            key = uint32_t(form);
          }
        } else if (!header.compare(0, 8, "control ")) {
          int action = actionIndex(header.substr(8));

          if (action >= 0) {
            section = Section::Control;
            key = uint32_t(action);
          }
        } else if (!header.compare(0, 4, "tip ")) {
          section = Section::Tip;
          tipName = header.substr(4);
        }

        continue;
      }

      // The game's own texts end their lines this way
      body += line;
      body += "\r\n";
    }

    finish();
  }


  const std::string* VrTutorialTexts::message(uint32_t formId) const {
    auto entry = m_messages.find(formId);
    return entry != m_messages.end() ? &entry->second : nullptr;
  }


  const std::string* VrTutorialTexts::control(uint32_t action) const {
    auto entry = m_controls.find(action);
    return entry != m_controls.end() ? &entry->second : nullptr;
  }


  const VrTipText* VrTutorialTexts::tip(const std::string& name) const {
    auto entry = m_tips.find(name);
    return entry != m_tips.end() ? &entry->second : nullptr;
  }


  uint32_t vrTutorialTextSource(uint32_t formId) {
    for (const TextSource& entry : TextSources) {
      if (entry.message == formId)
        return entry.source;
    }

    return formId;
  }


  std::string vrToGameEncoding(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ) {
      uint8_t lead = uint8_t(text[i]);
      uint32_t character = lead;
      size_t extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3 : size_t(-1);

      if (extra == size_t(-1) || i + extra >= text.size()) {
        result += '?';
        i += 1;
        continue;
      }

      if (extra) {
        character = lead & (0x3F >> extra);

        for (size_t n = 1; n <= extra; n++)
          character = (character << 6) | (uint8_t(text[i + n]) & 0x3F);
      }

      result += toGameCharacter(character);
      i += extra + 1;
    }

    return result;
  }

}
