#pragma once

#include <array>
#include <memory>
#include <vector>

#include <openxr/openxr.h>

#include "../../util/thread.h"

#include "vr_backend.h"

namespace dxvk {

  class VrOpenXrInput;
  class VrOpenXrLoader;
  class VrXrImageCopy;

  /**
   * \brief VR backend for a real headset through OpenXR
   *
   * Under Proton, the 32-bit \c openxr_loader.dll from the \c fnvvr folder
   * reaches the native runtime (WiVRn, Monado, SteamVR) through Proton's
   * \c wineopenxr; on Windows it reaches the runtime directly. See
   * findings/openxr-backend.md.
   *
   * The graphics binding is XR_KHR_vulkan_enable: the runtime names the
   * Vulkan extensions and DXVK creates its own instance and device with them.
   * The eye and panel images stay DXVK's: each one is copied into an image of
   * a swapchain of the backend (VrXrImageCopy), and the frame is ended with
   * a projection layer for the eyes and a quad layer per panel shown.
   *
   * Panels have no life of their own in OpenXR: the last image of each one
   * is kept in its swapchain and given again with every frame, rendered or
   * not, until it is hidden.
   *
   * Poses are given in the stage space (floor at y = 0), or the local floor
   * space without a stage. Head-anchored panels use the view space.
   *
   * Controllers are read through OpenXR actions, see VrOpenXrInput.
   *
   * Not done yet (findings/openxr-backend.md §5): lasers, fades, the
   * runtime's keyboard, eye tracking and the launcher's panel session.
   */
  class VrOpenXrBackend : public IVRBackend {

  public:

    VrOpenXrBackend();

    ~VrOpenXrBackend();

    const char* name() const override;

    VrVulkanRequirements queryVulkanRequirements() override;

    std::vector<std::string> queryDeviceExtensions(VkPhysicalDevice physicalDevice) override;

    VkPhysicalDevice selectPhysicalDevice(VkInstance instance) override;

    bool beginSession(const VrGraphicsBinding& binding) override;

    void endSession() override;

    bool beginPanelSession() override;

    VrSessionState sessionState() const override;

    VrExtent recommendedEyeExtent() const override;

    VrFrameTiming waitFrame() override;

    void beginFrame() override;

    VrInputState pollInput(int64_t displayTime) override;

    void setInputContext(VrInputContext context) override;

    std::array<VrEyeView, VrEyeCount> locateViews(int64_t displayTime) override;

    bool supportsEyeTracking() override;

    bool locateGaze(std::array<VrGaze, VrEyeCount>& gaze) override;

    bool submitFrame(const VrFrameSubmission& frame) override;

    void submitEmptyFrame(int64_t displayTime) override;

    bool submitPanel(VrPanelId id, const VrPanelSubmission& panel) override;

    bool panelsNeedFrames() const override;

    void hidePanel(VrPanelId id) override;

    bool submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) override;

    void showPointer(VrHand hand, const VrPointerSubmission& pointer) override;

    void hidePointer(VrHand hand) override;

    void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) override;

    void fadeView(float seconds, float opacity) override;

    bool showKeyboard(const VrKeyboardRequest& request) override;

    void hideKeyboard() override;

    VrKeyboardEvent takeKeyboardEvent(std::string& typed) override;

  private:

    struct Functions;

    /**
     * \brief Swapchain the backend copies one image into each frame
     */
    struct Swapchain {
      XrSwapchain          handle   = XR_NULL_HANDLE;
      std::vector<VkImage> images;
      VrExtent             extent;
      VkFormat             format   = VK_FORMAT_UNDEFINED;
      /// An image was released, so the swapchain can be shown
      bool                 hasImage = false;
      /// Index of the acquired image, or -1
      int32_t              acquired = -1;
    };

    struct Panel {
      Swapchain     swapchain;
      VrPose        pose;
      float         width         = 0.0f;
      VrPanelAnchor anchor        = VrPanelAnchor::Room;
      bool          premultiplied = false;
      bool          visible       = false;
      bool          logged        = false;
    };

    /**
     * \brief Creates the instance and finds the headset
     *
     * Runs once; later calls return the first result.
     */
    bool initializeRuntime();

    bool createInstance();

    bool findSystem();

    void logViewConfiguration();

    bool createSpaces();

    void destroySession();

    /**
     * \brief Handles the runtime's events, starting and ending the session
     */
    void pollEvents();

    void onSessionStateChanged(XrSessionState state);

    /**
     * \brief Closes a frame the caller waited for but did not end
     */
    void closeOpenFrame();

    /**
     * \brief Ends the frame begun by beginFrame
     *
     * \param [in] eyes Projection views of the eyes, or \c nullptr to show
     *    only the panels
     */
    bool endFrame(const XrCompositionLayerProjectionView* eyes);

    /**
     * \brief Makes sure a swapchain holds images of a size and format
     *
     * Recreates it when either changed.
     */
    bool prepareSwapchain(Swapchain& swapchain, VrExtent extent, VkFormat sourceFormat, const char* what);

    void destroySwapchain(Swapchain& swapchain);

    /**
     * \brief Acquires the next image of a swapchain and waits until it is free
     * \returns The image, or VK_NULL_HANDLE on failure
     */
    VkImage acquireImage(Swapchain& swapchain);

    bool releaseImage(Swapchain& swapchain);

    std::string resultString(XrResult result) const;

    bool check(XrResult result, const char* what) const;

    std::unique_ptr<VrOpenXrLoader>   m_loader;
    std::unique_ptr<Functions>        m_xr;
    std::unique_ptr<VrXrImageCopy>    m_copy;
    std::unique_ptr<VrOpenXrInput>    m_input;
    VrInputContext                    m_context        = VrInputContext::Game;
    VrGraphicsBinding                 m_binding;

    bool                              m_initialized    = false;
    bool                              m_runtimeReady   = false;
    bool                              m_localFloor     = false;

    XrInstance                        m_instance       = XR_NULL_HANDLE;
    XrSystemId                        m_system         = XR_NULL_SYSTEM_ID;
    XrSession                         m_session        = XR_NULL_HANDLE;
    XrSpace                           m_space          = XR_NULL_HANDLE;
    XrSpace                           m_viewSpace      = XR_NULL_HANDLE;
    std::vector<int64_t>              m_swapchainFormats;
    VrExtent                          m_recommended;

    XrSessionState                    m_xrState        = XR_SESSION_STATE_UNKNOWN;
    /// xrBeginSession was called, and xrEndSession not yet
    bool                              m_running        = false;
    bool                              m_lost           = false;

    XrFrameState                      m_frameState     = { XR_TYPE_FRAME_STATE };
    /// xrWaitFrame returned a frame that xrBeginFrame did not start yet
    bool                              m_frameWaited    = false;
    /// xrBeginFrame started a frame that xrEndFrame did not end yet
    bool                              m_frameBegun     = false;

    std::array<VrEyeView, VrEyeCount> m_views;
    std::array<Swapchain, VrEyeCount> m_eyes;

    /// Panels are submitted and hidden from the renderer's submission
    /// thread too, see IVRBackend
    dxvk::mutex                       m_panelMutex;
    std::array<Panel, VrPanelCount>   m_panels;

    // Steps of the first frame are logged once, so that a failure inside
    // the runtime can be located from the log
    bool                              m_loggedWait     = false;
    bool                              m_loggedSubmit   = false;
    bool                              m_loggedHead     = false;
    bool                              m_loggedEndFrame = false;
    bool                              m_loggedFormat   = false;

  };

}
