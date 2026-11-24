#include <windows.h>

#include <atomic>

#include "vr_depth_resolve.h"

#include "../../util/log/log.h"

namespace dxvk {

  bool vrOffersResz() {
    // A loaded NVSE plugin is not unloaded, so a positive answer is kept
    static std::atomic<bool> loaded = { false };

    if (loaded.load(std::memory_order_relaxed))
      return true;

    if (!GetModuleHandleW(L"DepthResolve.dll"))
      return false;

    if (!loaded.exchange(true))
      Logger::info("VR: Depth Resolve is loaded, RESZ is offered on this GPU");

    return true;
  }

}
