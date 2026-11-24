#pragma once

#include <array>
#include <memory>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief One image copied into an OpenXR swapchain image
   *
   * Source and target have the same size and the same bytes per pixel.
   */
  struct VrXrImageCopyRegion {
    VrEyeImage source;
    VkImage    target = VK_NULL_HANDLE;
  };

  /**
   * \brief Copies the game's images into OpenXR swapchain images
   *
   * OpenXR shows only images of its own swapchains, so each eye or panel
   * image is copied into the swapchain image acquired for it. The source
   * is moved to the transfer source layout and back to the layout DXVK left
   * it in, the target is taken from and returned in the colour attachment
   * layout, as XR_KHR_vulkan_enable requires for a released image.
   *
   * The CPU never waits for the GPU: the copy goes to the game's queue
   * after its rendering, and the runtime reads the swapchain image after
   * the copy. All calls must be made with the queue locked.
   *
   * It also fills the images the backend draws itself (lasers, the fade)
   * with a colour, and uploads the launcher's pixels.
   */
  class VrXrImageCopy {

  public:

    /**
     * \brief Creates the helper for a device
     * \returns \c nullptr if the Vulkan functions or objects are unavailable
     */
    static std::unique_ptr<VrXrImageCopy> create(const VrGraphicsBinding& binding);

    ~VrXrImageCopy();

    VrXrImageCopy(const VrXrImageCopy&) = delete;
    VrXrImageCopy& operator = (const VrXrImageCopy&) = delete;

    /// Most images copied by one call
    static constexpr uint32_t MaxRegions = VrEyeCount;

    /**
     * \brief Submits the copies
     *
     * \param [in] regions Images to copy, at most MaxRegions
     * \param [in] count Number of images
     * \returns \c false if the copy could not be submitted
     */
    bool copy(const VrXrImageCopyRegion* regions, uint32_t count);

    /**
     * \brief Fills a whole swapchain image with one colour
     *
     * \param [in] target Swapchain image
     * \param [in] color Colour in the image's format: linear values for an
     *    sRGB format, which the GPU encodes
     */
    bool fill(VkImage target, const VkClearColorValue& color);

    /**
     * \brief Copies pixels from system memory into a swapchain image
     *
     * Goes through a staging buffer of the helper, reused once the GPU is
     * done with the previous upload.
     *
     * \param [in] target Swapchain image of \c extent pixels
     * \param [in] pixels Four bytes per pixel, top row first, in the image's
     *    channel order
     */
    bool upload(VkImage target, VrExtent extent, const uint8_t* pixels);

  private:

    struct Functions;

    struct Slot {
      VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      VkFence         fence         = VK_NULL_HANDLE;
    };

    /// Slots used in turn. A frame makes one copy for the eyes and one per
    /// panel shown, so a slot is reused frames later, when the GPU is done.
    static constexpr uint32_t SlotCount = 32;

    explicit VrXrImageCopy(const VrGraphicsBinding& binding);

    bool initialize();

    /**
     * \brief Starts recording into the next slot, once the GPU is done with it
     * \returns The slot, or \c nullptr on failure
     */
    Slot* beginSlot();

    bool submitSlot(Slot& slot, const char* what);

    /**
     * \brief Records the barriers around a write of a whole target image
     *
     * \param [in] before Barrier into the transfer layout if \c true, out of
     *    it into the layout a released swapchain image must be in otherwise
     */
    void targetBarrier(VkCommandBuffer commandBuffer, VkImage target, bool before);

    bool prepareStaging(VkDeviceSize size);

    void destroyStaging();

    VrGraphicsBinding           m_binding;
    std::unique_ptr<Functions>  m_vk;
    VkCommandPool               m_pool     = VK_NULL_HANDLE;
    std::array<Slot, SlotCount> m_slots    = { };
    uint32_t                    m_nextSlot = 0;

    VkBuffer                    m_staging       = VK_NULL_HANDLE;
    VkDeviceMemory              m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize                m_stagingSize   = 0;
    void*                       m_stagingData   = nullptr;
    /// Fence of the slot of the last upload, which read the staging buffer
    VkFence                     m_stagingFence  = VK_NULL_HANDLE;

  };

}
