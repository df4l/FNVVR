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
    /// Milliseconds the caller spent getting the rendering to the queue
    /// just before the call, for the backend's frame statistics
    double                                syncMs      = 0.0;
  };

  /**
   * \brief Flat panels a backend can show at the same time
   */
  enum class VrPanelId : uint32_t {
    /// Screens without a 3D scene, such as the main menu
    Menu,
    /// HUD messages, objectives and subtitles, in front of the head
    HudHead,
    /// Health bar and compass, on the back of the right hand
    HudWrist,
    /// Interaction prompt, at the fingertip nearest the target
    HudPrompt,
    /// Charge of a power attack, beside the right hand
    HudCharge,
    /// Ammunition held by the left hand while it reloads, above it
    HudAmmo,
    /// The quick wheel of hotkeys, in front of the right hand
    QuickWheel,
    /// The console's terminal and keyboard, in front of the player
    Terminal,
  };

  constexpr uint32_t VrPanelCount = 8;

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
   * and its alpha channel is used for blending, its colours premultiplied
   * by it or not.
   */
  struct VrPanelSubmission {
    VrEyeImage    image;
    VrPose        pose;
    float         width  = 0.0f;
    VrPanelAnchor anchor = VrPanelAnchor::Room;
    /// The image's colours are premultiplied by its alpha
    bool          premultiplied = false;
    /// See VrFrameSubmission::syncMs
    double        syncMs = 0.0;
  };

  /**
   * \brief Image of a panel held in system memory
   *
   * For a panel with no graphics device behind it. The panel stays in the
   * room, see VrPanelAnchor::Room.
   */
  struct VrPanelPixels {
    /// RGBA8 pixels, top row first, not premultiplied
    const uint8_t* rgba   = nullptr;
    uint32_t       width  = 0;
    uint32_t       height = 0;
    VrPose         pose;
    /// Width in the room, in metres
    float          metres = 0.0f;
  };

  /**
   * \brief Laser drawn from a controller
   *
   * The laser starts at \c origin and goes along its -Z axis for \c length
   * metres. Both are in tracking space. An active laser is drawn brighter.
   */
  struct VrPointerSubmission {
    VrPose origin;
    float  length = 0.0f;
    bool   active = false;
  };

  /**
   * \brief Text keyboard the runtime shows for a text field
   *
   * Texts are UTF-8.
   */
  struct VrKeyboardRequest {
    /// What the text is for, shown above it
    std::string description;
    /// Characters the text may hold at most
    uint32_t    maxLength = 0;
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
   *  4. per frame: waitFrame, beginFrame, pollInput, locateViews, then
   *     submitFrame or submitEmptyFrame
   *  5. endSession
   *
   * submitPanel and hidePanel may be called at any time during the session.
   * beginFrame, submitFrame, submitEmptyFrame and submitPanel are called with
   * the device queue locked, so they may use it.
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

    /**
     * \brief Starts a session without a graphics device
     *
     * For a process that only shows panels and lasers. It renders no eyes:
     * the runtime shows its own surroundings behind the panels. Only
     * waitFrame, pollInput, setInputContext, submitPanelPixels, hidePanel,
     * showPointer, hidePointer and endSession may be used.
     *
     * \returns \c true if the session is running
     */
    virtual bool beginPanelSession() = 0;

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
     * \brief Marks the start of the frame's GPU work
     *
     * Called once after each waitFrame, before any rendering of the frame
     * reaches the queue.
     */
    virtual void beginFrame() = 0;

    /**
     * \brief Samples head and controller state for a display time
     *
     * \param [in] displayTime Predicted display time from waitFrame
     */
    virtual VrInputState pollInput(int64_t displayTime) = 0;

    /**
     * \brief Chooses which actions the next pollInput reports
     *
     * Stays in effect until it is called again. The initial context is Game.
     */
    virtual void setInputContext(VrInputContext context) = 0;

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
     * Either call tells the runtime that the frame's rendering is done.
     */
    virtual bool submitFrame(const VrFrameSubmission& frame) = 0;

    virtual void submitEmptyFrame(int64_t displayTime) = 0;

    /**
     * \brief Shows a flat panel, or updates it if it is shown
     *
     * The panel is drawn over the stereo frames and stays visible with its
     * last image until hidePanel is called for it. It does not depend on the
     * frame loop: it may be submitted while no stereo frames are rendered,
     * for example while the game loads. Panels are independent of each other.
     * HudHead is drawn over Menu, and the hand panels over HudHead.
     */
    virtual bool submitPanel(VrPanelId id, const VrPanelSubmission& panel) = 0;

    virtual void hidePanel(VrPanelId id) = 0;

    /**
     * \brief Shows a panel from an image in system memory
     *
     * Slower than submitPanel: meant for images that rarely change.
     *
     * \returns \c true if the panel was updated
     */
    virtual bool submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) = 0;

    /**
     * \brief Shows the laser of a hand, or moves it if it is shown
     *
     * Like panels, the laser is drawn over the stereo frames until
     * hidePointer is called for the hand.
     */
    virtual void showPointer(VrHand hand, const VrPointerSubmission& pointer) = 0;

    virtual void hidePointer(VrHand hand) = 0;

    /**
     * \brief Triggers controller vibration
     *
     * \param [in] amplitude Strength in [0, 1]
     * \param [in] durationNs Duration in nanoseconds
     */
    virtual void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) = 0;

    /**
     * \brief Fades the whole view to black or back
     *
     * The fade goes from the opacity shown now to the given one.
     * \param [in] seconds Duration of the fade, 0 for at once
     * \param [in] opacity Opacity of the black in [0, 1], 0 shows the view
     */
    virtual void fadeView(float seconds, float opacity) = 0;

    /**
     * \brief Shows the runtime's own text keyboard
     *
     * The keyboard is typed on with the runtime's lasers, over the frames
     * and panels, until it is confirmed, closed by the user or hidden with
     * hideKeyboard. It holds no text of its own: each key is given as it
     * is pressed, for the application to show. See takeKeyboardEvent.
     *
     * \returns \c false if the runtime has no keyboard or could not show it
     */
    virtual bool showKeyboard(const VrKeyboardRequest& request) = 0;

    virtual void hideKeyboard() = 0;

    /**
     * \brief Takes what happened to the keyboard since the last call
     *
     * Events are read while waiting for a frame. A confirmation wins over a
     * close seen in the same wait.
     *
     * \param [out] typed The keys pressed since the last call, in order, as
     *    UTF-8: characters, \c '\b' for Backspace and \c '\n' for Enter
     */
    virtual VrKeyboardEvent takeKeyboardEvent(std::string& typed) = 0;

  };

}
