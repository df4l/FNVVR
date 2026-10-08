#include "../../dxvk/dxvk_include.h"
#include "../../dxvk/dxvk_instance.h"

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
    if (adapterId < m_deviceExtensions.size())
      return m_deviceExtensions[adapterId];

    return DxvkExtensionList();
  }


  void VrExtensionProvider::initInstanceExtensions() {
    VrVulkanRequirements requirements = m_backend.queryVulkanRequirements();

    m_instanceExtensions.clear();

    for (const auto& name : requirements.instanceExtensions)
      m_instanceExtensions.push_back(vk::makeExtension(name.c_str()));

  }


  void VrExtensionProvider::initDeviceExtensions(const DxvkInstance* instance) {
    m_deviceExtensions.clear();

    for (uint32_t i = 0; instance->enumAdapters(i) != nullptr; i++) {
      DxvkExtensionList extensions;

      for (const auto& name : m_backend.queryDeviceExtensions(instance->enumAdapters(i)->handle()))
        extensions.push_back(vk::makeExtension(name.c_str()));

      m_deviceExtensions.push_back(std::move(extensions));
    }
  }

}
