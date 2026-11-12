#include <cstring>

#include "../../util/log/log.h"

#include "vr_fader_size.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    // The operand of the game's multiplication by one half
    const double NoExtent = 0.0;

  }


  bool VrFaderSize::install() {
    constexpr size_t Size = sizeof(VrGame::FaderHalfExtentCode);

    uint8_t patched[Size];
    std::memcpy(patched, VrGame::FaderHalfExtentCode, Size);

    uintptr_t operand = reinterpret_cast<uintptr_t>(&NoExtent);
    std::memcpy(patched + 2, &operand, sizeof(uint32_t));

    bool width  = VrGameMemory::replaceCode(VrGame::FaderHalfWidth,  VrGame::FaderHalfExtentCode, patched, Size);
    bool height = VrGameMemory::replaceCode(VrGame::FaderHalfHeight, VrGame::FaderHalfExtentCode, patched, Size);

    if (width && height) {
      Logger::info("VR: The game's faders are drawn over each eye");
      return true;
    }

    Logger::warn("VR: The faders' size was not found, fades may show as a rectangle on a panel");
    return false;
  }

}
