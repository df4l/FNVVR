#pragma once

#include "../../dxvk/dxvk_extension_provider.h"

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Enables the Vulkan extensions the VR backend requires
   */
  class VrExtensionProvider : public DxvkExtensionProvider {

  public:

    explicit VrExtensionProvider(IVRBackend& backend);

    std::string_view getName() override;

    DxvkExtensionList getInstanceExtensions() override;

    DxvkExtensionList getDeviceExtensions(
            uint32_t      adapterId) override;

    void initInstanceExtensions() override;

    void initDeviceExtensions(
      const DxvkInstance* instance) override;

  private:

    IVRBackend&           m_backend;
    DxvkExtensionList     m_instanceExtensions;
    DxvkExtensionList     m_deviceExtensions;

  };

}
