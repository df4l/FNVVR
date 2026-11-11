#pragma once

#include <array>
#include <memory>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Moves images to the layout the OpenVR compositor reads
   *
   * Used for the eye images and for the panel image.
   * The compositor takes no layout with a Vulkan texture and expects
   * \c VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, whereas DXVK leaves the images
   * in whatever layout it used last (see VrEyeImage). The images are moved
   * before the submission and put back afterwards, on the same queue, so that
   * DXVK finds them as it left them.
   *
   * The CPU never waits for the GPU: the runtime copies the image with
   * commands it submits to the same queue, so queue order alone puts the
   * copy after the game's rendering and before the image is moved back.
   * The game thread can then start on the next frame while the GPU
   * finishes this one.
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

    /// Most images moved by one call
    static constexpr uint32_t MaxImages = VrEyeCount;

    /**
     * \brief Moves images to the transfer source layout
     *
     * \param [in] images Images, at most MaxImages
     * \param [in] count Number of images
     * \returns \c false if the transition could not be submitted
     */
    bool toTransferSource(const VrEyeImage* images, uint32_t count);

    /**
     * \brief Moves images back to the layout they had
     *
     * The queue executes it after the compositor's copy.
     */
    bool restore(const VrEyeImage* images, uint32_t count);

  private:

    struct Functions;

    struct Slot {
      VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      VkFence         fence         = VK_NULL_HANDLE;
    };

    /// Slots used in turn. A frame makes at most ten transitions (eye
    /// images, head HUD, wrist HUD, interaction prompt and menu panels,
    /// each out and back), so a slot is reused four frames later, when the
    /// GPU is long done with it.
    static constexpr uint32_t SlotCount = 40;

    VrEyeTransition(const VrGraphicsBinding& binding);

    bool initialize();

    bool submit(const VrEyeImage* images, uint32_t count, bool toSource);

    VrGraphicsBinding           m_binding;
    std::unique_ptr<Functions>  m_vk;
    VkCommandPool               m_pool     = VK_NULL_HANDLE;
    std::array<Slot, SlotCount> m_slots    = { };
    uint32_t                    m_nextSlot = 0;

  };

}
