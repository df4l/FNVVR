#pragma once

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Vulkan objects a backend needs to submit frames
   */
  struct VrGraphicsBinding {
    VkInstance       instance         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice   = VK_NULL_HANDLE;
    VkDevice         device           = VK_NULL_HANDLE;
    uint32_t         queueFamilyIndex = 0;
    uint32_t         queueIndex       = 0;
  };

  /**
   * \brief Vulkan extensions a backend requires
   *
   * Must be queried before the Vulkan instance and device are created
   * so that they can be enabled alongside DXVK's own extensions.
   */
  struct VrVulkanRequirements {
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
  };

  /**
   * \brief Game-rendered image for one eye
   *
   * The image is owned by the renderer. The backend only reads it during
   * IVRBackend::submitFrame and must not keep references to it afterwards.
   * It is in \c layout when submitFrame is called, and the backend has to
   * leave it in that layout. The Vulkan queue is locked for the duration of
   * the call, so the backend may submit work on it.
   */
  struct VrEyeImage {
    VkImage       image     = VK_NULL_HANDLE;
    VkFormat      format    = VK_FORMAT_UNDEFINED;
    VkImageLayout layout    = VK_IMAGE_LAYOUT_UNDEFINED;
    VrExtent      extent;
    uint32_t      arrayLayer = 0;
  };

  /**
   * \brief Frame handed to the backend for display
   */
  struct VrFrameSubmission {
    std::array<VrEyeImage, VrEyeCount>    images;
    std::array<VrEyeView, VrEyeCount>     views;
    int64_t                               displayTime = 0;
  };

  /**
   * \brief Abstract VR runtime
   *
   * All game-facing VR code talks to this interface. The implementation is
   * selected once at startup, see vr_backend_factory.h.
   *
   * Expected call order:
   *  1. queryVulkanRequirements, before creating the Vulkan instance
   *  2. selectPhysicalDevice, when picking the adapter
   *  3. beginSession, once the device exists
   *  4. per frame: waitFrame, pollInput, locateViews, submitFrame
   *  5. endSession
   *
   * Methods are called from the render/game thread only.
   */
  class IVRBackend {

  public:

    virtual ~IVRBackend() { }

    /**
     * \brief Short backend name used in logs, e.g. "OpenXR"
     */
    virtual const char* name() const = 0;

    virtual VrVulkanRequirements queryVulkanRequirements() = 0;

    /**
     * \brief Selects the GPU the runtime wants to use
     *
     * \param [in] instance Vulkan instance created with the extensions
     *    returned by queryVulkanRequirements
     * \returns The physical device, or VK_NULL_HANDLE if the runtime has
     *    no preference or the query failed
     */
    virtual VkPhysicalDevice selectPhysicalDevice(VkInstance instance) = 0;

    /**
     * \brief Creates the session on the given Vulkan device
     * \returns \c true on success
     */
    virtual bool beginSession(const VrGraphicsBinding& binding) = 0;

    virtual void endSession() = 0;

    virtual VrSessionState sessionState() const = 0;

    /**
     * \brief Size of the render target for each eye, in pixels
     *
     * Valid once the session has begun.
     */
    virtual VrExtent recommendedEyeExtent() const = 0;

    /**
     * \brief Blocks until the next frame should start
     *
     * Paces the game loop to the headset's refresh rate.
     */
    virtual VrFrameTiming waitFrame() = 0;

    /**
     * \brief Samples head and controller state for a display time
     *
     * \param [in] displayTime Predicted display time from waitFrame
     */
    virtual VrInputState pollInput(int64_t displayTime) = 0;

    /**
     * \brief Computes the per-eye pose and field of view
     *
     * \param [in] displayTime Predicted display time from waitFrame
     * \returns Views for the left and right eye, in tracking space
     */
    virtual std::array<VrEyeView, VrEyeCount> locateViews(int64_t displayTime) = 0;

    /**
     * \brief Presents a rendered stereo frame
     *
     * Only valid for frames where waitFrame returned shouldRender.
     * Frames that are not rendered must still be closed with
     * submitEmptyFrame so the runtime can keep its frame loop in sync.
     */
    virtual bool submitFrame(const VrFrameSubmission& frame) = 0;

    virtual void submitEmptyFrame(int64_t displayTime) = 0;

    /**
     * \brief Triggers controller vibration
     *
     * \param [in] amplitude Strength in [0, 1]
     * \param [in] durationNs Duration in nanoseconds
     */
    virtual void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) = 0;

  };

}
