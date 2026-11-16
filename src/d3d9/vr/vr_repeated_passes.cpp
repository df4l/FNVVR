#include <cstring>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_repeated_passes.h"

namespace dxvk {

  namespace {

    using RealTimeFn = float (__fastcall*)(void* timer, void* unused);
    using ScreenshotFn = void (__cdecl*)(const char* name);
    using WaterFn = void (__fastcall*)(void* water, void* unused);
    using ArrayAddFn = uintptr_t (__fastcall*)(void* array, void* unused, void* item);
    using RenderEffectFn = void (__fastcall*)(void* manager, void* unused,
      uint32_t effect, void* renderer, void* source, void* target, void* parameter, uint32_t flag);

    VrRepeatedPasses* g_repeatedPasses = nullptr;

    // A repeated pass is being drawn
    bool g_repeating = false;

    // The step BSFadeNode::OnVisible reads instead of the game timer's
    float g_fadeStep = 0.0f;

    // The functions the redirected calls reached: the game's, or another mod's hook
    uintptr_t g_realTime        = VrGame::TimerRealTimeSeconds;
    uintptr_t g_screenshot      = VrGame::TakeScreenshot;
    uintptr_t g_waterActors     = VrGame::WaterUpdateActors;
    uintptr_t g_waterSounds     = VrGame::WaterUpdateSounds;
    uintptr_t g_displaceStencil = VrGame::WaterDisplaceStencil;
    uintptr_t g_displaceAdd     = VrGame::SimpleArrayAdd;
    uintptr_t g_displaceRender  = VrGame::ImageSpaceRenderEffect;
    uintptr_t g_noiseRender     = VrGame::ImageSpaceRenderEffect2;

    float& timerStep() {
      return *reinterpret_cast<float*>(VrGame::AppTimer + VrGame::TimerStepAfterMultiplier);
    }

    float& shaderTimeStep() {
      return *reinterpret_cast<float*>(VrGame::ShaderTimeStep);
    }

    bool skips() {
      return g_repeatedPasses && g_repeating;
    }

    void fadeStepRead(uint8_t (&code)[sizeof(VrGame::FadeStepRead)], const float* step) {
      auto address = uint32_t(reinterpret_cast<uintptr_t>(step));

      std::memcpy(code, VrGame::FadeStepRead, sizeof(code));
      std::memcpy(code + 2, &address, sizeof(address));
    }

  }


  VrRepeatedPasses::~VrRepeatedPasses() {
    if (m_repeating)
      endRepeat();

    // The redirected calls stay in place and fall through to the game's
    // functions, but the fades read the game's timer again
    if (m_fadeStepFound) {
      uint8_t patched[sizeof(VrGame::FadeStepRead)];
      fadeStepRead(patched, &g_fadeStep);

      for (uintptr_t site : VrGame::FadeStepReadSites)
        VrGameMemory::replaceCode(site, patched, VrGame::FadeStepRead, sizeof(patched));
    }

    g_repeatedPasses = nullptr;
    g_repeating      = false;
  }


  std::unique_ptr<VrRepeatedPasses> VrRepeatedPasses::install() {
    if (g_repeatedPasses || !VrGameMemory::readable(VrGame::AppTimer, VrGame::TimerSize)
     || !VrGameMemory::readable(VrGame::ShaderTimeStep, sizeof(float)))
      return nullptr;

    std::unique_ptr<VrRepeatedPasses> passes(new VrRepeatedPasses());

    passes->m_timeStepFound = VrGameMemory::matches(VrGame::ShaderGetTimer,
      VrGame::ShaderGetTimerCode, sizeof(VrGame::ShaderGetTimerCode));

    if (!passes->m_timeStepFound)
      Logger::info("VR: The shader timers were not found, the water, light fades and the eyes' adaptation move once per eye");

    passes->installFadeStep();
    passes->installSkips();

    g_repeatedPasses = passes.get();
    return passes;
  }


  void VrRepeatedPasses::beginFrame() {
    g_fadeStep = timerStep();
  }


  void VrRepeatedPasses::beginRepeat() {
    if (m_repeating)
      return;

    m_repeating = true;
    g_repeating = true;
    g_fadeStep  = 0.0f;

    if (m_timeStepFound) {
      m_savedTimeStep  = shaderTimeStep();
      shaderTimeStep() = 0.0f;
    }
  }


  void VrRepeatedPasses::endRepeat() {
    if (!m_repeating)
      return;

    if (m_timeStepFound)
      shaderTimeStep() = m_savedTimeStep;

    g_fadeStep  = timerStep();
    g_repeating = false;
    m_repeating = false;
  }


  void VrRepeatedPasses::installFadeStep() {
    for (uintptr_t site : VrGame::FadeStepReadSites) {
      if (!VrGameMemory::matches(site, VrGame::FadeStepRead, sizeof(VrGame::FadeStepRead))) {
        Logger::info(str::format("VR: The fade step read at 0x", std::hex, site,
          " was not found, objects fade in and out once per eye"));
        return;
      }
    }

    g_fadeStep = timerStep();

    uint8_t patched[sizeof(VrGame::FadeStepRead)];
    fadeStepRead(patched, &g_fadeStep);

    for (uintptr_t site : VrGame::FadeStepReadSites)
      VrGameMemory::replaceCode(site, VrGame::FadeStepRead, patched, sizeof(patched));

    m_fadeStepFound = true;
  }


  void VrRepeatedPasses::installSkips() {
    struct Skip {
      uintptr_t   site;
      uintptr_t   target;
      const void* hook;
      uintptr_t*  previous;
      const char* what;
    };

    const Skip skips[] = {
      { VrGame::SwapFaderTimeCallSite, VrGame::TimerRealTimeSeconds,
        reinterpret_cast<const void*>(&realTimeHook), &g_realTime,
        "the loading screen's black fader goes twice as fast" },
      { VrGame::SwapScreenshotCallSite, VrGame::TakeScreenshot,
        reinterpret_cast<const void*>(&screenshotHook), &g_screenshot,
        "a screenshot is taken once per eye" },
      { VrGame::WaterActorsCallSite, VrGame::WaterUpdateActors,
        reinterpret_cast<const void*>(&waterActorsHook), &g_waterActors,
        "wading actors are updated once per eye" },
      { VrGame::WaterSoundsCallSite, VrGame::WaterUpdateSounds,
        reinterpret_cast<const void*>(&waterSoundsHook), &g_waterSounds,
        "the water sounds are placed once per eye" },
      { VrGame::WaterDisplaceStencilCallSite, VrGame::WaterDisplaceStencil,
        reinterpret_cast<const void*>(&displaceStencilHook), &g_displaceStencil,
        "the wading stencil is drawn once per eye" },
      { VrGame::WaterDisplaceRenderCallSite, VrGame::ImageSpaceRenderEffect,
        reinterpret_cast<const void*>(&displaceRenderHook), &g_displaceRender,
        "wading ripples are struck once per eye" },
      { VrGame::WaterNoiseRenderCallSite, VrGame::ImageSpaceRenderEffect2,
        reinterpret_cast<const void*>(&noiseRenderHook), &g_noiseRender,
        "the water's noise map is drawn once per eye" },
    };

    uint32_t taken = 0;

    for (const Skip& skip : skips) {
      if (VrGameMemory::chainCall(skip.site, skip.target, skip.hook, skip.previous))
        taken += 1;
      else
        Logger::info(str::format("VR: The call at 0x", std::hex, skip.site, " was not found, ", skip.what));
    }

    // Both calls fill the arrays the displacement effect empties: one without
    // the other would leave half of them behind
    if (VrGameMemory::chainCalls(VrGame::WaterDisplaceAddCallSites, std::size(VrGame::WaterDisplaceAddCallSites),
        VrGame::SimpleArrayAdd, reinterpret_cast<const void*>(&displaceAddHook), &g_displaceAdd))
      taken += 1;
    else
      Logger::info("VR: The wading offsets were not found, wading ripples are added once per eye");

    Logger::info(str::format("VR: The world passes after the left eye's see no time passing",
      m_fadeStepFound ? ", fades included" : "", ", ", taken, " of ", std::size(skips) + 1,
      " pieces of work they repeated are skipped"));
  }


  float __fastcall VrRepeatedPasses::realTimeHook(void* timer, void* unused) {
    float seconds = reinterpret_cast<RealTimeFn>(g_realTime)(timer, nullptr);
    return skips() ? 0.0f : seconds;
  }


  void __cdecl VrRepeatedPasses::screenshotHook(const char* name) {
    if (!skips())
      reinterpret_cast<ScreenshotFn>(g_screenshot)(name);
  }


  void __fastcall VrRepeatedPasses::waterActorsHook(void* water, void* unused) {
    if (!skips())
      reinterpret_cast<WaterFn>(g_waterActors)(water, nullptr);
  }


  void __fastcall VrRepeatedPasses::waterSoundsHook(void* water, void* unused) {
    if (!skips())
      reinterpret_cast<WaterFn>(g_waterSounds)(water, nullptr);
  }


  void __fastcall VrRepeatedPasses::displaceStencilHook(void* water, void* unused) {
    if (!skips())
      reinterpret_cast<WaterFn>(g_displaceStencil)(water, nullptr);
  }


  uintptr_t __fastcall VrRepeatedPasses::displaceAddHook(void* array, void* unused, void* item) {
    return skips() ? 0 : reinterpret_cast<ArrayAddFn>(g_displaceAdd)(array, nullptr, item);
  }


  void __fastcall VrRepeatedPasses::displaceRenderHook(void* manager, void* unused,
      uint32_t effect, void* renderer, void* source, void* target, void* parameter, uint32_t flag) {
    if (!skips())
      reinterpret_cast<RenderEffectFn>(g_displaceRender)(manager, nullptr, effect, renderer, source, target, parameter, flag);
  }


  void __fastcall VrRepeatedPasses::noiseRenderHook(void* manager, void* unused,
      uint32_t effect, void* renderer, void* source, void* target, void* parameter, uint32_t flag) {
    if (!skips())
      reinterpret_cast<RenderEffectFn>(g_noiseRender)(manager, nullptr, effect, renderer, source, target, parameter, flag);
  }

}
