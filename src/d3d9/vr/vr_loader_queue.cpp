#include <atomic>
#include <cstdint>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_loader_queue.h"

namespace dxvk {

  namespace {

    using UpdateReferencesFn = bool (__thiscall*)(void* loader);


    uint32_t delayedQueueCount(void* loader) {
      auto queue = *reinterpret_cast<uint8_t* const*>(
        reinterpret_cast<uintptr_t>(loader) + VrGame::ModelLoaderDelayedQueue);

      if (!queue)
        return 1;

      // The game changes the count with interlocked operations
      return reinterpret_cast<const std::atomic<uint32_t>*>(
        queue + VrGame::LockFreeMapCount)->load(std::memory_order_acquire);
    }


    bool __thiscall updateReferencesToQueue(void* loader) {
      if (!delayedQueueCount(loader))
        return true;

      return reinterpret_cast<UpdateReferencesFn>(VrGame::ModelLoaderUpdateReferences)(loader);
    }

  }


  void VrLoaderQueue::install() {
    bool original = VrGameMemory::matches(VrGame::ModelLoaderUpdateReferences,
      VrGame::ModelLoaderUpdateReferencesPrologue, sizeof(VrGame::ModelLoaderUpdateReferencesPrologue));

    if (original && VrGameMemory::redirectCall(VrGame::ModelLoaderUpdateReferencesCallSite,
          VrGame::ModelLoaderUpdateReferences, reinterpret_cast<const void*>(&updateReferencesToQueue)))
      Logger::info("VR: Model loader skips its delayed queue when empty");
    else
      Logger::info("VR: Model loader's delayed queue walk is not the original, it stays as it is");
  }

}
