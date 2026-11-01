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

}
