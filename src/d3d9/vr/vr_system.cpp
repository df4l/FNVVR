#include <memory>

#include "../../dxvk/dxvk_instance.h"

#include "../../util/config/config.h"
#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_backend_factory.h"
#include "vr_d3d9_bridge.h"
#include "vr_emulator_backend.h"
#include "vr_emulator_keyboard.h"
#include "vr_extension_provider.h"
#include "vr_game_space.h"
#include "vr_gamepad_head.h"
#include "vr_gamepad_hook.h"
#include "vr_stereo_renderer.h"
#include "vr_system.h"

namespace dxvk {

  static std::unique_ptr<VrSystem> g_vrSystem;
  static bool                      g_vrInitialized = false;


  VrSystem::VrSystem(std::unique_ptr<IVRBackend> backend, bool showPreview,
    const VrPanelConfig& panel, const VrTurnConfig& turn, bool headsetResolution)
  : m_backend(std::move(backend)),
    m_extensionProvider(std::make_unique<VrExtensionProvider>(*m_backend)),
    m_showPreview(showPreview),
    m_panelConfig(panel),
    m_turnConfig(turn),
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

    std::unique_ptr<IVRBackend> backend = vrCreateBackend(name);

    if (!backend) {
      Logger::err(str::format("VR: Backend '", name, "' is unknown or not available, VR is disabled"));
      return;
    }

    Logger::info(str::format("VR: Using backend '", backend->name(), "'"));

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

      if (gamepad.hidePad || gamepad.controllers)
        VrGamepadHook::install(gamepad);
    }

    bool headsetResolution = config.getOption<bool>("d3d9.vrHeadsetResolution", true);

    g_vrSystem.reset(new VrSystem(std::move(backend), showPreview, panel, turn, headsetResolution));
    DxvkInstance::registerExtensionProvider(g_vrSystem->m_extensionProvider.get());
  }


  void VrSystem::onInterfaceCreated() {
    if (g_vrSystem)
      g_vrSystem->chooseResolution();
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

    VrExtent size = vrComputeGameResolution(eye, VrGameMinAspect);

    if (!m_resolution.apply(size)) {
      Logger::info("VR: The game's display settings were not found, the game keeps its own resolution");
      return;
    }

    Logger::info(str::format("VR: The game renders at ", size.width, "x", size.height,
      " in a window, for an eye resolution of ", eye.width, "x", eye.height));
  }


  VrSystem* VrSystem::get() {
    return g_vrSystem.get();
  }


  bool VrSystem::onPresent(IDirect3DSwapChain9* swapchain) {
    return g_vrSystem && g_vrSystem->m_stereoRenderer && g_vrSystem->m_stereoRenderer->onPresent(swapchain);
  }


  bool VrSystem::attachDevice(IDirect3DDevice9* device) {
    // The renderer has read the resolution by the time the device exists
    m_resolution.restoreSettings();

    if (m_hasSession)
      return true;

    VrGraphicsBinding binding;

    if (!VrD3D9Bridge::queryGraphicsBinding(device, binding))
      return false;

    VkPhysicalDevice preferred = m_backend->selectPhysicalDevice(binding.instance);

    if (preferred && preferred != binding.physicalDevice)
      Logger::warn("VR: The VR runtime prefers a different GPU than the one DXVK renders on");

    m_hasSession = m_backend->beginSession(binding);

    if (!m_hasSession) {
      Logger::err("VR: Failed to start the VR session");
      return false;
    }

    m_stereoRenderer = VrStereoRenderer::install(*m_backend, device, m_showPreview, m_panelConfig, m_turnConfig);
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
