#include <cstdint>
#include <cstring>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_tree_leaves.h"
#include "vr_tree_leaves_rules.h"

namespace dxvk {

  namespace {

    using ScaleAdjustmentFn = void (__cdecl*)();

    bool g_installed = false;

    VrVector3 readVector(const uint8_t* address) {
      VrVector3 v;
      std::memcpy(&v, address, sizeof(v));
      return v;
    }

    // Column of a NiMatrix3 stored row by row
    VrVector3 readColumn(const uint8_t* matrix, uint32_t column) {
      const float* m = reinterpret_cast<const float*>(matrix);
      return VrVector3 { m[column], m[3 + column], m[6 + column] };
    }

    void writeAxis(uintptr_t address, const VrVector3& axis) {
      float value[4] = { axis.x, axis.y, axis.z, 0.0f };
      std::memcpy(reinterpret_cast<void*>(address), value, sizeof(value));
    }

    const uint8_t* leavesBeingDrawn() {
      const uint8_t* pass = *reinterpret_cast<const uint8_t* const*>(VrGame::ShaderCurrentRenderPass);

      if (!pass)
        return nullptr;

      return *reinterpret_cast<const uint8_t* const*>(pass + VrGame::RenderPassGeometry);
    }

    // Centre of the leaves' world bound, or their origin without a bound
    VrVector3 leavesCenter(const uint8_t* geometry) {
      const uint8_t* bound = *reinterpret_cast<const uint8_t* const*>(geometry + VrGame::ObjectWorldBound);

      return bound
        ? readVector(bound)
        : readVector(geometry + VrGame::ObjectWorldTranslation);
    }

    void __cdecl updateScaleAdjustment() {
      reinterpret_cast<ScaleAdjustmentFn>(VrGame::LeafScaleAdjustment)();

      const uint8_t* camera = *reinterpret_cast<const uint8_t* const*>(VrGame::ShaderManagerCamera);
      const uint8_t* geometry = leavesBeingDrawn();

      if (!camera || !geometry)
        return;

      VrVector3 eye = readVector(camera + VrGame::CameraWorldTranslation);
      VrVector3 cameraRight = readColumn(camera + VrGame::CameraWorldRotation, 2);

      VrLeafCardAxes axes;

      if (!vrLeafCardAxesFacing(eye, leavesCenter(geometry), cameraRight, axes))
        return;

      writeAxis(VrGame::LeafBillboardUp, axes.up);
      writeAxis(VrGame::LeafBillboardRight, axes.right);
    }

  }


  bool VrTreeLeaves::install() {
    if (g_installed)
      return true;

    bool readable = VrGameMemory::readable(VrGame::ShaderManagerCamera, sizeof(void*))
      && VrGameMemory::readable(VrGame::ShaderCurrentRenderPass, sizeof(void*))
      && VrGameMemory::readable(VrGame::LeafBillboardRight, 4 * sizeof(float))
      && VrGameMemory::readable(VrGame::LeafBillboardUp, 4 * sizeof(float));

    g_installed = readable && VrGameMemory::redirectCall(VrGame::LeafScaleAdjustmentCallSite,
      VrGame::LeafScaleAdjustment, reinterpret_cast<const void*>(&updateScaleAdjustment));

    return g_installed;
  }

}
