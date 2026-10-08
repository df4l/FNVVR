#pragma once

#include <array>
#include <memory>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Moves the eye images to the layout the OpenVR compositor reads
   *
   * The compositor takes no layout with a Vulkan texture and expects
   * \c VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, whereas DXVK leaves the images
   * in whatever layout it used last (see VrEyeImage). The images are moved
   * before the submission and put back afterwards, on the same queue, so that
   * DXVK finds them as it left them.
   *
   * Uses the Vulkan library already loaded by DXVK. All calls must be made
   * with the queue locked, which is the case inside IVRBackend::submitFrame.
   */
  class VrEyeTransition {

  public:

    /**
     * \brief Creates the helper for a device
     * \returns \c nullptr if the Vulkan functions or objects are unavailable
     */
    static std::unique_ptr<VrEyeTransition> create(const VrGraphicsBinding& binding);

    ~VrEyeTransition();

    VrEyeTransition(const VrEyeTransition&) = delete;
    VrEyeTransition& operator = (const VrEyeTransition&) = delete;

    /**
     * \brief Moves both images to the transfer source layout
     *
     * Waits until the transition is done on the GPU.
     * \returns \c false if the transition could not be submitted
     */
    bool toTransferSource(const std::array<VrEyeImage, VrEyeCount>& images);

    /**
     * \brief Moves both images back to the layout they had
     *
     * Does not wait: the queue executes it after the compositor's work.
     */
    bool restore(const std::array<VrEyeImage, VrEyeCount>& images);

  private:

    struct Functions;

    struct Slot {
      VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      VkFence         fence         = VK_NULL_HANDLE;
    };

    VrEyeTransition(const VrGraphicsBinding& binding);

    bool initialize();

    bool submit(const std::array<VrEyeImage, VrEyeCount>& images, Slot& slot, bool toSource);

    VrGraphicsBinding          m_binding;
    std::unique_ptr<Functions> m_vk;
    VkCommandPool              m_pool = VK_NULL_HANDLE;
    Slot                       m_toSource;
    Slot                       m_restore;

  };

}
