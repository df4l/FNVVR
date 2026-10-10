#include <cstring>

#include <windows.h>

#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    constexpr uint8_t  CallOpcode = 0xE8;
    constexpr uint32_t CallLength = 5;

    bool isReadable(uintptr_t address, size_t size) {
      MEMORY_BASIC_INFORMATION info = { };

      if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)))
        return false;

      if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;

      // The range must not leave the region that was just checked
      uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
      return address + size <= regionEnd;
    }

    bool readCallTarget(uintptr_t site, uintptr_t* target) {
      if (!isReadable(site, CallLength))
        return false;

      const uint8_t* code = reinterpret_cast<const uint8_t*>(site);

      if (code[0] != CallOpcode)
        return false;

      int32_t displacement = 0;
      std::memcpy(&displacement, code + 1, sizeof(displacement));

      *target = site + CallLength + displacement;
      return true;
    }

  }


  bool VrGameMemory::matches(
          uintptr_t             address,
    const uint8_t*              bytes,
          size_t                size) {
    return isReadable(address, size)
        && !std::memcmp(reinterpret_cast<const void*>(address), bytes, size);
  }


  bool VrGameMemory::readable(
          uintptr_t             address,
          size_t                size) {
    return isReadable(address, size);
  }


  bool VrGameMemory::callsTo(
          uintptr_t             site,
          uintptr_t             target) {
    uintptr_t currentTarget = 0;
    return readCallTarget(site, &currentTarget) && currentTarget == target;
  }


  bool VrGameMemory::redirectCall(
          uintptr_t             site,
          uintptr_t             expectedTarget,
    const void*                 newTarget) {
    if (!callsTo(site, expectedTarget))
      return false;

    uint8_t* code = reinterpret_cast<uint8_t*>(site);
    DWORD oldProtect = 0;

    if (!VirtualProtect(code, CallLength, PAGE_EXECUTE_READWRITE, &oldProtect))
      return false;

    int32_t newDisplacement = static_cast<int32_t>(
      reinterpret_cast<uintptr_t>(newTarget) - (site + CallLength));
    std::memcpy(code + 1, &newDisplacement, sizeof(newDisplacement));

    VirtualProtect(code, CallLength, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), code, CallLength);
    return true;
  }


  bool VrGameMemory::redirectVirtual(
          uintptr_t             slot,
          uintptr_t             expectedTarget,
    const void*                 newTarget) {
    if (!isReadable(slot, sizeof(uintptr_t)))
      return false;

    uintptr_t* entry = reinterpret_cast<uintptr_t*>(slot);

    if (*entry != expectedTarget)
      return false;

    DWORD oldProtect = 0;

    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtect))
      return false;

    *entry = reinterpret_cast<uintptr_t>(newTarget);

    VirtualProtect(entry, sizeof(*entry), oldProtect, &oldProtect);
    return true;
  }

}
