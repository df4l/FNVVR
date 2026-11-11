#include <cstring>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_engine_math_stubs.h"
#include "vr_fast_math.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    constexpr size_t StartLength = 8;
    constexpr size_t JumpLength  = 5;

    /**
     * \brief Makes a game function jump to a replacement
     *
     * \param [in] function Address of the function
     * \param [in] start Its first StartLength bytes
     * \param [in] replacement Function with the same convention
     */
    bool redirectFunction(uintptr_t function, const uint8_t* start, engine_math::Function replacement) {
      if (!VrGameMemory::matches(function, start, StartLength))
        return false;

      uint8_t jump[JumpLength] = { 0xE9 };
      int32_t displacement = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(replacement) - (function + JumpLength));
      std::memcpy(jump + 1, &displacement, sizeof(displacement));

      return VrGameMemory::replaceCode(function, start, jump, JumpLength);
    }

  }


  void VrFastMath::install() {
    // fld qword ptr [esp+4]; fabs; ret
    const uint8_t fabsCode[] = { 0xDD, 0x44, 0x24, 0x04, 0xD9, 0xE1, 0xC3 };

    if (VrGameMemory::replaceCode(VrGame::CrtFabs, VrGame::CrtFabsStart, fabsCode, sizeof(fabsCode)))
      Logger::info("VR: fabs of the game uses the x87 instruction");
    else
      Logger::warn("VR: fabs of the game was not found, it stays as it is");

    struct Replacement {
      const char*             name;
      uintptr_t               address;
      const uint8_t*          start;
      engine_math::Function   function;
    };

    const Replacement replacements[] = {
      { "NiPoint3 constructor",  VrGame::PointConstruct,     VrGame::PointConstructStart,     engine_math::PointConstruct },
      { "NiPoint3 sum",          VrGame::PointAdd,           VrGame::PointAddStart,           engine_math::PointAdd },
      { "NiPoint3 difference",   VrGame::PointSubtract,      VrGame::PointSubtractStart,      engine_math::PointSubtract },
      { "NiPoint3 scaling",      VrGame::PointScale,         VrGame::PointScaleStart,         engine_math::PointScale },
      { "NiMatrix3 by NiPoint3", VrGame::MatrixPoint,        VrGame::MatrixPointStart,        engine_math::MatrixPoint },
      { "NiMatrix3 product",     VrGame::MatrixMatrix,       VrGame::MatrixMatrixStart,       engine_math::MatrixMatrix },
      { "NiTransform product",   VrGame::TransformTransform, VrGame::TransformTransformStart, engine_math::TransformTransform },
    };

    uint32_t done = 0;

    for (const Replacement& replacement : replacements) {
      if (redirectFunction(replacement.address, replacement.start, replacement.function))
        done++;
      else
        Logger::info(str::format("VR: The game's ", replacement.name, " is not the original, it stays as it is"));
    }

    Logger::info(str::format("VR: ", done, " of ", std::size(replacements), " math functions of the game replaced"));
  }

}
