#include <windows.h>

#include <cstring>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_sound_focus.h"

namespace dxvk {

  namespace {

    // DSBCAPS_GLOBALFOCUS
    constexpr uint32_t GlobalFocus = 0x8000;

    // The immediate operand of `or reg, imm32`
    constexpr size_t ImmediateOffset = 2;

    bool addGlobalFocus(uintptr_t site, const uint8_t* instruction, size_t size) {
      uint32_t flags = 0;
      std::memcpy(&flags, instruction + ImmediateOffset, sizeof(flags));
      flags |= GlobalFocus;

      uint8_t patched[6];
      std::memcpy(patched, instruction, size);
      std::memcpy(patched + ImmediateOffset, &flags, sizeof(flags));

      return VrGameMemory::replaceCode(site, instruction, patched, size);
    }

    HWND WINAPI gameWindowInFront() {
      auto main = *reinterpret_cast<const uint8_t* const*>(VrGame::MainSingleton);
      return main ? *reinterpret_cast<const HWND*>(main + VrGame::MainWindow) : nullptr;
    }

    /**
     * \brief Replaces what a module's import table holds for a function
     *
     * \returns \c true if the module imports the function
     */
    bool replaceImport(HMODULE module, FARPROC function, const void* replacement) {
      auto base = reinterpret_cast<const uint8_t*>(module);
      auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      auto nt   = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
      const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

      if (!imports.VirtualAddress)
        return false;

      auto library = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);

      for (; library->Name; library++) {
        auto entry = reinterpret_cast<uintptr_t*>(const_cast<uint8_t*>(base) + library->FirstThunk);

        for (; *entry; entry++) {
          if (*entry != reinterpret_cast<uintptr_t>(function))
            continue;

          DWORD oldProtect = 0;

          if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtect))
            return false;

          *entry = reinterpret_cast<uintptr_t>(replacement);
          VirtualProtect(entry, sizeof(*entry), oldProtect, &oldProtect);
          return true;
        }
      }

      return false;
    }

  }


  void VrSoundFocus::install() {
    constexpr size_t Size = sizeof(VrGame::SoundFlagsOrEcx);

    bool found = VrGameMemory::matches(VrGame::SoundFlagsOpen, VrGame::SoundFlagsOrEcx, Size)
      && VrGameMemory::matches(VrGame::SoundFlagsOpenOgg, VrGame::SoundFlagsOrEcx, Size)
      && VrGameMemory::matches(VrGame::SoundFlagsCopy, VrGame::SoundFlagsOrEcx, Size)
      && VrGameMemory::matches(VrGame::SoundFlagsPrepareReverb, VrGame::SoundFlagsOrEdx, Size);

    // All of them or none: a sound and its copy or reverb share their fate
    if (!found
     || !addGlobalFocus(VrGame::SoundFlagsOpen, VrGame::SoundFlagsOrEcx, Size)
     || !addGlobalFocus(VrGame::SoundFlagsOpenOgg, VrGame::SoundFlagsOrEcx, Size)
     || !addGlobalFocus(VrGame::SoundFlagsCopy, VrGame::SoundFlagsOrEcx, Size)
     || !addGlobalFocus(VrGame::SoundFlagsPrepareReverb, VrGame::SoundFlagsOrEdx, Size)) {
      Logger::warn("VR: The game's sound buffers were not found, sounds stop when the window loses the focus");
      return;
    }

    Logger::info("VR: The game's sounds stay audible when the window loses the focus");
  }


  void VrSoundFocus::keepPluginSound() {
    HMODULE plugin = GetModuleHandleW(L"itr-nvse.dll");

    if (!plugin)
      return;

    FARPROC function = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetForegroundWindow");

    if (function && replaceImport(plugin, function, reinterpret_cast<const void*>(&gameWindowInFront)))
      Logger::info("VR: itr-nvse sees the game's window in front, its bAltTabMute does not silence the hidden window");
    else
      Logger::warn("VR: itr-nvse's window test was not found, set bAltTabMute to 0 if the game has no sound");
  }

}
