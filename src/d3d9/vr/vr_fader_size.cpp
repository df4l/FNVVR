#include "../../util/log/log.h"

#include "vr_fader_size.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  void VrFaderSize::install() {
    constexpr size_t Size = sizeof(VrGame::FaderHalfExtentCode);

    const uint8_t nops[Size] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

    bool width  = VrGameMemory::replaceCode(VrGame::FaderHalfWidth,  VrGame::FaderHalfExtentCode, nops, Size);
    bool height = VrGameMemory::replaceCode(VrGame::FaderHalfHeight, VrGame::FaderHalfExtentCode, nops, Size);

    if (width && height)
      Logger::info("VR: Faders cover twice the screen, the whole of each eye");
    else
      Logger::warn("VR: The faders' size was not found, fades to black may leave the edges of the eyes uncovered");
  }

}
