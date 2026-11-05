#include <cstring>
#include <mutex>
#include <set>

#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

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

    /**
     * \brief Checks that code belongs to neither the game nor this library
     */
    bool isForeignCode(uintptr_t target) {
      MEMORY_BASIC_INFORMATION info = { };

      if (!VirtualQuery(reinterpret_cast<void*>(target), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;

      HMODULE self = nullptr;
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const char*>(&isForeignCode), &self);

      return info.AllocationBase != GetModuleHandleA(nullptr) && info.AllocationBase != self;
    }

    /**
     * \brief Logs which module holds a site of ours, once per site
     *
     * A feature that does not start names no other mod by itself, this
     * line does.
     */
    void reportForeignCode(uintptr_t site, uintptr_t target) {
      static std::mutex         mutex;
      static std::set<uintptr_t> reported;

      if (!isForeignCode(target))
        return;

      std::lock_guard<std::mutex> lock(mutex);

      if (!reported.insert(site).second)
        return;

      MEMORY_BASIC_INFORMATION info = { };
      char name[MAX_PATH] = { };

      if (VirtualQuery(reinterpret_cast<void*>(target), &info, sizeof(info)))
        GetModuleFileNameA(reinterpret_cast<HMODULE>(info.AllocationBase), name, sizeof(name));

      Logger::info(str::format("VR: Game code at 0x", std::hex, site, " is already patched, it reaches 0x", target,
        std::dec, " in '", name[0] ? name : "unnamed memory", "'"));
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

    if (!readCallTarget(site, &currentTarget))
      return false;

    if (currentTarget == target)
      return true;

    reportForeignCode(site, currentTarget);
    return false;
  }


  bool VrGameMemory::callTarget(
          uintptr_t             site,
          uintptr_t*            target) {
    return readCallTarget(site, target);
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


  bool VrGameMemory::canChainCall(
          uintptr_t             site,
          uintptr_t             expectedTarget) {
    uintptr_t current = 0;

    return readCallTarget(site, &current)
        && (current == expectedTarget || isForeignCode(current));
  }


  bool VrGameMemory::chainCall(
          uintptr_t             site,
          uintptr_t             expectedTarget,
    const void*                 newTarget,
          uintptr_t*            previous) {
    uintptr_t current = 0;

    if (!canChainCall(site, expectedTarget) || !readCallTarget(site, &current))
      return false;

    if (!redirectCall(site, current, newTarget))
      return false;

    reportForeignCode(site, current);
    *previous = current;
    return true;
  }


  bool VrGameMemory::chainCalls(
    const uintptr_t*            sites,
          size_t                count,
          uintptr_t             expectedTarget,
    const void*                 newTarget,
          uintptr_t*            previous) {
    uintptr_t held = 0;
    bool same = count && readCallTarget(sites[0], &held);

    for (size_t i = 0; i < count; i++) {
      uintptr_t target = 0;
      same = same && readCallTarget(sites[i], &target) && target == held
        && canChainCall(sites[i], expectedTarget);
    }

    for (size_t i = 0; i < count; i++) {
      if (same)
        same = chainCall(sites[i], expectedTarget, newTarget, previous);
      else
        callsTo(sites[i], expectedTarget);
    }

    return same;
  }


  bool VrGameMemory::replaceCode(
          uintptr_t             site,
    const uint8_t*              expected,
    const uint8_t*              replacement,
          size_t                size) {
    if (!matches(site, expected, size))
      return false;

    void* code = reinterpret_cast<void*>(site);
    DWORD oldProtect = 0;

    if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &oldProtect))
      return false;

    std::memcpy(code, replacement, size);

    VirtualProtect(code, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), code, size);
    return true;
  }


  bool VrGameMemory::redirectVirtual(
          uintptr_t             slot,
          uintptr_t             expectedTarget,
    const void*                 newTarget) {
    if (!isReadable(slot, sizeof(uintptr_t)))
      return false;

    uintptr_t* entry = reinterpret_cast<uintptr_t*>(slot);

    if (*entry != expectedTarget) {
      reportForeignCode(slot, *entry);
      return false;
    }

    DWORD oldProtect = 0;

    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtect))
      return false;

    *entry = reinterpret_cast<uintptr_t>(newTarget);

    VirtualProtect(entry, sizeof(*entry), oldProtect, &oldProtect);
    return true;
  }


  bool VrGameMemory::chainVirtual(
          uintptr_t             slot,
          uintptr_t             expectedTarget,
    const void*                 newTarget,
          uintptr_t*            previous) {
    if (!isReadable(slot, sizeof(uintptr_t)))
      return false;

    uintptr_t current = *reinterpret_cast<const uintptr_t*>(slot);

    if (current != expectedTarget && !isForeignCode(current))
      return false;

    if (!redirectVirtual(slot, current, newTarget))
      return false;

    reportForeignCode(slot, current);
    *previous = current;
    return true;
  }

}
