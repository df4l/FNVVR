#include "../../util/log/log.h"

#include "vr_fast_math.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  void VrFastMath::install() {
    // fld qword ptr [esp+4]; fabs; ret
    const uint8_t replacement[] = { 0xDD, 0x44, 0x24, 0x04, 0xD9, 0xE1, 0xC3 };

    static_assert(sizeof(replacement) == sizeof(VrGame::CrtFabsStart), "same length");

    if (VrGameMemory::replaceCode(VrGame::CrtFabs, VrGame::CrtFabsStart, replacement, sizeof(replacement)))
      Logger::info("VR: fabs of the game uses the x87 instruction");
    else
      Logger::warn("VR: fabs of the game was not found, it stays as it is");
  }

}
