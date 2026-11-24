#pragma once

#include <memory>

#include "../../vulkan/vulkan_loader.h"

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Vulkan device of its own, for an OpenXR session without the game's
   *
   * OpenXR shows nothing without a graphics binding, so the launcher's panel
   * session (IVRBackend::beginPanelSession) creates a minimal instance and
   * device with the extensions the runtime asks for, on the GPU it names.
   * The device only copies images into swapchains: no feature is enabled.
   */
  class VrXrVulkanDevice {

  public:

    /**
     * \brief Creates the device for a backend's runtime
     *
     * \param [in] backend Backend whose runtime is initialised, see
     *    IVRBackend::queryVulkanRequirements
     * \returns The device, or \c nullptr on failure
     */
    static std::unique_ptr<VrXrVulkanDevice> create(IVRBackend& backend);

    ~VrXrVulkanDevice();

    VrXrVulkanDevice(const VrXrVulkanDevice&) = delete;
    VrXrVulkanDevice& operator = (const VrXrVulkanDevice&) = delete;

    /**
     * \brief Objects to begin the session with
     *
     * The queue is only used by the thread of the session, so the binding
     * has no lock.
     */
    const VrGraphicsBinding& binding() const {
      return m_binding;
    }

  private:

    VrXrVulkanDevice() = default;

    bool initialize(IVRBackend& backend);

    bool createInstance(IVRBackend& backend);

    bool createDevice(IVRBackend& backend);

    Rc<vk::LibraryLoader> m_library;
    VrGraphicsBinding     m_binding;

    PFN_vkDestroyInstance m_destroyInstance = nullptr;
    PFN_vkDestroyDevice   m_destroyDevice   = nullptr;
    PFN_vkDeviceWaitIdle  m_deviceWaitIdle  = nullptr;

  };

}
