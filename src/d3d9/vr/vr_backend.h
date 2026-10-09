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
    /// Entry point of the Vulkan loader DXVK uses (winevulkan.dll under Wine)
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkInstance       instance         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice   = VK_NULL_HANDLE;
    VkDevice         device           = VK_NULL_HANDLE;
    VkQueue          queue            = VK_NULL_HANDLE;
    uint32_t         queueFamilyIndex = 0;
    uint32_t         queueIndex       = 0;
  };

  /**
   * \brief Vulkan instance extensions a backend requires
   *
   * Must be queried before the Vulkan instance is created so that they can
   * be enabled alongside DXVK's own extensions. Device extensions depend on
   * the physical device, see IVRBackend::queryDeviceExtensions.
   */
  struct VrVulkanRequirements {
    std::vector<std::string> instanceExtensions;
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
   * \brief Flat panels a backend can show at the same time
   */
  enum class VrPanelId : uint32_t {
    /// Screens without a 3D scene, such as the main menu
    Menu,
    /// HUD messages, objectives and subtitles, in front of the head
    HudHead,
  };

  constexpr uint32_t VrPanelCount = 2;

  /**
   * \brief What a panel's pose is relative to
   */
  enum class VrPanelAnchor : uint32_t {
    /// Tracking space: the panel stays fixed in the room
    Room,
    /// The head: the panel moves with it, without latency
    Head,
  };

  /**
   * \brief Flat image shown in front of the user
   *
   * The panel is a rectangle centred on \c pose and facing along its +Z
   * axis. The pose is in tracking space or relative to the head, see
   * \c anchor. The panel is \c width metres wide, and its height follows the
   * aspect ratio of the image. The image follows the rules of VrEyeImage,
   * and its alpha channel is used for blending.
   */
  struct VrPanelSubmission {
    VrEyeImage    image;
    VrPose        pose;
    float         width  = 0.0f;
    VrPanelAnchor anchor = VrPanelAnchor::Room;
  };

  /**
   * \brief Abstract VR runtime
   *
   * All game-facing VR code talks to this interface. The implementation is
   * selected once at startup, see vr_backend_factory.h.
   *
   * Expected call order:
   *  1. queryVulkanRequirements, before creating the Vulkan instance
   *  2. queryDeviceExtensions for each adapter, and selectPhysicalDevice
   *  3. beginSession, once the device exists
   *  4. per frame: waitFrame, pollInput, locateViews, submitFrame
   *  5. endSession
   *
   * submitPanel and hidePanel may be called at any time during the session.
   *
   * Methods are called from the render/game thread only.
   */
  class IVRBackend {

  public:

    virtual ~IVRBackend() { }

    /**
     * \brief Short backend name used in logs, e.g. "OpenVR"
     */
    virtual const char* name() const = 0;

    virtual VrVulkanRequirements queryVulkanRequirements() = 0;

    /**
     * \brief Vulkan device extensions the runtime needs on a GPU
     *
     * \param [in] physicalDevice Physical device of the instance created
     *    with the extensions from queryVulkanRequirements
     */
    virtual std::vector<std::string> queryDeviceExtensions(VkPhysicalDevice physicalDevice) = 0;

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
     * \brief Shows a flat panel, or updates it if it is shown
     *
     * The panel is drawn over the stereo frames and stays visible with its
     * last image until hidePanel is called for it. It does not depend on the
     * frame loop: it may be submitted while no stereo frames are rendered,
     * for example while the game loads. Panels are independent of each other,
     * and HudHead is drawn over Menu.
     */
    virtual bool submitPanel(VrPanelId id, const VrPanelSubmission& panel) = 0;

    virtual void hidePanel(VrPanelId id) = 0;

    /**
     * \brief Triggers controller vibration
     *
     * \param [in] amplitude Strength in [0, 1]
     * \param [in] durationNs Duration in nanoseconds
     */
    virtual void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) = 0;

  };

}
