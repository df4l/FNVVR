#pragma once

#include <array>
#include <memory>
#include <vector>

#include "../../util/util_time.h"

#include "vr_backend.h"
#include "vr_frame_stats.h"

namespace vr {
  class IVRCompositor;
  class IVRInput;
  class IVRSystem;
  struct VRVulkanTextureData_t;
}

struct HINSTANCE__;

namespace dxvk {

  class VrEyeTransition;

  /**
   * \brief CPU time used so far, in 100 ns units
   */
  struct VrCpuTimes {
    uint64_t threadKernel = 0;
    uint64_t threadUser   = 0;
    uint64_t process      = 0;
  };

  /**
   * \brief VR backend for a real headset through OpenVR
   *
   * OpenVR is used because the game is a 32-bit process: the 32-bit
   * \c openvr_api.dll talks to the 64-bit SteamVR over IPC, which a 32-bit
   * OpenXR loader cannot do without a 32-bit runtime. The DLL is loaded at
   * run time from the game folder (or the search path), so the build does not
   * depend on it.
   *
   * Controller poses come from IVRCompositor::WaitGetPoses (the aim pose
   * equals the grip pose for now). Buttons and sticks are read as actions
   * through IVRInput: the action manifest \c actions.json and the default
   * bindings it names are loaded from the \c fnvvr folder next to
   * d3d9.dll, see \ref VrOpenVrBackend::manifestPath. The user can change
   * the bindings in SteamVR's controller settings. Without the manifest the
   * headset still works, but the controllers do nothing.
   *
   * Each panel is an IVROverlay, placed in the standing tracking space or
   * relative to the headset.
   *
   * Frames use the compositor's explicit timing mode, with the hand-off done
   * by the backend: the game runs its logic and renders on one thread, so in
   * the implicit mode the compositor would only start on a frame at the next
   * WaitGetPoses, after the game logic of the following frame. Each frame
   * is handed off as soon as it is submitted instead, and WaitGetPoses
   * no longer uses the queue, which DXVK's submission thread shares.
   *
   * Everything here needs a headset to be verified; the conversions are
   * tested in vr_openvr_convert.h.
   */
  class VrOpenVrBackend : public IVRBackend {

  public:

    VrOpenVrBackend();
    ~VrOpenVrBackend();

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

    bool submitFrame(const VrFrameSubmission& frame) override;

    void submitEmptyFrame(int64_t displayTime) override;

    bool submitPanel(VrPanelId id, const VrPanelSubmission& panel) override;

    void hidePanel(VrPanelId id) override;

    bool submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) override;

    void showPointer(VrHand hand, const VrPointerSubmission& pointer) override;

    void hidePointer(VrHand hand) override;

    void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) override;

  private:

    struct Runtime;

    bool initializeRuntime();

    void startSession();

    void shutdownRuntime();

    void pollEvents();

    bool initializeInput();

    /**
     * \brief Logs the headset model and each eye's projection and offset
     *
     * Lets logs from different headsets be compared when the image
     * looks distorted on one of them.
     */
    void logEyeGeometry();

    /**
     * \brief Measures the frame cycle that ends now
     *
     * Called when WaitGetPoses returns. The cycle is only counted if the
     * previous frame was rendered: frames of the menus and loading screens,
     * drawn on a panel, restart the window.
     */
    void measureFrame(high_resolution_clock::time_point waitStart, high_resolution_clock::time_point posesReady);

    /**
     * \brief Starts a window of frame statistics
     *
     * Takes the compositor's counts the window's are computed from.
     */
    void startStatsWindow();

    /**
     * \brief Logs the frame timing of a full window, then starts the next one
     *
     * Logs where the time of a frame went, how many frames were late, and
     * what the compositor did with them. Reprojection warps the image, which
     * can look like a distortion of the view, and late frames look like
     * stutter.
     */
    void logFrameStats();

    void readActions(VrInputState& input);

    /**
     * \brief Path of the action manifest
     *
     * \c fnvvr\\actions.json in the folder that holds d3d9.dll, as a
     * Windows path. Under Proton the runtime client converts it.
     */
    static std::string manifestPath();

    uint32_t controllerIndex(VrHand hand) const;

    void fillTextureData(const VrEyeImage& image, vr::VRVulkanTextureData_t& data) const;

    struct Panel {
      uint64_t handle  = 0;
      bool     visible = false;
      bool     failed  = false;
      bool     logged  = false;
      bool     premultiplied = false;
    };

    bool createPanel(VrPanelId id);

    void destroyPanels();

    struct Pointer {
      uint64_t handle  = 0;
      bool     visible = false;
      bool     failed  = false;
    };

    bool createPointer(VrHand hand);

    void destroyPointers();

    /**
     * \brief Tells the compositor that the frame's rendering is done
     */
    void handOffFrame();

    /**
     * \brief Time from now until the next frame reaches the eyes, in seconds
     */
    float secondsToPhotons() const;

    std::unique_ptr<Runtime>          m_runtime;
    std::unique_ptr<VrEyeTransition>  m_transition;
    VrGraphicsBinding                 m_binding;
    VrSessionState                    m_state = VrSessionState::Idle;
    VrInputState                      m_poses;
    int64_t                           m_frameCounter = 0;
    int64_t                           m_periodNs     = 0;

    /// WaitGetPoses started a frame that is not handed off yet
    bool                              m_frameStarted = false;

    /// Seconds' worth of frames at the display rate in a window of frame statistics
    constexpr static uint32_t         StatsPeriod    = 10;

    /// Times of the frame being measured, see VrFrameStep
    struct FrameClock {
      high_resolution_clock::time_point posesReady;
      high_resolution_clock::time_point submitStart;
      high_resolution_clock::time_point imagesReady;
      high_resolution_clock::time_point eyesSubmitted;
      high_resolution_clock::time_point submitted;
      double                            syncMs      = 0.0;
      double                            panelMs     = 0.0;
      bool                              isSubmitted = false;
    };

    FrameClock                        m_frameClock;
    VrFrameStats                      m_stats        = VrFrameStats(1, 0.0);
    bool                              m_statsRunning = false;

    // The compositor's cumulative counts when the window started. They
    // belong to one process and restart when another one connects.
    uint32_t                          m_statsPid     = 0;
    uint32_t                          m_presented    = 0;
    uint32_t                          m_reprojected  = 0;
    uint32_t                          m_dropped      = 0;

    // CPU time used when the window started
    VrCpuTimes                        m_statsCpu     = { };

    // Action handles, see vr_openvr_convert.h for the paths
    struct Actions {
      uint64_t sets[2]                 = { };
      uint64_t digital[VrActionCount]  = { };
      uint64_t move[2]                 = { };
      uint64_t turn[2]                 = { };
      uint64_t haptic[VrHandCount]     = { };
      uint64_t hands                   = 0;
      uint64_t aim[VrHandCount]        = { };
      uint64_t skeleton[VrHandCount]   = { };
    };

    Actions                           m_actions;
    bool                              m_inputReady = false;
    VrInputContext                    m_context    = VrInputContext::Game;
    bool                              m_loggedInputError = false;
    bool                              m_loggedFingers[VrHandCount] = { };

    // One overlay per panel, created when the panel is first shown
    std::array<Panel, VrPanelCount>   m_panels;

    // One overlay per hand for its laser, created when it is first shown
    std::array<Pointer, VrHandCount>  m_pointers;
    std::vector<uint8_t>              m_pointerImage;

    // Each step of the first frame is logged once, so that a failure
    // inside the runtime can be located from the log
    bool                              m_loggedWait   = false;
    bool                              m_loggedSubmit = false;
    bool                              m_loggedTiming = false;

  };

}
