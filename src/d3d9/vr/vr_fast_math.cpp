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
    // Why: the C runtime's fabs (cdecl, double on the stack, result in st0)
    // calls __ctrlfp twice, which stores and reloads the x87 control word
    // and flushes the pipeline each time. The profiles of a 120 Hz run
    // showed that as about 4.5 % of the game's samples.
    // How: the same function as three instructions, fld qword ptr [esp+4];
    // fabs; ret. The instruction clears the sign bit and needs no control
    // word, since the sign bit does not depend on the rounding mode.
    // The 7 bytes fit in the 7 bytes that are checked.
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

    // Why: the executable keeps these as real calls with unoptimised x87
    // code (see vr_engine_math_stubs.h for each function). A 5-byte jump to
    // a replacement is the smallest change that speeds up every caller
    // without touching them, and it is enough to dominate the call cost.
    // The order of the arithmetic is the original's, so results are the
    // same bit for bit (checked against a reference under Wine).
    const Replacement replacements[] = {
      { "NiPoint3 constructor",  VrGame::PointConstruct,     VrGame::PointConstructStart,     engine_math::PointConstruct },
      { "NiPoint3 sum",          VrGame::PointAdd,           VrGame::PointAddStart,           engine_math::PointAdd },
      { "NiPoint3 difference",   VrGame::PointSubtract,      VrGame::PointSubtractStart,      engine_math::PointSubtract },
      { "NiPoint3 scaling",      VrGame::PointScale,         VrGame::PointScaleStart,         engine_math::PointScale },
      { "NiMatrix3 by NiPoint3", VrGame::MatrixPoint,        VrGame::MatrixPointStart,        engine_math::MatrixPoint },
      { "NiMatrix3 product",     VrGame::MatrixMatrix,       VrGame::MatrixMatrixStart,       engine_math::MatrixMatrix },
      { "NiTransform product",   VrGame::TransformTransform, VrGame::TransformTransformStart, engine_math::TransformTransform },
      { "shadow light bound test", VrGame::LightFrustumBound, VrGame::LightFrustumBoundStart, engine_math::LightFrustumBound },
      { "__CIsqrt",              VrGame::CrtCiSqrt,          VrGame::CrtCiSqrtStart,          engine_math::CrtCiSqrt },
      { "sqrt",                  VrGame::CrtSqrt,            VrGame::CrtSqrtStart,            engine_math::CrtSqrt },
    };

    uint32_t done = 0;

    for (const Replacement& replacement : replacements) {
      if (redirectFunction(replacement.address, replacement.start, replacement.function))
        done++;
      else
        Logger::info(str::format("VR: The game's ", replacement.name, " is not the original, it stays as it is"));
    }

    Logger::info(str::format("VR: ", done, " of ", std::size(replacements), " math functions of the game replaced"));

    // Why: these two getters only return a field, but the compiled form
    // stores ecx to the stack and reloads it (a store-forwarding delay on
    // the critical path of every caller) and they are called very often.
    // How: mov eax, [ecx+field]; ret, written over the start of the body
    // (replaceCode compares and writes the same length, so the rest of
    // the 13 checked bytes is filled with nops, never reached).
    struct Getter {
      uintptr_t       address;
      const uint8_t*  start;
      uint8_t         field;
    };

    const Getter getters[] = {
      { VrGame::CurrentProcessGetter, VrGame::CurrentProcessGetterStart, 0x68 },
      { VrGame::ProcessLevelGetter,   VrGame::ProcessLevelGetterStart,   0x28 },
    };

    uint32_t getterDone = 0;

    for (const Getter& getter : getters) {
      const uint8_t code[] = { 0x8B, 0x41, getter.field, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

      if (VrGameMemory::replaceCode(getter.address, getter.start, code, sizeof(code)))
        getterDone++;
    }

    Logger::info(str::format("VR: ", getterDone, " of ", std::size(getters), " field getters of the game replaced"));
  }

}
