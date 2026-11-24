#include <algorithm>
#include <cstring>
#include <deque>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_console.h"
#include "vr_game_memory.h"
#include "vr_game_text_edit.h"

namespace dxvk {

  namespace {

    using TileGetValueFn = uintptr_t (__thiscall*)(uintptr_t tile, uint32_t trait);

    // Offset of a tile value's string, see VrGame::TileGetValue
    constexpr uintptr_t TileValueString = 0x0C;

    // Characters read from the field at most
    constexpr size_t MaxTextLength = 0x400;

    // Keys a text needs at most: the erased and the typed characters
    constexpr size_t MaxQueuedKeys = 2 * MaxTextLength + 2;

    bool                 g_installed = false;
    std::deque<uint32_t> g_keys;

    template<typename T>
    T peek(uintptr_t address) {
      T value;
      std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
      return value;
    }

    uintptr_t menu() {
      return peek<uintptr_t>(VrGame::TextEditGlobal);
    }

    std::string readTileText(uintptr_t tile) {
      if (!tile)
        return std::string();

      uintptr_t value = reinterpret_cast<TileGetValueFn>(VrGame::TileGetValue)(tile, VrGame::TileTraitString);
      const char* text = value ? peek<const char*>(value + TileValueString) : nullptr;

      return text ? std::string(text, strnlen(text, MaxTextLength)) : std::string();
    }

    // BSStringT: the characters, then a 16-bit length
    std::string readString(uintptr_t string) {
      auto text   = peek<const char*>(string);
      auto length = peek<uint16_t>(string + sizeof(const char*));

      if (!text)
        return std::string();

      return std::string(text, strnlen(text, std::min<size_t>(length, MaxTextLength)));
    }

  }


  bool VrGameTextEdit::install() {
    if (g_installed)
      return true;

    g_installed = VrGameConsole::install()
      && VrGameMemory::readable(VrGame::TextEditGlobal, sizeof(uintptr_t))
      && VrGameMemory::matches(VrGame::TileGetValue,
           VrGame::TileGetValuePrologue, sizeof(VrGame::TileGetValuePrologue));

    if (!g_installed)
      Logger::warn("VR: The game's text field was not found, the player's name cannot be typed from the headset");

    return g_installed;
  }


  bool VrGameTextEdit::isOpen() {
    uintptr_t field = g_installed ? menu() : 0;
    return field && peek<uint8_t>(field + VrGame::TextEditActive);
  }


  void VrGameTextEdit::read(std::string& title, std::string& text) {
    title.clear();
    text.clear();

    if (!isOpen())
      return;

    uintptr_t field = menu();
    title = readTileText(peek<uintptr_t>(field + VrGame::TextEditTitle));
    text  = readString(field + VrGame::TextEditText);
  }


  void VrGameTextEdit::pressKeys(const std::vector<uint32_t>& keys) {
    for (uint32_t key : keys) {
      if (g_keys.size() < MaxQueuedKeys)
        g_keys.push_back(key);
    }
  }


  bool VrGameTextEdit::keysPending() {
    return !g_keys.empty();
  }


  uint32_t VrGameTextEdit::takeKey() {
    if (!isOpen()) {
      g_keys.clear();
      return 0;
    }

    if (g_keys.empty())
      return 0;

    uint32_t key = g_keys.front();
    g_keys.pop_front();
    return key;
  }

}
