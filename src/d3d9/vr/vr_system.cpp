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
#include "vr_stereo_renderer.h"
#include "vr_system.h"

namespace dxvk {

  static std::unique_ptr<VrSystem> g_vrSystem;
  static bool                      g_vrInitialized = false;


  VrSystem::VrSystem(std::unique_ptr<IVRBackend> backend, bool showPreview)
  : m_backend(std::move(backend)),
    m_extensionProvider(std::make_unique<VrExtensionProvider>(*m_backend)),
    m_showPreview(showPreview) { }


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

    if (auto* emulator = dynamic_cast<VrEmulatorBackend*>(backend.get())) {
      VrEmulatorKeyboard keyboard;
      emulator->setInputSource([keyboard] { return keyboard.sample(); });
    }

    g_vrSystem.reset(new VrSystem(std::move(backend), showPreview));
    DxvkInstance::registerExtensionProvider(g_vrSystem->m_extensionProvider.get());
  }


  VrSystem* VrSystem::get() {
    return g_vrSystem.get();
  }


  bool VrSystem::attachDevice(IDirect3DDevice9* device) {
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

    m_stereoRenderer = VrStereoRenderer::install(*m_backend, device, m_showPreview);
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
