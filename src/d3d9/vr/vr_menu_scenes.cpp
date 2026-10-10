#include <cstring>
#include <vector>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_menu_scenes.h"

namespace dxvk {

  namespace {

    using IsMenuActiveFn    = bool (__cdecl*)(uint32_t menuId, uint32_t unused);
    using RenderMenuSceneFn = void (__cdecl*)();
    using GetAsNodeFn       = uintptr_t (__fastcall*)(uintptr_t object, void* unused);

    constexpr uint8_t PushZero[] = { 0x6A, 0x00 };
    constexpr uint8_t PushImm32  = 0x68;

    // The lock model is four levels deep below the scene root
    constexpr uint32_t MaxSceneDepth = 8;

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

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    uintptr_t asNode(uintptr_t object) {
      auto vtable = readField<uintptr_t>(object, 0);
      return reinterpret_cast<GetAsNodeFn>(readField<uintptr_t>(vtable, VrGame::ObjectGetAsNodeSlot))(object, nullptr);
    }

    void hideByName(uintptr_t object, const char* name, uint32_t depth, std::vector<uint32_t*>& hidden) {
      auto objectName = readField<const char*>(object, VrGame::ObjectName);
      auto flags = reinterpret_cast<uint32_t*>(object + VrGame::ObjectFlags);

      if (objectName && !std::strcmp(objectName, name)) {
        if (!(*flags & VrGame::ObjectFlagAppCulled)) {
          *flags |= VrGame::ObjectFlagAppCulled;
          hidden.push_back(flags);
        }
        return;
      }

      uintptr_t node = asNode(object);

      if (!node || depth == MaxSceneDepth)
        return;

      auto children = readField<const uintptr_t*>(node, VrGame::NodeChildren);
      auto count    = readField<uint16_t>(node, VrGame::NodeChildCount);

      for (uint32_t i = 0; children && i < count; i++) {
        if (children[i])
          hideByName(children[i], name, depth + 1, hidden);
      }
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

    // The panel has no background for the lock's shadow to darken, and the
    // panel's alpha comes from depth, so the soft disc would show as an
    // opaque black ring around the lock
    std::vector<uint32_t*> hidden;

    uintptr_t root = isMenuActive(VrGame::LockPickMenuId)
      ? readField<uintptr_t>(VrGame::LockPickSceneRoot, 0) : 0;

    if (root)
      hideByName(root, VrGame::LockPickBackdrop, 0, hidden);

    // Same order as the game's own dispatch
    for (const auto& scene : VrGame::MenuScenes) {
      if (isMenuActive(scene.menuId))
        reinterpret_cast<RenderMenuSceneFn>(scene.render)();
    }

    for (uint32_t* flags : hidden)
      *flags &= ~VrGame::ObjectFlagAppCulled;
  }

}
