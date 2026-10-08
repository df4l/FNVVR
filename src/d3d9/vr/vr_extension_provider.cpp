#include "../../dxvk/dxvk_include.h"

#include "vr_extension_provider.h"

namespace dxvk {

  VrExtensionProvider::VrExtensionProvider(IVRBackend& backend)
  : m_backend(backend) { }


  std::string_view VrExtensionProvider::getName() {
    return m_backend.name();
  }


  DxvkExtensionList VrExtensionProvider::getInstanceExtensions() {
    return m_instanceExtensions;
  }


  DxvkExtensionList VrExtensionProvider::getDeviceExtensions(uint32_t adapterId) {
    return m_deviceExtensions;
  }


  void VrExtensionProvider::initInstanceExtensions() {
    // The requirements are queried once, device extensions are
    // returned together with them since the backend does not depend
    // on the Vulkan instance for this information.
    VrVulkanRequirements requirements = m_backend.queryVulkanRequirements();

    m_instanceExtensions.clear();
    m_deviceExtensions.clear();

    for (const auto& name : requirements.instanceExtensions)
      m_instanceExtensions.push_back(vk::makeExtension(name.c_str()));

    for (const auto& name : requirements.deviceExtensions)
      m_deviceExtensions.push_back(vk::makeExtension(name.c_str()));
  }


  void VrExtensionProvider::initDeviceExtensions(const DxvkInstance* instance) { }

}
