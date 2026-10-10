#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_menu_scenes.h"

namespace dxvk {

  namespace {

    using IsMenuActiveFn    = bool (__cdecl*)(uint32_t menuId, uint32_t unused);
    using RenderMenuSceneFn = void (__cdecl*)();

    constexpr uint8_t PushZero[] = { 0x6A, 0x00 };
    constexpr uint8_t PushImm32  = 0x68;

    bool matchesScene(const VrGame::MenuScene& scene) {
      uint8_t push[sizeof(PushZero) + 1 + sizeof(uint32_t)] = { PushZero[0], PushZero[1], PushImm32 };

      for (uint32_t i = 0; i < sizeof(uint32_t); i++)
        push[sizeof(PushZero) + 1 + i] = uint8_t(scene.menuId >> (8 * i));

      return VrGameMemory::matches(scene.checkSite, push, sizeof(push))
          && VrGameMemory::callsTo(scene.checkSite + VrGame::MenuSceneActiveCallOffset, VrGame::IsMenuActive)
          && VrGameMemory::callsTo(scene.checkSite + VrGame::MenuSceneRenderCallOffset, scene.render);
    }

    bool isMenuActive(uint32_t menuId) {
      return reinterpret_cast<IsMenuActiveFn>(VrGame::IsMenuActive)(menuId, 0);
    }

  }


  bool VrMenuScenes::initialize() {
    m_available = true;

    for (const auto& scene : VrGame::MenuScenes)
      m_available = m_available && matchesScene(scene);

    return m_available;
  }


  bool VrMenuScenes::isAnyOpen() const {
    if (!m_available)
      return false;

    for (const auto& scene : VrGame::MenuScenes) {
      if (isMenuActive(scene.menuId))
        return true;
    }

    return false;
  }


  void VrMenuScenes::render() const {
    if (!m_available)
      return;

    // Same order as the game's own dispatch
    for (const auto& scene : VrGame::MenuScenes) {
      if (isMenuActive(scene.menuId))
        reinterpret_cast<RenderMenuSceneFn>(scene.render)();
    }
  }

}
