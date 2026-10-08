#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "vr_emulator_backend.h"

namespace dxvk {

  namespace {

    int64_t wallClockNs() {
      using namespace std::chrono;
      return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

  }


  VrEmulatorBackend::VrEmulatorBackend(const VrEmulatorConfig& config)
  : m_config(config), m_rig(config.rig) {
    m_periodNs = 1000000000ll / int64_t(std::max(config.refreshRate, 1u));
  }


  VrEmulatorBackend::~VrEmulatorBackend() { }


  const char* VrEmulatorBackend::name() const {
    return "Emulator";
  }


  VrVulkanRequirements VrEmulatorBackend::queryVulkanRequirements() {
    return VrVulkanRequirements();
  }


  VkPhysicalDevice VrEmulatorBackend::selectPhysicalDevice(VkInstance instance) {
    return VK_NULL_HANDLE;
  }


  bool VrEmulatorBackend::beginSession(const VrGraphicsBinding& binding) {
    m_state = VrSessionState::Running;
    m_displayTime = 0;
    m_lastInputTime = 0;
    m_startWallNs = wallClockNs();
    return true;
  }


  void VrEmulatorBackend::endSession() {
    m_state = VrSessionState::Idle;
  }


  VrSessionState VrEmulatorBackend::sessionState() const {
    return m_state;
  }


  VrExtent VrEmulatorBackend::recommendedEyeExtent() const {
    return m_config.eyeExtent;
  }


  VrFrameTiming VrEmulatorBackend::waitFrame() {
    VrFrameTiming timing;

    if (m_state != VrSessionState::Running)
      return timing;

    m_displayTime += m_periodNs;

    if (m_config.realtime)
      sleepUntilDisplayTime(m_displayTime);

    timing.shouldRender         = true;
    timing.predictedDisplayTime = m_displayTime;
    timing.predictedPeriod      = m_periodNs;
    return timing;
  }


  VrInputState VrEmulatorBackend::pollInput(int64_t displayTime) {
    const float dt = float(displayTime - m_lastInputTime) * 1e-9f;
    m_lastInputTime = displayTime;

    if (m_inputSource)
      setInput(m_inputSource());

    m_rig.update(m_pendingInput, dt);

    // Mouse deltas are consumed once, movement keys stay held
    m_pendingInput.mouseDeltaX = 0.0f;
    m_pendingInput.mouseDeltaY = 0.0f;
    m_pendingInput.recenter    = false;

    if (!m_script.empty())
      m_rig.setHeadPose(scriptedPose(displayTime));

    VrInputState state;
    state.isHeadTracked = true;
    state.headPose      = m_rig.headPose();

    for (uint32_t i = 0; i < VrHandCount; i++)
      state.controllers[i] = m_rig.controllerState(VrHand(i));

    return state;
  }


  std::array<VrEyeView, VrEyeCount> VrEmulatorBackend::locateViews(int64_t displayTime) {
    const VrPose head = m_rig.headPose();

    VrFov fov;
    fov.angleLeft  = -m_config.horizontalFov * 0.5f;
    fov.angleRight =  m_config.horizontalFov * 0.5f;
    fov.angleDown  = -m_config.verticalFov * 0.5f;
    fov.angleUp    =  m_config.verticalFov * 0.5f;

    std::array<VrEyeView, VrEyeCount> views;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      const float side = i == uint32_t(VrEye::Left) ? -0.5f : 0.5f;

      VrPose eyeInHead;
      eyeInHead.position.x = side * m_config.ipdMeters;

      views[i].pose = vrCompose(head, eyeInHead);
      views[i].fov  = fov;
    }

    return views;
  }


  bool VrEmulatorBackend::submitFrame(const VrFrameSubmission& frame) {
    if (m_state != VrSessionState::Running)
      return false;

    m_submittedFrames++;

    if (m_sink)
      m_sink->onFrame(frame);

    return true;
  }


  void VrEmulatorBackend::submitEmptyFrame(int64_t displayTime) {
    m_emptyFrames++;
  }


  void VrEmulatorBackend::applyHaptic(VrHand hand, float amplitude, int64_t durationNs) {
    m_haptic[uint32_t(hand)] = amplitude;
  }


  void VrEmulatorBackend::setInput(const VrEmulatorInput& input) {
    // Accumulate mouse movement between frames, keep the latest key state
    const float dx = m_pendingInput.mouseDeltaX + input.mouseDeltaX;
    const float dy = m_pendingInput.mouseDeltaY + input.mouseDeltaY;
    const bool recenter = m_pendingInput.recenter || input.recenter;

    m_pendingInput = input;
    m_pendingInput.mouseDeltaX = dx;
    m_pendingInput.mouseDeltaY = dy;
    m_pendingInput.recenter    = recenter;
  }


  void VrEmulatorBackend::setInputSource(std::function<VrEmulatorInput()> source) {
    m_inputSource = std::move(source);
  }


  void VrEmulatorBackend::setHeadScript(std::vector<VrPoseKeyframe> keyframes) {
    m_script = std::move(keyframes);
  }


  void VrEmulatorBackend::setFrameSink(IVRFrameSink* sink) {
    m_sink = sink;
  }


  float VrEmulatorBackend::lastHapticAmplitude(VrHand hand) const {
    return m_haptic[uint32_t(hand)];
  }


  VrPose VrEmulatorBackend::scriptedPose(int64_t timeNs) const {
    if (timeNs <= m_script.front().timeNs)
      return m_script.front().pose;

    if (timeNs >= m_script.back().timeNs)
      return m_script.back().pose;

    for (size_t i = 1; i < m_script.size(); i++) {
      const VrPoseKeyframe& next = m_script[i];

      if (timeNs <= next.timeNs) {
        const VrPoseKeyframe& prev = m_script[i - 1];
        const float t = float(timeNs - prev.timeNs) / float(next.timeNs - prev.timeNs);
        return vrLerp(prev.pose, next.pose, t);
      }
    }

    return m_script.back().pose;
  }


  void VrEmulatorBackend::sleepUntilDisplayTime(int64_t displayTime) const {
    const int64_t target = m_startWallNs + displayTime;
    const int64_t now    = wallClockNs();

    if (target > now)
      std::this_thread::sleep_for(std::chrono::nanoseconds(target - now));
  }

}
