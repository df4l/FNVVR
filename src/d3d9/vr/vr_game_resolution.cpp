#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_resolution.h"

namespace dxvk {

  namespace {

    template<typename T>
    T* settingValue(uintptr_t setting) {
      return reinterpret_cast<T*>(setting + VrGame::SettingValue);
    }

  }


  bool VrGameResolution::apply(const VrExtent& size) {
    if (m_applied)
      return true;

    bool found = VrGameMemory::matches(VrGame::RendererSizeReadSite,
        VrGame::RendererSizeRead, sizeof(VrGame::RendererSizeRead))
      && VrGameMemory::matches(VrGame::GetIsFullscreen,
        VrGame::GetIsFullscreenPrologue, sizeof(VrGame::GetIsFullscreenPrologue))
      && VrGameMemory::readable(VrGame::SettingSizeWidth  + VrGame::SettingValue, sizeof(int32_t))
      && VrGameMemory::readable(VrGame::SettingSizeHeight + VrGame::SettingValue, sizeof(int32_t))
      && VrGameMemory::readable(VrGame::SettingFullScreen + VrGame::SettingValue, sizeof(uint8_t));

    if (!found)
      return false;

    int32_t* width  = settingValue<int32_t>(VrGame::SettingSizeWidth);
    int32_t* height = settingValue<int32_t>(VrGame::SettingSizeHeight);

    m_width  = *width;
    m_height = *height;

    *width  = int32_t(size.width);
    *height = int32_t(size.height);
    *settingValue<uint8_t>(VrGame::SettingFullScreen) = 0;

    m_applied = true;
    return true;
  }


  void VrGameResolution::restoreSettings() {
    if (!m_applied)
      return;

    *settingValue<int32_t>(VrGame::SettingSizeWidth)  = m_width;
    *settingValue<int32_t>(VrGame::SettingSizeHeight) = m_height;
    m_applied = false;
  }

}
