#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <memory>

#include "../../dxvk/dxvk_instance.h"

#include "../../util/config/config.h"
#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_backend_factory.h"
#include "vr_crash_log.h"
#include "vr_d3d9_bridge.h"
#include "vr_emulator_backend.h"
#include "vr_emulator_keyboard.h"
#include "vr_extension_provider.h"
#include "vr_game_console.h"
#include "vr_game_settings.h"
#include "vr_game_space.h"
#include "vr_gamepad_head.h"
#include "vr_gamepad_hook.h"
#include "vr_large_address.h"
#include "vr_live_casino.h"
#include "vr_engine_patches.h"
#include "vr_game_heap.h"
#include "vr_fast_math.h"
#include "vr_sound_focus.h"
#include "vr_game_window.h"
#include "vr_launcher_overlay.h"
#include "vr_menu_cursor.h"
#include "vr_options_file.h"
#include "vr_options_menu.h"
#include "vr_stereo_renderer.h"
#include "vr_system.h"
#include "vr_text_files.h"
#include "vr_weapon_types.h"

namespace dxvk {

  static std::unique_ptr<VrSystem> g_vrSystem;
  static bool                      g_vrInitialized = false;

  // Never deleted: it runs on its own thread until the launcher's process ends
  static VrLauncherOverlay*        g_launcherOverlay = nullptr;


  namespace {

    std::wstring lowercase(std::wstring text) {
      for (wchar_t& c : text)
        c = wchar_t(std::towlower(c));
      return text;
    }

    /**
     * \brief Logs the modules loaded from outside the Windows folder
     *
     * Script extenders, plugins and other graphics wrappers (ENB, ReShade)
     * can change the camera or the image, so their presence is part of
     * what a distortion report needs.
     */
    void logLoadedModules() {
      WCHAR windows[MAX_PATH] = { };
      UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
      std::wstring windowsPath = lowercase(std::wstring(windows, length < MAX_PATH ? length : 0));

      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);

      if (snapshot == INVALID_HANDLE_VALUE)
        return;

      std::string list;
      MODULEENTRY32W entry = { };
      entry.dwSize = sizeof(entry);

      for (BOOL found = Module32FirstW(snapshot, &entry); found; found = Module32NextW(snapshot, &entry)) {
        std::wstring path = lowercase(entry.szExePath);

        if (!windowsPath.empty() && path.compare(0, windowsPath.size(), windowsPath) == 0)
          continue;

        if (!list.empty())
          list += ", ";

        list += str::fromws(entry.szExePath);
      }

      CloseHandle(snapshot);
      Logger::info(str::format("VR: Modules loaded outside the Windows folder: ", list));
    }

  }


  VrSystem::VrSystem(std::unique_ptr<IVRBackend> backend, bool showPreview,
    const VrPanelConfig& panel, const VrTurnConfig& turn, bool weaponInHand, bool headsetResolution,
    const VrGameTimerConfig& timer, const VrRenderPassConfig& passes, const VrDeveloperConfig& developer,
    const VrSwingConfig& swing)
  : m_backend(std::move(backend)),
    m_extensionProvider(std::make_unique<VrExtensionProvider>(*m_backend)),
    m_showPreview(showPreview),
    m_panelConfig(panel),
    m_turnConfig(turn),
    m_weaponInHand(weaponInHand),
    m_timerConfig(timer),
    m_passConfig(passes),
    m_developerConfig(developer),
    m_swingConfig(swing),
    m_headsetResolution(headsetResolution) { }


  VrSystem::~VrSystem() {
    detachDevice();
  }


  void VrSystem::initialize() {
    if (g_vrInitialized)
      return;

    g_vrInitialized = true;

    Config config = Config::getUserConfig();
    config.merge(Config::getAppConfig(env::getExePath()));

    std::string name = env::getEnvVar("DXVK_VR_BACKEND");

    if (name.empty())
      name = config.getOption<std::string>("d3d9.vrBackend", "off");

    if (name == "off" || name.empty())
      return;

    // The launcher loads this DLL too, and has no game to render
    if (VrLauncherOverlay::isLauncherProcess()) {
      if (name == "openvr" && config.getOption<bool>("d3d9.vrLauncherOverlay", true)) {
        VrPanelConfig panel;
        g_launcherOverlay = new VrLauncherOverlay(name,
          config.getOption<float>("d3d9.vrPanelDistance", panel.distance),
          config.getOption<float>("d3d9.vrPanelWidth", panel.width));
      }

      return;
    }

    std::unique_ptr<IVRBackend> backend = vrCreateBackend(name);

    if (!backend) {
      Logger::err(str::format("VR: Backend '", name, "' is unknown or not available, VR is disabled"));
      return;
    }

    Logger::info(str::format("VR: Using backend '", backend->name(), "'"));

    VrCrashLog::install();

    // The emulator has no headset to look through, so it shows a window by default
    bool showPreview = config.getOption<bool>("d3d9.vrPreviewWindow", name == "emulator");

    VrPanelConfig panel;
    panel.distance = config.getOption<float>("d3d9.vrPanelDistance", panel.distance);
    panel.width    = config.getOption<float>("d3d9.vrPanelWidth",    panel.width);
    panel.hudDistance = config.getOption<float>("d3d9.vrHudDistance", panel.hudDistance);
    panel.hudWidth    = config.getOption<float>("d3d9.vrHudWidth",    panel.hudWidth);
    panel.hudHeight   = config.getOption<float>("d3d9.vrHudHeight",   panel.hudHeight);
    panel.hudMessagesOffset = config.getOption<float>("d3d9.vrHudMessagesOffset", panel.hudMessagesOffset);

    VrTurnConfig turn;
    turn.smooth      = config.getOption<bool>("d3d9.vrSmoothTurn", turn.smooth);
    turn.snapAngle   = config.getOption<float>("d3d9.vrSnapTurnAngle", turn.snapAngle);
    turn.smoothSpeed = config.getOption<float>("d3d9.vrSmoothTurnSpeed", turn.smoothSpeed);

    // What the player picked on the options page overrides dxvk.conf
    VrOptions options;
    options.smoothTurn      = turn.smooth;
    options.smoothTurnSpeed = turn.smoothSpeed;
    options.pipBoyScale     = std::min(std::max(config.getOption<float>("d3d9.vrPipBoyScale", options.pipBoyScale),
      VrPipBoyScaleMin), vrPipBoyStepToScale(VrPipBoyScaleSteps - 1));
    options.manualReload    = config.getOption<bool>("d3d9.vrManualReload", options.manualReload);
    options.physicalSneak   = config.getOption<bool>("d3d9.vrPhysicalSneak", options.physicalSneak);
    options.scopeLens       = config.getOption<bool>("d3d9.vrScopes", options.scopeLens);
    options.roomscale       = config.getOption<bool>("d3d9.vrRoomscale", options.roomscale);
    VrOptionsFile::load(options);
    turn.smooth      = options.smoothTurn;
    turn.smoothSpeed = options.smoothTurnSpeed;

    bool emulated = dynamic_cast<VrEmulatorBackend*>(backend.get()) != nullptr;

    if (auto* emulator = dynamic_cast<VrEmulatorBackend*>(backend.get())) {
      VrEmulatorKeyboard keyboard;
      emulator->setInputSource([keyboard] {
        return vrMergeInput(keyboard.sample(), VrGamepadHook::headInput());
      });

      VrGamepadHook::Options gamepad;
      gamepad.virtualPad  = config.getOption<bool>("d3d9.vrVirtualGamepad", true);
      gamepad.headControl = config.getOption<bool>("d3d9.vrGamepadHead", true);

      if (gamepad.virtualPad || gamepad.headControl)
        VrGamepadHook::install(gamepad);
    } else {
      // A headset session can come with a gamepad the player never touches
      // (Steam's virtual controllers), which takes the mouse cursor away.
      // The VR controllers take its place while they are in use.
      VrGamepadHook::Options gamepad;
      gamepad.hidePad     = config.getOption<bool>("d3d9.vrHideGamepad", true);
      gamepad.controllers = config.getOption<bool>("d3d9.vrControllers", true);

      // The controllers point at the menu panel with the game's cursor
      if ((gamepad.hidePad || gamepad.controllers) && VrGamepadHook::install(gamepad) && gamepad.controllers)
        VrMenuCursor::install();
    }

    bool weaponInHand = config.getOption<bool>("d3d9.vrWeaponInHand", true);
    bool headsetResolution = config.getOption<bool>("d3d9.vrHeadsetResolution", true);

    VrGameTimerConfig timer;
    timer.preciseStep   = config.getOption<bool>("d3d9.vrPreciseGameTimer", timer.preciseStep);
    timer.havokPerFrame = config.getOption<bool>("d3d9.vrHavokStepPerFrame", timer.havokPerFrame);

    VrRenderPassConfig passes;
    passes.shareShadows = config.getOption<bool>("d3d9.vrShareShadows", passes.shareShadows);
    passes.handsDepthTest = config.getOption<bool>("d3d9.vrHandsDepthTest", passes.handsDepthTest);
    passes.scopeResolution = uint32_t(std::max<int32_t>(0,
      config.getOption<int32_t>("d3d9.vrScopeResolution", int32_t(passes.scopeResolution))));

    VrDeveloperConfig developer;
    developer.enabled = config.getOption<bool>("d3d9.vrDeveloperMode", developer.enabled);

    if (developer.enabled) {
      std::vector<std::string> unknown;
      developer.weaponTypes = vrParseWeaponTypes(config.getOption<std::string>("d3d9.vrDeveloperWeapons", ""), unknown);

      for (const auto& name : unknown)
        Logger::warn(str::format("VR: d3d9.vrDeveloperWeapons: '", name, "' is not a weapon type, it is ignored"));
    }

    // The head rises back by more than the hysteresis before standing
    VrCrouchConfig crouch;
    crouch.depth = std::min(std::max(config.getOption<float>("d3d9.vrCrouchDepth", crouch.depth),
      2.0f * crouch.hysteresis), 1.0f);

    VrSwingConfig swing;
    swing.hitSpeed = config.getOption<float>("d3d9.vrMeleeHitSpeed", swing.hitSpeed);
    swing.hitsPerSecond = std::min(std::max(config.getOption<float>("d3d9.vrMeleeHitsPerSecond",
      swing.hitsPerSecond), 1.0f), 30.0f);
    swing.powerCost.base = config.getOption<int32_t>("d3d9.vrPowerAttackApCost", swing.powerCost.base);
    swing.powerCost.max  = config.getOption<int32_t>("d3d9.vrPowerAttackApCostMax", swing.powerCost.max);
    swing.powerCost.weightMult = config.getOption<float>("d3d9.vrPowerAttackApWeightMult", swing.powerCost.weightMult);

    g_vrSystem.reset(new VrSystem(std::move(backend), showPreview, panel, turn, weaponInHand, headsetResolution,
      timer, passes, developer, swing));
    g_vrSystem->m_texts = vrReadTexts();
    g_vrSystem->m_pipBoyScale = options.pipBoyScale;
    g_vrSystem->m_manualReload = options.manualReload;
    g_vrSystem->m_handPush = config.getOption<bool>("d3d9.vrHandPush", true);
    g_vrSystem->m_eyeAspect = config.getOption<bool>("d3d9.vrEyeAspect", true);
    g_vrSystem->m_asyncHandOff = config.getOption<bool>("d3d9.vrAsyncHandOff", true);
    g_vrSystem->m_scopes = options.scopeLens;
    g_vrSystem->m_roomscale = options.roomscale;
    g_vrSystem->m_immersiveMinigames = config.getOption<bool>("d3d9.vrImmersiveMinigames", true);
    g_vrSystem->m_sneakConfig.physical = options.physicalSneak;
    g_vrSystem->m_sneakConfig.crouch   = crouch;
    g_vrSystem->m_forceGameSettings = !emulated && config.getOption<bool>("d3d9.vrForceGameSettings", true);
    g_vrSystem->m_rewriteTutorials = !emulated && config.getOption<bool>("d3d9.vrTutorials", true);
    g_vrSystem->m_hideGameWindow = !emulated && config.getOption<bool>("d3d9.vrHideGameWindow", true);
    g_vrSystem->m_keepGameActive = g_vrSystem->m_hideGameWindow;
    DxvkInstance::registerExtensionProvider(g_vrSystem->m_extensionProvider.get());

    VrOptionsMenu::install(options, g_vrSystem->m_texts, [] (const VrOptions& changed) {
      if (g_vrSystem)
        g_vrSystem->applyOptions(changed);
    }, [] {
      VrGameConsole::requestOpen();
    });

    VrLargeAddress::install(g_vrSystem->m_texts);
    VrSoundFocus::install();
    if (config.getOption<bool>("d3d9.vrFastMath", true))
      VrFastMath::install();
    if (config.getOption<bool>("d3d9.vrEnginePatches", true))
      VrEnginePatches::install(config.getOption<std::string>("d3d9.vrEnginePatchGroups", "all"));
    if (config.getOption<bool>("d3d9.vrGameHeap", true))
      VrGameHeap::install();

    if (g_vrSystem->m_hideGameWindow)
      VrSoundFocus::keepPluginSound();

    VrLiveCasino::install(
      config.getOption<bool>("d3d9.vrRealTimeCasino", true),
      config.getOption<bool>("d3d9.vrRealTimeVigorTester", true),
      config.getOption<bool>("d3d9.vrRealTimeCaravan", true));
  }


  void VrSystem::onInterfaceCreated() {
    if (g_launcherOverlay)
      g_launcherOverlay->start();

    if (!g_vrSystem)
      return;

    g_vrSystem->chooseResolution();

    if (g_vrSystem->m_forceGameSettings) {
      g_vrSystem->m_forceGameSettings = false;
      VrGameSettings::apply();
    }

    if (g_vrSystem->m_rewriteTutorials) {
      g_vrSystem->m_rewriteTutorials = false;
      g_vrSystem->m_tutorials = VrTutorials::install(g_vrSystem->m_texts);
    }

    if (g_vrSystem->m_keepGameActive) {
      g_vrSystem->m_keepGameActive = false;
      VrGameSettings::keepActive();
    }
  }


  void VrSystem::chooseResolution() {
    if (!m_headsetResolution || m_resolutionChosen)
      return;

    m_resolutionChosen = true;

    VrExtent eye = m_backend->recommendedEyeExtent();

    if (!eye.width || !eye.height) {
      Logger::warn("VR: The headset reported no eye resolution, the game keeps its own");
      return;
    }

    VrExtent size = vrComputeGameResolution(eye, m_eyeAspect ? 0.0f : VrGameMinAspect);

    if (!m_resolution.apply(size)) {
      Logger::info("VR: The game's display settings were not found, the game keeps its own resolution");
      return;
    }

    Logger::info(str::format("VR: The game renders at ", size.width, "x", size.height,
      " in a window, for an eye resolution of ", eye.width, "x", eye.height));
  }


  void VrSystem::applyOptions(const VrOptions& options) {
    m_turnConfig.smooth      = options.smoothTurn;
    m_turnConfig.smoothSpeed = options.smoothTurnSpeed;

    m_pipBoyScale = options.pipBoyScale;
    m_manualReload = options.manualReload;
    m_sneakConfig.physical = options.physicalSneak;
    m_scopes = options.scopeLens;
    m_roomscale = options.roomscale;

    if (m_stereoRenderer) {
      m_stereoRenderer->setScopesEnabled(m_scopes);
      m_stereoRenderer->setRoomscale(m_roomscale);
      m_stereoRenderer->setTurnConfig(m_turnConfig);
      m_stereoRenderer->setPipBoyScale(m_pipBoyScale);
      m_stereoRenderer->setManualReload(m_manualReload);
      m_stereoRenderer->setSneakConfig(m_sneakConfig);
    }

    VrOptionsFile::save(options);
  }


  VrSystem* VrSystem::get() {
    return g_vrSystem.get();
  }


  VrPresentMode VrSystem::onPresent(IDirect3DSwapChain9* swapchain) {
    VrGameHeap::logStatsIfDue();

    if (!g_vrSystem || !g_vrSystem->m_stereoRenderer)
      return VrPresentMode::Game;

    VrPresentMode mode = g_vrSystem->m_stereoRenderer->onPresent(swapchain);

    if (!g_vrSystem->m_hideGameWindow || mode == VrPresentMode::Drop || !VrGameWindow::hide(swapchain))
      return mode;

    // Without a present, nothing else paces the frames the headset does not pace
    if (mode == VrPresentMode::Game)
      VrGameWindow::waitUnpacedFrame();

    return VrPresentMode::Hidden;
  }


  bool VrSystem::attachDevice(IDirect3DDevice9* device) {
    // The renderer has read the resolution by the time the device exists
    m_resolution.restoreSettings();

    if (m_hasSession)
      return true;

    VrGraphicsBinding binding;

    if (!VrD3D9Bridge::queryGraphicsBinding(device, binding))
      return false;

    binding.asyncHandOff = m_asyncHandOff;

    logLoadedModules();

    VkPhysicalDevice preferred = m_backend->selectPhysicalDevice(binding.instance);

    if (preferred && preferred != binding.physicalDevice)
      Logger::warn("VR: The VR runtime prefers a different GPU than the one DXVK renders on");

    m_hasSession = m_backend->beginSession(binding);

    if (!m_hasSession) {
      Logger::err("VR: Failed to start the VR session");
      return false;
    }

    m_stereoRenderer = VrStereoRenderer::install(*m_backend, device, m_showPreview, m_panelConfig, m_turnConfig,
      m_weaponInHand, m_timerConfig, m_passConfig, m_developerConfig, m_swingConfig, m_texts);

    if (m_stereoRenderer) {
      m_stereoRenderer->setPipBoyScale(m_pipBoyScale);
      m_stereoRenderer->setMinigamesEnabled(m_immersiveMinigames);
      m_stereoRenderer->setScopesEnabled(m_scopes);
      m_stereoRenderer->setManualReload(m_manualReload);
      m_stereoRenderer->setRoomscale(m_roomscale);
      m_stereoRenderer->setHandPush(m_handPush);
      m_stereoRenderer->setSneakConfig(m_sneakConfig);
    }

    return true;
  }


  void VrSystem::detachDevice() {
    if (!m_hasSession)
      return;

    // The eye textures belong to the device and must go before the session
    m_stereoRenderer = nullptr;
    m_backend->endSession();
    m_hasSession = false;
  }

}
