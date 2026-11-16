#include <cstring>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_water_reflection.h"

namespace dxvk {

  namespace {

    using ReflectCameraFn = void (__cdecl*)(void* camera, void* plane, void* reflected);

    // The game's function, or another plugin's hook on its calls
    uintptr_t g_reflectCamera = VrGame::ReflectCameraAboutPlane;

  }


  bool VrWaterReflection::install() {
    constexpr size_t SiteCount = sizeof(VrGame::ReflectCameraCallSites) / sizeof(VrGame::ReflectCameraCallSites[0]);

    bool installed = VrGameMemory::chainCalls(VrGame::ReflectCameraCallSites, SiteCount,
      VrGame::ReflectCameraAboutPlane, reinterpret_cast<const void*>(&VrWaterReflection::reflectCameraHook),
      &g_reflectCamera);

    if (installed)
      Logger::info("VR: Water reflections are drawn for the headset's frustum");
    else
      Logger::warn("VR: The water reflection camera was not found, reflections are read too high in the headset");

    return installed;
  }


  void __cdecl VrWaterReflection::reflectCameraHook(void* camera, void* plane, void* reflected) {
    reinterpret_cast<ReflectCameraFn>(g_reflectCamera)(camera, plane, reflected);

    if (!reflected)
      return;

    // The frustum is read when the reflection is culled and drawn, after
    // this call: nothing derived from it needs updating here
    auto frustumAddress = static_cast<uint8_t*>(reflected) + VrGame::CameraFrustum;

    VrGameFrustum frustum;
    std::memcpy(&frustum, frustumAddress, sizeof(frustum));

    VrGameFrustum mirrored = vrComputeReflectionFrustum(frustum);
    std::memcpy(frustumAddress, &mirrored, sizeof(mirrored));
  }

}
