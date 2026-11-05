#include <cstring>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_menu_scenes.h"
#include "vr_pipboy_render.h"
#include "vr_sky_anchor.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  namespace {

    // __thiscall functions on Main are called as __fastcall with an unused
    // second argument, which passes this in ECX and nothing in EDX
    using MainFn          = void  (__fastcall*)(void* main, void* unused);
    using RenderFn        = void  (__fastcall*)(void* main, void* unused,
      void* target, uint32_t renderedMenu, uint32_t flag);
    using InitTargetFn    = void* (__fastcall*)(void* main, void* unused,
      void* renderer, void* destination, uint32_t disableImageSpace, uint32_t multisampling);
    using CopyBackgroundFn = void (__fastcall*)(void* main, void* unused,
      void* imageSpaceManager, void* renderer, uint32_t multisampling, void* destination);
    using RestartTargetFn = void  (__fastcall*)(void* main, void* unused,
      void* target, uint32_t multisampling, uint32_t clearFlags);
    using DrawSceneFn     = void  (__fastcall*)(void* main, void* unused,
      void* sun, uint32_t drawHands, uint32_t wireframe, uint32_t vats);
    using AccumulatorPassFn = void (__fastcall*)(void* main, void* unused,
      void* accumulator, void* target);
    using ScreenFn        = void  (__fastcall*)(void* main, void* unused,
      void* screen, void* renderer);
    using ObjectFn        = uint32_t (__fastcall*)(void* object, void* unused);
    using GetEffectFn     = void* (__fastcall*)(void* manager, void* unused, uint32_t id);
    using SettingIntFn    = int32_t* (__fastcall*)(void* setting, void* unused);
    using BoolFn          = uint32_t (__cdecl*)();
    using PointerFn       = void* (__cdecl*)();

    VrPipBoyRender* g_pipBoyRender = nullptr;

    // The function the world branch called: the game's, or another mod's hook
    uintptr_t g_worldRender = VrGame::Render;

    // Read instead of the thread count while the world is drawn behind the
    // Pip-Boy
    int32_t g_noThreads = 0;

    template<typename T>
    T read(uintptr_t address) {
      T value;
      std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
      return value;
    }

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    bool returnsTrue(uintptr_t function) {
      return (game<BoolFn>(function)() & 0xFF) != 0;
    }

  }


  VrPipBoyRender::~VrPipBoyRender() {
    // The patched calls stay in place and fall through to the game's functions
    g_pipBoyRender = nullptr;
  }


  std::unique_ptr<VrPipBoyRender> VrPipBoyRender::install(ScreenPassFn screenPass) {
    if (g_pipBoyRender)
      return nullptr;

    struct Redirect {
      uintptr_t   site;
      uintptr_t   target;
      const void* hook;
    };

    const Redirect redirects[] = {
      { VrGame::PipBoyBackgroundCallSite,       VrGame::PipBoyBackground,   reinterpret_cast<const void*>(&backgroundHook) },
      { VrGame::RaceSexBackgroundCallSite,      VrGame::PipBoyBackground,   reinterpret_cast<const void*>(&backgroundHook) },
      { VrGame::RenderedMenuTargetCallSite,    VrGame::InitRenderTexture,  reinterpret_cast<const void*>(&targetHook) },
      { VrGame::RenderedMenuBackgroundCallSite, VrGame::CopyMenuBackground, reinterpret_cast<const void*>(&copyBackgroundHook) },
      { VrGame::DrawSceneThreadsCallSite,       VrGame::SettingInt,         reinterpret_cast<const void*>(&threadsSettingHook) },
      { VrGame::PipBoyScreenCallSite,           VrGame::RenderPipBoy,       reinterpret_cast<const void*>(&screenHook) },
    };

    // All sites are checked before any is patched, so that a different
    // executable is left as it is. JohnnyGuitar holds the world branch's call
    // for its render events, and its hook stays in the chain.
    bool found = VrGameMemory::canChainCall(VrGame::WorldRenderCallSite, VrGame::Render);

    for (const auto& redirect : redirects)
      found = found && VrGameMemory::callsTo(redirect.site, redirect.target);

    if (!found) {
      Logger::info("VR: The Pip-Boy render was not found, the Pip-Boy shows a frozen background");
      return nullptr;
    }

    for (const auto& redirect : redirects)
      VrGameMemory::redirectCall(redirect.site, redirect.target, redirect.hook);

    VrGameMemory::chainCall(VrGame::WorldRenderCallSite, VrGame::Render,
      reinterpret_cast<const void*>(&worldRenderHook), &g_worldRender);

    std::unique_ptr<VrPipBoyRender> render(new VrPipBoyRender());
    render->m_screenPass = std::move(screenPass);
    g_pipBoyRender = render.get();
    return render;
  }


  void __fastcall VrPipBoyRender::backgroundHook(
          void*                 main,
          void*                 unused) {
    // Without the frozen background, its flag stays clear and the world
    // branch below draws the Pip-Boy or the character creation
    if (!g_pipBoyRender)
      game<MainFn>(VrGame::PipBoyBackground)(main, nullptr);
  }


  void __fastcall VrPipBoyRender::worldRenderHook(
          void*                 main,
          void*                 unused,
          void*                 target,
          uint32_t              renderedMenu,
          uint32_t              flag) {
    // The game's menu branch passes the same value: Render then draws the
    // rendered menu, unless the pause menu is open over it
    if (g_pipBoyRender && returnsTrue(VrGame::IsPipBoyOpen))
      renderedMenu = 1;

    game<RenderFn>(g_worldRender)(main, nullptr, target, renderedMenu, flag);
  }


  void* __fastcall VrPipBoyRender::targetHook(
          void*                 main,
          void*                 unused,
          void*                 renderer,
          void*                 destination,
          uint32_t              disableImageSpace,
          uint32_t              multisampling) {
    void* target = game<InitTargetFn>(VrGame::InitRenderTexture)(
      main, nullptr, renderer, destination, disableImageSpace, multisampling);

    if (g_pipBoyRender)
      g_pipBoyRender->m_target = target;

    return target;
  }


  void __fastcall VrPipBoyRender::copyBackgroundHook(
          void*                 main,
          void*                 unused,
          void*                 imageSpaceManager,
          void*                 renderer,
          uint32_t              multisampling,
          void*                 destination) {
    // Without a frozen background there is nothing to copy: the world
    // branch draws the rendered menu, and the world is drawn behind it
    bool frozen = read<uint8_t>(VrGame::MenuBackgroundReady) && !returnsTrue(VrGame::IsPipBoyShown);

    if (!g_pipBoyRender || !g_pipBoyRender->m_target || frozen) {
      game<CopyBackgroundFn>(VrGame::CopyMenuBackground)(
        main, nullptr, imageSpaceManager, renderer, multisampling, destination);
      return;
    }

    g_pipBoyRender->drawWorld(static_cast<uint8_t*>(main), g_pipBoyRender->m_target);
    g_pipBoyRender->m_target = nullptr;
  }


  int32_t* __fastcall VrPipBoyRender::threadsSettingHook(
          void*                 setting,
          void*                 unused) {
    if (g_pipBoyRender && g_pipBoyRender->m_drawingWorld)
      return &g_noThreads;

    return game<SettingIntFn>(VrGame::SettingInt)(setting, nullptr);
  }


  void __fastcall VrPipBoyRender::screenHook(
          void*                 main,
          void*                 unused,
          void*                 screen,
          void*                 renderer) {
    // Other rendered menus, such as the character creation, are drawn on
    // the menu panel
    if (VrMenuScenes::movesRenderedMenu())
      return;

    VrVector3 shift;

    if (!g_pipBoyRender || !g_pipBoyRender->m_screenPass || !firstPersonShift(shift)) {
      game<ScreenFn>(VrGame::RenderPipBoy)(main, nullptr, screen, renderer);
      return;
    }

    if (!g_pipBoyRender->m_loggedScreen) {
      Logger::info("VR: The Pip-Boy screen is drawn with the eye's camera");
      g_pipBoyRender->m_loggedScreen = true;
    }

    // Copied first: the callback may replace itself
    ScreenPassFn screenPass = g_pipBoyRender->m_screenPass;

    screenPass(&shift);
    game<ScreenFn>(VrGame::RenderPipBoy)(main, nullptr, screen, renderer);
    screenPass(nullptr);
  }


  void VrPipBoyRender::drawWorld(uint8_t* main, void* target) {
    // The same steps as the game's normal world render, without the
    // first-person model, which the rendered-menu render draws after this
    uint32_t multisampling = game<BoolFn>(VrGame::IsMultisampling)() & 0xFF;

    auto shadowSceneNode = read<void*>(VrGame::ShadowSceneNodes);

    if (shadowSceneNode && (game<ObjectFn>(VrGame::IsWireframe)(shadowSceneNode, nullptr) & 0xFF))
      return;

    if (!m_loggedWorld) {
      Logger::info("VR: The live world is drawn behind the Pip-Boy and the character creation");
      m_loggedWorld = true;
    }

    VrSkyAnchor::drawWorldInit(main);
    game<MainFn>(VrGame::DrawWorldUpdateWater)(main, nullptr);
    game<RestartTargetFn>(VrGame::DrawWorldRestartTarget)(main, nullptr, target, multisampling, VrGame::ClearBackAndDepth);

    m_drawingWorld = true;
    game<DrawSceneFn>(VrGame::DrawWorldDrawScene)(main, nullptr, nullptr, 0, 0, 0);
    m_drawingWorld = false;

    auto accumulator = *reinterpret_cast<void**>(main + VrGame::MainWorldAccumulator);

    if (accumulator && returnsTrue(VrGame::IsRefractionEnabled)
     && game<ObjectFn>(VrGame::RefractivePassCount)(accumulator, nullptr))
      game<AccumulatorPassFn>(VrGame::DrawWorldRefraction)(main, nullptr, accumulator, target);

    void* imageSpace = game<PointerFn>(VrGame::ImageSpaceManagerSingleton)();
    void* depthOfField = imageSpace
      ? game<GetEffectFn>(VrGame::ImageSpaceGetEffect)(imageSpace, nullptr, VrGame::ImageSpaceDepthOfField)
      : nullptr;

    if (accumulator && depthOfField && (game<ObjectFn>(VrGame::ImageSpaceDoDepth)(depthOfField, nullptr) & 0xFF))
      game<AccumulatorPassFn>(VrGame::DrawWorldDoDepth)(main, nullptr, accumulator, target);

    // The rendered-menu render applies the image space effects once the
    // first-person model is drawn, as the normal world render does. With the
    // frozen background's flag clear it applies all of them, so applying
    // them here too would tone map and tint the world twice.
    game<RestartTargetFn>(VrGame::DrawWorldRestartTarget)(main, nullptr, target, multisampling, VrGame::ClearDepth);
  }


  bool VrPipBoyRender::firstPersonShift(VrVector3& shift) {
    // The model is drawn with its root's translation replaced by the draw
    // origin. Both points are brought to the world by the root's parent, so
    // the shift between the spaces is the parent's transform of their
    // difference.
    auto player = read<uint8_t*>(VrGame::Player);
    auto root   = player ? *reinterpret_cast<uint8_t**>(player + VrGame::PlayerFirstPersonRoot) : nullptr;
    auto parent = root ? *reinterpret_cast<uint8_t**>(root + VrGame::ObjectParent) : nullptr;

    if (!parent)
      return false;

    VrVector3 local, origin;
    std::memcpy(&local, root + VrGame::NodeLocalTranslate, sizeof(local));
    std::memcpy(&origin, reinterpret_cast<const void*>(VrGame::FirstPersonDrawOrigin), sizeof(origin));

    VrGameRotation rotate;
    std::memcpy(rotate.m, parent + VrGame::ObjectWorldRotation, sizeof(rotate.m));
    float scale = *reinterpret_cast<const float*>(parent + VrGame::ObjectWorldScale);

    shift = (rotate * (local - origin)) * scale;
    return true;
  }

}
