#pragma once

#include <functional>
#include <vector>

#include "vr_backend.h"
#include "vr_emulator_rig.h"

namespace dxvk {

  /**
   * \brief Receives every frame submitted to the emulator
   *
   * Implementations can display the stereo output in a window or write it
   * to a file. Tests use it to check what the game submitted.
   */
  class IVRFrameSink {

  public:

    virtual ~IVRFrameSink() { }

    virtual void onFrame(const VrFrameSubmission& frame) = 0;

  };

  /**
   * \brief Head pose keyframe for scripted motion
   */
  struct VrPoseKeyframe {
    int64_t timeNs = 0;
    VrPose  pose;
  };

  struct VrEmulatorConfig {
    VrExtent  eyeExtent        = { 1440, 1600 };
    float     ipdMeters        = 0.064f;
    float     horizontalFov    = 1.75f;
    float     verticalFov      = 1.80f;
    uint32_t  refreshRate      = 90;
    bool      realtime         = false;
    VrEmulatorRigConfig rig;
  };

  /**
   * \brief VR backend that simulates a headset and two controllers
   *
   * Needs no hardware and no Vulkan runtime support. Head and hands are
   * driven by VrEmulatorInput or by a scripted head path. Submitted
   * frames are forwarded to an optional IVRFrameSink.
   *
   * Time is virtual unless \c realtime is set: every waitFrame advances
   * the clock by exactly one refresh period, which makes runs reproducible.
   */
  class VrEmulatorBackend : public IVRBackend {

  public:

    explicit VrEmulatorBackend(const VrEmulatorConfig& config = VrEmulatorConfig());

    ~VrEmulatorBackend();

    const char* name() const override;

    VrVulkanRequirements queryVulkanRequirements() override;

    std::vector<std::string> queryDeviceExtensions(VkPhysicalDevice physicalDevice) override;

    VkPhysicalDevice selectPhysicalDevice(VkInstance instance) override;

    bool beginSession(const VrGraphicsBinding& binding) override;

    void endSession() override;

    VrSessionState sessionState() const override;

    VrExtent recommendedEyeExtent() const override;

    VrFrameTiming waitFrame() override;

    VrInputState pollInput(int64_t displayTime) override;

    std::array<VrEyeView, VrEyeCount> locateViews(int64_t displayTime) override;

    bool submitFrame(const VrFrameSubmission& frame) override;

    void submitEmptyFrame(int64_t displayTime) override;

    void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) override;

    /**
     * \brief Sets the keyboard and mouse input applied on the next frame
     */
    void setInput(const VrEmulatorInput& input);

    /**
     * rief Sets a function that is sampled once per pollInput
     *
     * Its result is applied like setInput, before the rig advances.
     */
    void setInputSource(std::function<VrEmulatorInput()> source);

    /**
     * \brief Replaces interactive head control with a scripted path
     *
     * Keyframes must be sorted by time. The pose is interpolated between
     * them and held at the ends. An empty script restores interactive control.
     */
    void setHeadScript(std::vector<VrPoseKeyframe> keyframes);

    void setFrameSink(IVRFrameSink* sink);

    uint32_t submittedFrameCount() const { return m_submittedFrames; }

    uint32_t emptyFrameCount() const { return m_emptyFrames; }

    /**
     * \brief Last haptic pulse requested for a hand, for tests
     */
    float lastHapticAmplitude(VrHand hand) const;

  private:

    VrEmulatorConfig  m_config;
    VrEmulatorRig     m_rig;
    VrSessionState    m_state = VrSessionState::Idle;

    VrEmulatorInput   m_pendingInput;
    std::function<VrEmulatorInput()> m_inputSource;
    std::vector<VrPoseKeyframe> m_script;
    IVRFrameSink*     m_sink = nullptr;

    int64_t           m_periodNs = 0;
    int64_t           m_displayTime = 0;
    int64_t           m_lastInputTime = 0;
    int64_t           m_startWallNs = 0;

    uint32_t          m_submittedFrames = 0;
    uint32_t          m_emptyFrames = 0;
    float             m_haptic[VrHandCount] = { };

    VrPose scriptedPose(int64_t timeNs) const;

    void sleepUntilDisplayTime(int64_t displayTime) const;

  };

}
