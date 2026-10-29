#include <iomanip>
#include <sstream>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_render_passes.h"

namespace dxvk {

  namespace {

    using ClearFn = void (__fastcall*)(void* renderer, void* unused, const void* rect, uint32_t flags);
    using PassFn  = uintptr_t (__fastcall*)(void* main, void* unused);
    using Pass4Fn = uintptr_t (__fastcall*)(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

    // Stereo frames covered by each logged window
    constexpr uint32_t WindowFrames = 900;

    VrRenderPasses* g_renderPasses = nullptr;

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    std::string fixed(double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    }

    bool redirect(uintptr_t site, uintptr_t target, const void* hook, const char* name) {
      if (VrGameMemory::redirectCall(site, target, hook))
        return true;

      Logger::info(str::format("VR: The ", name, " call of the world render was not found, it is not timed per eye"));
      return false;
    }

    // The update passes also step the render threads' stages, which only
    // take effect with multithreaded rendering. Skipping a pass then would
    // leave the other thread waiting for those stages.
    bool multiThreadedRendering() {
      return *reinterpret_cast<const uint8_t*>(VrGame::MultiThreadedRendering) != 0;
    }

    template<typename T>
    T read(uintptr_t address) {
      return *reinterpret_cast<const T*>(address);
    }

    // Shadow lights of the world scene, far above iActorShadowCount
    constexpr uint32_t MaxShadowLights = 256;

    // Each shadow light keeps its shadow box's planes in the clip space of
    // the eye that first drew shadowed geometry; drawing the shadow maps is
    // what makes the next eye compute them again. When the right eye skips
    // that pass, the flag is cleared here instead, or the right eye would
    // cut its shadows with the left eye's planes.
    void forgetShadowClipPlanes() {
      auto node = read<uintptr_t>(VrGame::ShadowSceneNodes);

      if (!node)
        return;

      auto item = read<uintptr_t>(node + VrGame::ShadowSceneNodeShadowLightsOffset);

      for (uint32_t i = 0; item && i < MaxShadowLights; i++) {
        auto light = read<uintptr_t>(item + VrGame::ShadowListItemLightOffset);

        if (light)
          *reinterpret_cast<uint8_t*>(light + VrGame::ShadowLightClipPlanesReadyOffset) = 0;

        item = read<uintptr_t>(item + VrGame::ShadowListItemNextOffset);
      }
    }

  }


  VrRenderPasses::VrRenderPasses(const VrRenderPassConfig& config, ViewFn view)
  : m_config(config), m_view(std::move(view)), m_profile(WindowFrames) { }


  VrRenderPasses::~VrRenderPasses() {
    // The redirected calls stay in place and fall through to the game's functions
    g_renderPasses = nullptr;
  }


  std::unique_ptr<VrRenderPasses> VrRenderPasses::install(const VrRenderPassConfig& config, ViewFn view) {
    if (g_renderPasses || !VrGameMemory::readable(VrGame::MultiThreadedRendering, 1))
      return nullptr;

    VrRenderPassConfig used = config;

    redirect(VrGame::WaterPassCallSite, VrGame::DrawWorldUpdateWater,
      reinterpret_cast<const void*>(&VrRenderPasses::waterHook), "water reflection");
    used.shareShadows = redirect(VrGame::ShadowPassCallSite, VrGame::DrawWorldShadows,
      reinterpret_cast<const void*>(&VrRenderPasses::shadowsHook), "shadow map") && used.shareShadows;

    redirect(VrGame::ScenePassCallSite, VrGame::DrawWorldDrawScene,
      reinterpret_cast<const void*>(&VrRenderPasses::sceneHook), "scene");
    redirect(VrGame::FirstPersonPassCallSite, VrGame::DrawWorldFirstPerson,
      reinterpret_cast<const void*>(&VrRenderPasses::firstPersonHook), "first-person");
    redirect(VrGame::ImageSpacePassCallSite, VrGame::DrawWorldImageSpace,
      reinterpret_cast<const void*>(&VrRenderPasses::imageSpaceHook), "image space");

    if (used.handsDepthTest) {
      used.handsDepthTest = VrGameMemory::redirectCall(VrGame::FirstPersonDepthClearSite,
        VrGame::RendererClearBuffer, reinterpret_cast<const void*>(&VrRenderPasses::firstPersonClearHook));

      Logger::info(used.handsDepthTest
        ? "VR: The hands and the weapon are hidden by the world in front of them"
        : "VR: The depth clear of the first-person render was not found, the hands are drawn over the world");
    }

    if (used.shareShadows)
      Logger::info("VR: Shadow maps are drawn once per frame for both eyes");

    std::unique_ptr<VrRenderPasses> passes(new VrRenderPasses(used, std::move(view)));
    g_renderPasses = passes.get();
    return passes;
  }


  void VrRenderPasses::beginEye(uint32_t eye) {
    m_eye      = eye;
    m_eyeStart = high_resolution_clock::now();

    if (eye == 0)
      m_shadowsDrawnForLeft = false;
  }


  void VrRenderPasses::endEye() {
    if (m_eye == NoEye)
      return;

    auto ms = std::chrono::duration<double, std::milli>(high_resolution_clock::now() - m_eyeStart).count();
    m_profile.add(m_eye, VrRenderPass::Eye, ms);
    m_eye = NoEye;
  }


  void VrRenderPasses::endFrame() {
    if (m_profile.endFrame()) {
      logWindow();
      m_profile.reset();
      m_shadowsReused = 0;
    }
  }


  template<typename Fn>
  uintptr_t VrRenderPasses::runPass(VrRenderPass pass, Fn&& fn) {
    if (m_eye == NoEye)
      return fn();

    uint32_t eye = m_eye;
    auto start = high_resolution_clock::now();
    uintptr_t result = fn();

    m_profile.add(eye, pass,
      std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count());
    return result;
  }


  template<typename Fn>
  uintptr_t VrRenderPasses::runShadows(Fn&& fn) {
    if (m_eye == NoEye || !sharesShadows())
      return runPass(VrRenderPass::Shadows, std::forward<Fn>(fn));

    // The right eye keeps the maps drawn for the left eye
    if (m_eye == 1 && m_shadowsDrawnForLeft) {
      forgetShadowClipPlanes();
      m_shadowsReused += 1;
      return 0;
    }

    // Shadows are kept or dropped by what the camera sees: the left eye
    // draws them for the view of both eyes
    VrGameCameraState saved;
    VrGameCameraPose pose;
    VrGameFrustum frustum;

    bool moved = m_camera.acquire() && m_view && m_view(pose, frustum);

    if (moved) {
      saved = m_camera.save();
      m_camera.apply(pose, frustum);
    }

    uintptr_t result = runPass(VrRenderPass::Shadows, std::forward<Fn>(fn));

    if (moved)
      m_camera.restore(saved);

    if (m_eye == 0)
      m_shadowsDrawnForLeft = true;

    return result;
  }


  bool VrRenderPasses::sharesShadows() const {
    return m_config.shareShadows && !multiThreadedRendering();
  }


  void VrRenderPasses::logWindow() {
    auto times = [this] (VrRenderPass pass) {
      return str::format(
        fixed(m_profile.averageMs(0, pass)), "/", fixed(m_profile.maxMs(0, pass)), " | ",
        fixed(m_profile.averageMs(1, pass)), "/", fixed(m_profile.maxMs(1, pass)));
    };

    Logger::info(str::format("VR: Render per eye over ", m_profile.frames(),
      " frames, average/longest ms for the left | right eye: whole eye ", times(VrRenderPass::Eye),
      ", shadow maps ", times(VrRenderPass::Shadows),
      ", water ", times(VrRenderPass::Water),
      ", scene ", times(VrRenderPass::Scene),
      ", first person ", times(VrRenderPass::FirstPerson),
      ", image space ", times(VrRenderPass::ImageSpace),
      ", rest of the eye ", fixed(m_profile.otherMs(0)), " | ", fixed(m_profile.otherMs(1)),
      ". Shadow maps reused by the right eye in ", m_shadowsReused, " frames"));
  }


  uintptr_t __fastcall VrRenderPasses::waterHook(void* main, void* unused) {
    auto original = game<PassFn>(VrGame::DrawWorldUpdateWater);

    if (!g_renderPasses)
      return original(main, nullptr);

    return g_renderPasses->runPass(VrRenderPass::Water,
      [&] { return original(main, nullptr); });
  }


  uintptr_t __fastcall VrRenderPasses::shadowsHook(void* main, void* unused) {
    auto original = game<PassFn>(VrGame::DrawWorldShadows);

    if (!g_renderPasses)
      return original(main, nullptr);

    return g_renderPasses->runShadows(
      [&] { return original(main, nullptr); });
  }


  uintptr_t __fastcall VrRenderPasses::sceneHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    auto original = game<Pass4Fn>(VrGame::DrawWorldDrawScene);

    if (!g_renderPasses)
      return original(main, nullptr, a, b, c, d);

    return g_renderPasses->runPass(VrRenderPass::Scene,
      [&] { return original(main, nullptr, a, b, c, d); });
  }


  uintptr_t __fastcall VrRenderPasses::firstPersonHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    auto original = game<Pass4Fn>(VrGame::DrawWorldFirstPerson);

    if (!g_renderPasses)
      return original(main, nullptr, a, b, c, d);

    return g_renderPasses->runPass(VrRenderPass::FirstPerson,
      [&] { return original(main, nullptr, a, b, c, d); });
  }


  uintptr_t __fastcall VrRenderPasses::imageSpaceHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    auto original = game<Pass4Fn>(VrGame::DrawWorldImageSpace);

    if (!g_renderPasses)
      return original(main, nullptr, a, b, c, d);

    return g_renderPasses->runPass(VrRenderPass::ImageSpace,
      [&] { return original(main, nullptr, a, b, c, d); });
  }


  void __fastcall VrRenderPasses::firstPersonClearHook(void* renderer, void* unused,
      const void* rect, uint32_t flags) {
    // The world's depth stays for the first-person model
    if (g_renderPasses && g_renderPasses->m_config.handsDepthTest)
      flags &= ~VrGame::ClearDepthBuffer;

    if (flags)
      game<ClearFn>(VrGame::RendererClearBuffer)(renderer, nullptr, rect, flags);
  }

}
