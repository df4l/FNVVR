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

    using ReadKeyFn      = uint32_t (__fastcall*)(void* interfaceManager, void* unused, int32_t state, uint32_t scanCode);
    using CloseFn        = void (__cdecl*)();
    using GetDebugTextFn = bool (__thiscall*)(void* debugText, char* buffer, float x, float y, int32_t alignment);

    // Keys typed faster than the game reads them, one per frame
    constexpr size_t MaxQueuedKeys = 64;

    // Lines walked at most in the console's list, far above its history size
    constexpr uint32_t MaxPrintedLines = 4096;

    bool                 g_installed = false;
    std::deque<uint32_t> g_keys;
    bool                 g_close     = false;
    std::vector<char>    g_buffer(VrGame::DebugTextBufferSize);

    const uint8_t* console() {
      return *reinterpret_cast<const uint8_t* const*>(VrGame::ConsoleGlobal);
    }

    template<typename T>
    T read(const uint8_t* object, uintptr_t offset) {
      T value;
      std::memcpy(&value, object + offset, sizeof(value));
      return value;
    }

    // BSStringT: the characters, then a 16-bit length
    std::string readString(const uint8_t* string) {
      auto text   = read<const char*>(string, 0);
      auto length = read<uint16_t>(string, sizeof(const char*));

      if (!text)
        return std::string();

      return std::string(text, strnlen(text, length == 0xFFFF ? VrGame::DebugTextBufferSize : length));
    }

    uint32_t __fastcall readKeyHook(void* interfaceManager, void* unused, int32_t state, uint32_t scanCode) {
      uint32_t key = reinterpret_cast<ReadKeyFn>(VrGame::ConsoleReadKey)(interfaceManager, nullptr, state, scanCode);

      // With the console closed, the keys typed for the game's text field
      if (!VrGameConsole::isOpen()) {
        g_keys.clear();
        g_close = false;
        return key ? key : VrGameTextEdit::takeKey();
      }

      // Closed here, where the game itself toggles the console
      if (g_close) {
        g_keys.clear();
        g_close = false;
        reinterpret_cast<CloseFn>(VrGame::CloseConsole)();
        return 0;
      }

      if (key || g_keys.empty())
        return key;

      key = g_keys.front();
      g_keys.pop_front();
      return key;
    }

  }


  bool VrGameConsole::install() {
    if (g_installed)
      return true;

    bool found = canOpen()
      && VrGameMemory::matches(VrGame::CloseConsole,
           VrGame::CloseConsolePrologue, sizeof(VrGame::CloseConsolePrologue))
      && VrGameMemory::matches(VrGame::ConsoleReadKey,
           VrGame::ConsoleReadKeyPrologue, sizeof(VrGame::ConsoleReadKeyPrologue))
      && VrGameMemory::matches(VrGame::GetDebugText,
           VrGame::GetDebugTextPrologue, sizeof(VrGame::GetDebugTextPrologue))
      && VrGameMemory::readable(VrGame::ConsoleGlobal, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::DebugTextGlobal, sizeof(uintptr_t));

    g_installed = found && VrGameMemory::redirectCall(VrGame::ConsoleKeyCallSite,
      VrGame::ConsoleReadKey, reinterpret_cast<const void*>(&readKeyHook));

    if (!g_installed)
      Logger::warn("VR: The console's key input was not found, the console cannot be typed in from the headset");

    return g_installed;
  }


  bool VrGameConsole::canOpen() {
    return VrGameMemory::matches(VrGame::SetOpenConsoleRequest,
      VrGame::SetOpenConsoleRequestBytes, sizeof(VrGame::SetOpenConsoleRequestBytes));
  }


  void VrGameConsole::requestOpen() {
    if (!canOpen())
      return;

    *reinterpret_cast<volatile uint8_t*>(VrGame::OpenConsoleRequest) = 1;
    Logger::info("VR: The console was asked to open");
  }


  bool VrGameConsole::isOpen() {
    const uint8_t* menu = console();
    return menu && read<int8_t>(menu, VrGame::ConsoleVisible) > 0;
  }


  void VrGameConsole::pressKey(uint32_t key) {
    if (g_keys.size() < MaxQueuedKeys)
      g_keys.push_back(key);
  }


  void VrGameConsole::requestClose() {
    g_close = true;
  }


  void VrGameConsole::readOutput(std::vector<std::string>& lines, uint32_t count) {
    lines.clear();

    const uint8_t* menu = console();

    if (!menu)
      return;

    uint32_t printed = std::min(read<uint32_t>(menu, VrGame::ConsolePrintedCount), MaxPrintedLines);
    uint32_t last    = uint32_t(std::clamp(read<int32_t>(menu, VrGame::ConsoleLastLine), 0, int32_t(printed)));
    uint32_t first   = last > count ? last - count : 0;

    auto node = read<const uint8_t*>(menu, VrGame::ConsolePrintedLines);

    for (uint32_t i = 0; i < last && node; i++) {
      if (i >= first)
        lines.push_back(readString(node + VrGame::ConsoleLineNodeText));

      node = read<const uint8_t*>(node, 0);
    }
  }


  void VrGameConsole::readInput(std::string& text, uint32_t& cursor) {
    text.clear();
    cursor = 0;

    const uint8_t* menu = console();
    auto debugText = *reinterpret_cast<void* const*>(VrGame::DebugTextGlobal);

    if (!g_installed || !menu || !debugText)
      return;

    float x = float(read<int32_t>(menu, VrGame::ConsoleTextX));
    float y = float(read<int32_t>(menu, VrGame::ConsoleTextY));

    // The game copies at most the buffer's size less two, without always
    // ending the copy
    std::fill(g_buffer.begin(), g_buffer.end(), '\0');

    if (!reinterpret_cast<GetDebugTextFn>(VrGame::GetDebugText)(debugText, g_buffer.data(), x, y,
        VrGame::ConsoleTextAlignment))
      return;

    text.assign(g_buffer.data(), strnlen(g_buffer.data(), g_buffer.size() - 1));

    size_t marker = text.find(VrGame::ConsoleCursorChar);

    if (marker == std::string::npos) {
      cursor = uint32_t(text.size());
      return;
    }

    text.erase(marker, 1);
    cursor = uint32_t(marker);
  }

}
