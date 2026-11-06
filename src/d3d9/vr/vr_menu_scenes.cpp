#include <cstring>
#include <vector>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_menu_scenes.h"

namespace dxvk {

  namespace {

    using IsMenuActiveFn    = bool (__cdecl*)(uint32_t menuId, uint32_t unused);
    using RenderMenuSceneFn = void (__cdecl*)();
    using PointerFn         = void* (__cdecl*)();
    using ObjectPointerFn   = void* (__fastcall*)(void* object, void* unused);

    // RenderPipBoy, __thiscall on Main with the screen and the renderer
    using ScreenFn          = void (__fastcall*)(void* main, void* unused,
      void* screen, void* renderer);

    // __thiscall on Main without arguments, called as __fastcall with an
    // unused second argument
    using MainFn            = void (__fastcall*)(void* main, void* unused);

    // Set while the hook below keeps the rendered menu out of the eye images
    bool g_renderedMenuMoved = false;

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

      uintptr_t node = vrGameAsNode(object);

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


  VrMenuScenes::~VrMenuScenes() {
    // The patched call stays in place and falls through to the game's function
    if (m_renderedMenuMoved)
      g_renderedMenuMoved = false;
  }


  bool VrMenuScenes::initialize() {
    m_available = !g_renderedMenuMoved
      && VrGameMemory::readable(VrGame::MainSingleton, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::Renderer, sizeof(uintptr_t))
      && VrGameMemory::callsTo(VrGame::PipBoyScreenCallSite, VrGame::RenderPipBoy);

    for (const auto& scene : VrGame::MenuScenes)
      m_available = m_available && matchesScene(scene);

    m_available = m_available && VrGameMemory::redirectCall(VrGame::RenderedMenuParentCallSite,
      VrGame::DrawRenderedMenuParent, reinterpret_cast<const void*>(&renderedMenuHook));

    m_renderedMenuMoved = m_available;
    g_renderedMenuMoved = m_available;
    return m_available;
  }


  bool VrMenuScenes::isAnyOpen() const {
    if (!m_available)
      return false;

    if (isRenderedMenuOpen() && !m_eyeRenderedMenu)
      return true;

    for (const auto& scene : VrGame::MenuScenes) {
      if (scene.menuId != m_eyeMenuId && isMenuActive(scene.menuId))
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
    hideLockBackdrop(hidden);

    // Same order as the game's own dispatch
    for (const auto& scene : VrGame::MenuScenes) {
      if (scene.menuId != m_eyeMenuId && isMenuActive(scene.menuId))
        reinterpret_cast<RenderMenuSceneFn>(scene.render)();
    }

    showHidden(hidden);

    auto main = readField<void*>(VrGame::MainSingleton, 0);

    if (!main || !isRenderedMenuOpen() || m_eyeRenderedMenu)
      return;

    reinterpret_cast<MainFn>(VrGame::DrawRenderedMenuParent)(main, nullptr);

    // The screen comes last, blended over the model, as in the game
    void* menu     = reinterpret_cast<PointerFn>(VrGame::GetCurrentRenderedMenu)();
    void* screen   = reinterpret_cast<ObjectPointerFn>(VrGame::GetScreenGeometry)(menu, nullptr);
    void* renderer = readField<void*>(VrGame::Renderer, 0);

    if (screen && renderer)
      reinterpret_cast<ScreenFn>(VrGame::RenderPipBoy)(main, nullptr, screen, renderer);
  }


  void VrMenuScenes::hideLockBackdrop(std::vector<uint32_t*>& hidden) {
    uintptr_t root = isMenuActive(VrGame::LockPickMenuId)
      ? readField<uintptr_t>(VrGame::LockPickSceneRoot, 0) : 0;

    if (root)
      hideByName(root, VrGame::LockPickBackdrop, 0, hidden);
  }


  void VrMenuScenes::hideObject(uintptr_t root, const char* name, std::vector<uint32_t*>& hidden) {
    hideByName(root, name, 0, hidden);
  }


  void VrMenuScenes::showHidden(const std::vector<uint32_t*>& hidden) {
    for (uint32_t* flags : hidden)
      *flags &= ~VrGame::ObjectFlagAppCulled;
  }


  bool VrMenuScenes::movesRenderedMenu() {
    return g_renderedMenuMoved && isRenderedMenuOpen();
  }


  void __fastcall VrMenuScenes::renderedMenuHook(
          void*                 main,
          void*                 unused) {
    // In the eye images, the menu's camera would put the model at the same
    // place in both eyes, stuck to the face
    if (!g_renderedMenuMoved)
      reinterpret_cast<MainFn>(VrGame::DrawRenderedMenuParent)(main, nullptr);
  }


  bool VrMenuScenes::isRenderedMenuOpen() {
    void* menu = reinterpret_cast<PointerFn>(VrGame::GetCurrentRenderedMenu)();
    return menu && menu != reinterpret_cast<PointerFn>(VrGame::GetPipBoy)();
  }

}
