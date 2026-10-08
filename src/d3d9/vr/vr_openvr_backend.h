#pragma once

#include <memory>

#include "vr_backend.h"

namespace vr {
  class IVRCompositor;
  class IVRSystem;
}

struct HINSTANCE__;

namespace dxvk {

  class VrEyeTransition;

  /**
   * \brief VR backend for a real headset through OpenVR
   *
   * OpenVR is used because the game is a 32-bit process: the 32-bit
   * \c openvr_api.dll talks to the 64-bit SteamVR over IPC, which a 32-bit
   * OpenXR loader cannot do without a 32-bit runtime. The DLL is loaded at
   * run time from the game folder (or the search path), so the build does not
   * depend on it.
   *
   * Poses and buttons come from the legacy IVRSystem and IVRCompositor
   * interfaces, which need no action manifest. This has limits compared to
   * the action-based input: there is no separate aim pose (the aim pose equals
   * the grip pose), the squeeze is on or off, and a haptic pulse has a
   * duration but no amplitude.
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

    VrSessionState sessionState() const override;

    VrExtent recommendedEyeExtent() const override;

    VrFrameTiming waitFrame() override;

    VrInputState pollInput(int64_t displayTime) override;

    std::array<VrEyeView, VrEyeCount> locateViews(int64_t displayTime) override;

    bool submitFrame(const VrFrameSubmission& frame) override;

    void submitEmptyFrame(int64_t displayTime) override;

    void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) override;

  private:

    struct Runtime;

    bool initializeRuntime();

    void shutdownRuntime();

    void pollEvents();

    uint32_t controllerIndex(VrHand hand) const;

    std::unique_ptr<Runtime>          m_runtime;
    std::unique_ptr<VrEyeTransition>  m_transition;
    VrGraphicsBinding                 m_binding;
    VrSessionState                    m_state = VrSessionState::Idle;
    VrInputState                      m_poses;
    int64_t                           m_frameCounter = 0;
    int64_t                           m_periodNs     = 0;

  };

}
