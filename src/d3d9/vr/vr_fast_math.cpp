#include <cstdint>
#include <cstring>

#include "../../util/log/log.h"

#include "vr_fast_math.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  // cdecl double (double): the argument is on the stack, the result goes in
  // st0 as the game's callers expect. roundsd modes: 9 = down, 10 = up,
  // both without the precision exception.
  __attribute__((naked)) static void floorDouble() {
    asm volatile(
      "movsd 4(%esp), %xmm0\n\t"
      "roundsd $9, %xmm0, %xmm0\n\t"
      "subl $8, %esp\n\t"
      "movsd %xmm0, (%esp)\n\t"
      "fldl (%esp)\n\t"
      "addl $8, %esp\n\t"
      "ret\n\t");
  }


  __attribute__((naked)) static void ceilDouble() {
    asm volatile(
      "movsd 4(%esp), %xmm0\n\t"
      "roundsd $10, %xmm0, %xmm0\n\t"
      "subl $8, %esp\n\t"
      "movsd %xmm0, (%esp)\n\t"
      "fldl (%esp)\n\t"
      "addl $8, %esp\n\t"
      "ret\n\t");
  }


  static bool redirectFunction(uintptr_t function, void (*replacement)()) {
    constexpr size_t JumpLength = 5;

    if (!VrGameMemory::matches(function, VrGame::MathFunctionStart, sizeof(VrGame::MathFunctionStart)))
      return false;

    uint8_t original[JumpLength];
    std::memcpy(original, VrGame::MathFunctionStart, JumpLength);

    uint8_t jump[JumpLength] = { 0xE9 };
    int32_t displacement = static_cast<int32_t>(
      reinterpret_cast<uintptr_t>(replacement) - (function + JumpLength));
    std::memcpy(jump + 1, &displacement, sizeof(displacement));

    return VrGameMemory::replaceCode(function, original, jump, JumpLength);
  }


  void VrFastMath::install() {
    __builtin_cpu_init();

    if (!__builtin_cpu_supports("sse4.1")) {
      Logger::info("VR: No SSE4.1, the game keeps its own floor and ceil");
      return;
    }

    bool floorDone = redirectFunction(VrGame::CrtFloor, floorDouble);
    bool ceilDone  = redirectFunction(VrGame::CrtCeil, ceilDouble);

    if (floorDone && ceilDone)
      Logger::info("VR: floor and ceil of the game use SSE4.1");
    else
      Logger::warn("VR: floor and ceil of the game were not found, they stay as they are");
  }

}
