#include "vr_terminal_keyboard.h"
#include "vr_text_entry.h"

namespace dxvk {

  namespace {

    /// Unicode code points of the Windows-1252 bytes 0x80 to 0x9F, 0 where
    /// the byte is not assigned
    constexpr uint16_t Windows1252High[32] = {
      0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
    };

    /// The game's byte for a code point, or 0 if it has none
    uint8_t gameCharacter(uint32_t codePoint) {
      if (codePoint < 0x80 || (codePoint >= 0xA0 && codePoint <= 0xFF))
        return uint8_t(codePoint);

      for (uint32_t i = 0; i < 32; i++) {
        if (Windows1252High[i] && Windows1252High[i] == codePoint)
          return uint8_t(0x80 + i);
      }

      return 0;
    }

    /// Reads the code point at \c pos and moves past it; a malformed
    /// sequence gives 0 and skips one byte
    uint32_t nextCodePoint(const std::string& utf8, size_t& pos) {
      uint8_t lead = uint8_t(utf8[pos++]);

      if (lead < 0x80)
        return lead;

      uint32_t extra;
      uint32_t codePoint;

      if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        codePoint = lead & 0x1F;
      } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        codePoint = lead & 0x0F;
      } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        codePoint = lead & 0x07;
      } else {
        return 0;
      }

      if (pos + extra > utf8.size())
        return 0;

      for (uint32_t i = 0; i < extra; i++) {
        uint8_t next = uint8_t(utf8[pos + i]);

        if ((next & 0xC0) != 0x80)
          return 0;

        codePoint = (codePoint << 6) | (next & 0x3F);
      }

      pos += extra;
      return codePoint;
    }

    void appendUtf8(std::string& utf8, uint32_t codePoint) {
      if (codePoint < 0x80) {
        utf8 += char(codePoint);
      } else if (codePoint < 0x800) {
        utf8 += char(0xC0 | (codePoint >> 6));
        utf8 += char(0x80 | (codePoint & 0x3F));
      } else {
        utf8 += char(0xE0 | (codePoint >> 12));
        utf8 += char(0x80 | ((codePoint >> 6) & 0x3F));
        utf8 += char(0x80 | (codePoint & 0x3F));
      }
    }

  }


  std::string vrGameTextFromUtf8(const std::string& utf8) {
    std::string text;
    size_t pos = 0;

    while (pos < utf8.size()) {
      uint8_t character = gameCharacter(nextCodePoint(utf8, pos));

      if (character >= 0x20 && character != '|' && character != 0x7F)
        text += char(character);
    }

    return text;
  }


  std::string vrUtf8FromGameText(const std::string& text) {
    std::string utf8;

    for (char c : text) {
      uint8_t byte = uint8_t(c);
      uint32_t codePoint = byte;

      if (byte >= 0x80 && byte < 0xA0)
        codePoint = Windows1252High[byte - 0x80];

      if (codePoint)
        appendUtf8(utf8, codePoint);
    }

    return utf8;
  }


  std::vector<uint32_t> vrTextEntryKeys(uint32_t length, const std::string& text) {
    std::vector<uint32_t> keys;
    keys.reserve(length + text.size() + 2);

    // End also clears the field's "first key replaces the text" state
    keys.push_back(VrConsoleKey::End);
    keys.insert(keys.end(), length, VrConsoleKey::Backspace);

    for (char c : text)
      keys.push_back(uint8_t(c));

    keys.push_back(VrConsoleKey::Enter);
    return keys;
  }


  VrTextEntryStep VrTextEntry::update(bool fieldOpen, bool keysPending, VrKeyboardEvent event, float dt) {
    if (!fieldOpen) {
      bool shown = m_state == State::Shown;
      m_state = State::Waiting;
      m_wait  = OpenDelay;
      return shown ? VrTextEntryStep::Hide : VrTextEntryStep::None;
    }

    switch (m_state) {
      case State::Shown:
        if (event == VrKeyboardEvent::Done) {
          m_state = State::Applying;
          return VrTextEntryStep::Apply;
        }

        if (event == VrKeyboardEvent::Closed) {
          m_state = State::Waiting;
          m_wait  = ReopenDelay;
        }

        return VrTextEntryStep::None;

      case State::Applying:
        if (!keysPending) {
          m_state = State::Waiting;
          m_wait  = OpenDelay;
        }

        return VrTextEntryStep::None;

      case State::Waiting:
        m_wait -= dt;

        if (m_wait > 0.0f)
          return VrTextEntryStep::None;

        m_state = State::Shown;
        return VrTextEntryStep::Show;

      case State::Refused:
        return VrTextEntryStep::None;
    }

    return VrTextEntryStep::None;
  }


  void VrTextEntry::refuse() {
    m_state = State::Refused;
  }

}
