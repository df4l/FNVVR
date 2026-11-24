#include <algorithm>
#include <cstdint>
#include <cstring>

#include "vr_fader_overlay.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  bool VrFaderOverlay::initialize() {
    m_available = VrGameMemory::readable(VrGame::FaderTable, VrGame::FaderCount * VrGame::FaderStride);
    return m_available;
  }


  bool VrFaderOverlay::update(float width, float height) {
    m_fills = VrPaintList(uint32_t(width), uint32_t(height));

    if (!m_available)
      return false;

    m_fills.setBlend(VrPaintBlend::OverKeepAlpha);

    bool shown = false;

    for (uint32_t i = 0; i < VrGame::FaderCount; i++) {
      uintptr_t entry = VrGame::FaderTable + i * VrGame::FaderStride;

      uintptr_t root = 0;
      float alpha = 0.0f;
      std::memcpy(&root, reinterpret_cast<const void*>(entry + VrGame::FaderRoot), sizeof(uint32_t));
      std::memcpy(&alpha, reinterpret_cast<const void*>(entry + VrGame::FaderAlpha), sizeof(alpha));

      alpha = std::clamp(alpha, 0.0f, 1.0f);

      if (!root || alpha <= 0.0f)
        continue;

      float level = i == VrGame::FaderWhite ? 1.0f : 0.0f;
      m_fills.fillRect(0.0f, 0.0f, width, height, { level, level, level, alpha });
      shown = true;
    }

    return shown;
  }

}
