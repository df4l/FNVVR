#include <windows.h>

#include <algorithm>
#include <array>
#include <sstream>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

#include <openvr/openvr.hpp>

#include "vr_eye_transition.h"
#include "vr_math.h"
#include "vr_openvr_backend.h"
#include "vr_openvr_convert.h"

namespace dxvk {

  namespace {

    // A copy next to the game takes precedence. Proton installs its own
    // 32-bit build as openvr_api_dxvk.dll in syswow64, for its DXVK.
    constexpr std::array<const char*, 2> RuntimeLibraryNames = {
      "openvr_api.dll",
      "openvr_api_dxvk.dll",
    };

    constexpr int64_t NanosecondsPerSecond = 1000000000;

    // Used when the headset does not report its refresh rate
    constexpr float FallbackRefreshRate = 90.0f;

    // Identifies the panel overlay within SteamVR, and names it in its UI
    // Overlay key and name of each panel, indexed by VrPanelId. The sort
    // order follows the index, so the HUD is drawn over the menu panel.
    constexpr std::array<const char*, VrPanelCount> PanelOverlayKeys  = { "fnvvr.panel", "fnvvr.hud.head" };
    constexpr std::array<const char*, VrPanelCount> PanelOverlayNames = { "Fallout: New Vegas", "Fallout: New Vegas HUD" };

    VrPose poseFromOpenVr(const vr::HmdMatrix34_t& matrix) {
      return vrPoseFromMatrix34(matrix.m);
    }

    /**
     * \brief Calls IVRSystem::GetEyeToHeadTransform
     *
     * The runtime is built with MSVC, which passes \c this in ECX and the
     * address of a returned struct on the stack. 32-bit GCC swaps the two
     * for virtual methods returning a struct, so the method is called
     * through the vtable with MSVC's convention instead.
     */
    vr::HmdMatrix34_t getEyeToHeadTransform(vr::IVRSystem* system, vr::EVREye eye) {
#if defined(__i386__) && !defined(_MSC_VER)
      // Slot of GetEyeToHeadTransform in the IVRSystem_019 vtable
      constexpr size_t EyeToHeadTransformSlot = 4;

      using Fn = void (__thiscall*)(vr::IVRSystem*, vr::HmdMatrix34_t*, vr::EVREye);

      vr::HmdMatrix34_t result = { };
      auto vtable = *reinterpret_cast<void* const* const*>(system);
      reinterpret_cast<Fn>(vtable[EyeToHeadTransformSlot])(system, &result, eye);
      return result;
#else
      return system->GetEyeToHeadTransform(eye);
#endif
    }

    std::vector<std::string> splitExtensions(const std::string& list) {
      std::vector<std::string> result;
      std::stringstream stream(list);
      std::string name;

      while (std::getline(stream, name, ' ')) {
        if (!name.empty())
          result.push_back(name);
      }

      return result;
    }

    std::string trimTerminator(std::vector<char> buffer, uint32_t length) {
      // The reported length includes the terminating zero
      size_t size = std::min<size_t>(length, buffer.size());

      while (size && buffer[size - 1] == '\0')
        size--;

      return std::string(buffer.data(), size);
    }

  }


  /**
   * \brief The loaded openvr_api.dll and the interfaces taken from it
   */
  struct VrOpenVrBackend::Runtime {
    using InitFn      = vr::IVRSystem* (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType);
    using ShutdownFn  = void (VR_CALLTYPE*)();
    using InterfaceFn = void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*);

    HMODULE            library    = nullptr;
    ShutdownFn         shutdown   = nullptr;
    vr::IVRSystem*     system     = nullptr;
    vr::IVRCompositor* compositor = nullptr;
    vr::IVROverlay*    overlay    = nullptr;
    vr::IVRInput*      input      = nullptr;
    bool               initialized = false;
  };


  VrOpenVrBackend::VrOpenVrBackend() { }


  VrOpenVrBackend::~VrOpenVrBackend() {
    endSession();
    shutdownRuntime();
  }


  const char* VrOpenVrBackend::name() const {
    return "OpenVR";
  }


  bool VrOpenVrBackend::initializeRuntime() {
    if (m_runtime)
      return m_runtime->initialized;

    m_runtime = std::make_unique<Runtime>();
    const char* libraryName = nullptr;

    for (const char* name : RuntimeLibraryNames) {
      m_runtime->library = LoadLibraryA(name);

      if (m_runtime->library) {
        libraryName = name;
        break;
      }
    }

    if (!m_runtime->library) {
      Logger::err("VR: No OpenVR runtime library was found, copy the 32-bit openvr_api.dll next to the game");
      return false;
    }

    Logger::info(str::format("VR: Loaded ", libraryName));

    auto init = reinterpret_cast<Runtime::InitFn>(GetProcAddress(m_runtime->library, "VR_InitInternal"));
    auto getInterface = reinterpret_cast<Runtime::InterfaceFn>(GetProcAddress(m_runtime->library, "VR_GetGenericInterface"));
    m_runtime->shutdown = reinterpret_cast<Runtime::ShutdownFn>(GetProcAddress(m_runtime->library, "VR_ShutdownInternal"));

    if (!init || !getInterface || !m_runtime->shutdown) {
      Logger::err(str::format("VR: ", libraryName, " does not export the OpenVR entry points"));
      return false;
    }

    vr::EVRInitError error = vr::VRInitError_None;
    init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None) {
      Logger::err(str::format("VR: OpenVR could not be initialized, error ", int32_t(error),
        ". Is SteamVR installed and the headset connected?"));
      return false;
    }

    m_runtime->initialized = true;

    m_runtime->system = static_cast<vr::IVRSystem*>(getInterface(vr::IVRSystem_Version, &error));

    if (error == vr::VRInitError_None)
      m_runtime->compositor = static_cast<vr::IVRCompositor*>(getInterface(vr::IVRCompositor_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->system || !m_runtime->compositor) {
      Logger::err(str::format("VR: The OpenVR interfaces are not available, error ", int32_t(error)));
      m_runtime->system     = nullptr;
      m_runtime->compositor = nullptr;
      return false;
    }

    // Only the panel needs the overlay interface, VR works without it
    m_runtime->overlay = static_cast<vr::IVROverlay*>(getInterface(vr::IVROverlay_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->overlay) {
      Logger::warn(str::format("VR: The OpenVR overlay interface is not available, error ", int32_t(error),
        ". Menus will not be shown in the headset"));
      m_runtime->overlay = nullptr;
    }

    // Controllers need the input interface and the action manifest, the
    // headset works without them
    m_runtime->input = static_cast<vr::IVRInput*>(getInterface(vr::IVRInput_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->input) {
      Logger::warn(str::format("VR: The OpenVR input interface is not available, error ", int32_t(error),
        ". The controllers will do nothing"));
      m_runtime->input = nullptr;
    }

    m_inputReady = initializeInput();

    Logger::info("VR: OpenVR initialized");
    return true;
  }


  std::string VrOpenVrBackend::manifestPath() {
    HMODULE module = nullptr;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&VrOpenVrBackend::manifestPath), &module))
      return std::string();

    std::vector<WCHAR> buffer(MAX_PATH);
    DWORD length = 0;

    while ((length = GetModuleFileNameW(module, buffer.data(), DWORD(buffer.size()))) == buffer.size())
      buffer.resize(buffer.size() * 2);

    if (!length)
      return std::string();

    std::wstring path(buffer.data(), length);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"fnvvr\\actions.json";

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
      Logger::warn(str::format("VR: The action manifest ", str::fromws(path.c_str()),
        " was not found. The controllers will do nothing"));
      return std::string();
    }

    return str::fromws(path.c_str());
  }


  bool VrOpenVrBackend::initializeInput() {
    vr::IVRInput* input = m_runtime->input;

    if (!input)
      return false;

    std::string manifest = manifestPath();

    if (manifest.empty())
      return false;

    vr::EVRInputError error = input->SetActionManifestPath(manifest.c_str());

    if (error != vr::VRInputError_None) {
      Logger::err(str::format("VR: SteamVR rejected the action manifest ", manifest, ", error ", int32_t(error)));
      return false;
    }

    bool found = true;

    auto action = [&] (const char* path, uint64_t& handle) {
      vr::EVRInputError result = input->GetActionHandle(path, &handle);

      if (result != vr::VRInputError_None) {
        Logger::err(str::format("VR: The action ", path, " is unknown to SteamVR, error ", int32_t(result)));
        found = false;
      }
    };

    for (uint32_t i = 0; i < 2; i++) {
      if (input->GetActionSetHandle(VrOpenVrActionSets[i], &m_actions.sets[i]) != vr::VRInputError_None) {
        Logger::err(str::format("VR: The action set ", VrOpenVrActionSets[i], " is unknown to SteamVR"));
        found = false;
      }

      action(vrOpenVrStickPath(VrInputContext(i), false), m_actions.move[i]);
    }

    for (uint32_t i = 0; i < VrActionCount; i++)
      action(vrOpenVrActionPath(VrAction(i)), m_actions.digital[i]);

    action(vrOpenVrStickPath(VrInputContext::Game, true), m_actions.turn);

    for (uint32_t i = 0; i < VrHandCount; i++)
      action(vrOpenVrHapticPath(VrHand(i)), m_actions.haptic[i]);

    if (found)
      Logger::info(str::format("VR: Controller actions loaded from ", manifest));

    return found;
  }


  void VrOpenVrBackend::shutdownRuntime() {
    if (!m_runtime)
      return;

    if (m_runtime->initialized)
      m_runtime->shutdown();

    if (m_runtime->library)
      FreeLibrary(m_runtime->library);

    m_runtime = nullptr;
  }


  VrVulkanRequirements VrOpenVrBackend::queryVulkanRequirements() {
    VrVulkanRequirements requirements;

    if (!initializeRuntime() || !m_runtime->compositor)
      return requirements;

    uint32_t length = m_runtime->compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
    std::vector<char> buffer(length);
    length = m_runtime->compositor->GetVulkanInstanceExtensionsRequired(buffer.data(), length);

    requirements.instanceExtensions = splitExtensions(trimTerminator(std::move(buffer), length));
    return requirements;
  }


  std::vector<std::string> VrOpenVrBackend::queryDeviceExtensions(VkPhysicalDevice physicalDevice) {
    if (!m_runtime || !m_runtime->compositor)
      return { };

    auto* device = reinterpret_cast<VkPhysicalDevice_T*>(physicalDevice);

    uint32_t length = m_runtime->compositor->GetVulkanDeviceExtensionsRequired(device, nullptr, 0);
    std::vector<char> buffer(length);
    length = m_runtime->compositor->GetVulkanDeviceExtensionsRequired(device, buffer.data(), length);

    return splitExtensions(trimTerminator(std::move(buffer), length));
  }


  VkPhysicalDevice VrOpenVrBackend::selectPhysicalDevice(VkInstance instance) {
    if (!m_runtime || !m_runtime->system)
      return VK_NULL_HANDLE;

    uint64_t device = 0;
    m_runtime->system->GetOutputDevice(&device, vr::TextureType_Vulkan,
      reinterpret_cast<VkInstance_T*>(instance));

    return reinterpret_cast<VkPhysicalDevice>(device);
  }


  bool VrOpenVrBackend::beginSession(const VrGraphicsBinding& binding) {
    if (!initializeRuntime() || !m_runtime->compositor)
      return false;

    m_transition = VrEyeTransition::create(binding);

    if (!m_transition)
      return false;

    m_binding = binding;

    // Poses are reported relative to the floor, with the origin where the
    // room set-up of the user puts it
    m_runtime->compositor->SetTrackingSpace(vr::TrackingUniverseStanding);

    float rate = m_runtime->system->GetFloatTrackedDeviceProperty(
      vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);

    if (rate <= 0.0f)
      rate = FallbackRefreshRate;

    m_periodNs = int64_t(double(NanosecondsPerSecond) / double(rate));
    m_state    = VrSessionState::Running;
    return true;
  }


  void VrOpenVrBackend::endSession() {
    destroyPanels();
    m_transition = nullptr;

    if (m_state != VrSessionState::Idle)
      m_state = VrSessionState::Idle;
  }


  VrSessionState VrOpenVrBackend::sessionState() const {
    return m_state;
  }


  VrExtent VrOpenVrBackend::recommendedEyeExtent() const {
    VrExtent extent;

    if (m_runtime && m_runtime->system)
      m_runtime->system->GetRecommendedRenderTargetSize(&extent.width, &extent.height);

    return extent;
  }


  void VrOpenVrBackend::pollEvents() {
    vr::VREvent_t event;

    while (m_runtime->system->PollNextEvent(&event, sizeof(event))) {
      if (event.eventType == vr::VREvent_Quit && m_state == VrSessionState::Running) {
        Logger::info("VR: SteamVR asked the application to quit");
        m_state = VrSessionState::Stopping;
      }
    }
  }


  uint32_t VrOpenVrBackend::controllerIndex(VrHand hand) const {
    return m_runtime->system->GetTrackedDeviceIndexForControllerRole(
      hand == VrHand::Left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
  }


  VrFrameTiming VrOpenVrBackend::waitFrame() {
    VrFrameTiming timing;
    timing.predictedPeriod = m_periodNs;

    if (m_state != VrSessionState::Running)
      return timing;

    pollEvents();

    // Blocks until the compositor wants the next frame. The poses are
    // converted here and handed out by pollInput
    std::vector<vr::TrackedDevicePose_t> poses(vr::k_unMaxTrackedDeviceCount);
    vr::EVRCompositorError error = m_runtime->compositor->WaitGetPoses(
      poses.data(), uint32_t(poses.size()), nullptr, 0);

    if (error != vr::VRCompositorError_None) {
      if (!m_loggedWait) {
        Logger::err(str::format("VR: WaitGetPoses failed, error ", int32_t(error)));
        m_loggedWait = true;
      }

      return timing;
    }

    m_poses = VrInputState();

    const vr::TrackedDevicePose_t& head = poses[vr::k_unTrackedDeviceIndex_Hmd];
    m_poses.isHeadTracked = head.bPoseIsValid;

    if (!m_loggedWait) {
      Logger::info(str::format("VR: First poses received, head tracked: ",
        head.bPoseIsValid ? "yes" : "no"));
      m_loggedWait = true;
    }

    if (head.bPoseIsValid)
      m_poses.headPose = poseFromOpenVr(head.mDeviceToAbsoluteTracking);

    for (uint32_t i = 0; i < VrHandCount; i++) {
      uint32_t index = controllerIndex(VrHand(i));

      if (index >= poses.size() || !poses[index].bPoseIsValid)
        continue;

      VrControllerState& controller = m_poses.controllers[i];
      controller.isActive = true;
      controller.gripPose = poseFromOpenVr(poses[index].mDeviceToAbsoluteTracking);
      controller.aimPose  = controller.gripPose;
    }

    // The runtime predicts the poses itself, so the display time is only a
    // frame counter for the callers
    timing.shouldRender         = m_runtime->compositor->CanRenderScene();
    timing.predictedDisplayTime = ++m_frameCounter;
    return timing;
  }


  VrInputState VrOpenVrBackend::pollInput(int64_t displayTime) {
    VrInputState input = m_poses;

    if (m_inputReady)
      readActions(input);

    return input;
  }


  void VrOpenVrBackend::setInputContext(VrInputContext context) {
    m_context = context;
  }


  void VrOpenVrBackend::readActions(VrInputState& input) {
    vr::IVRInput* vrInput = m_runtime->input;
    uint32_t context = uint32_t(m_context);

    vr::VRActiveActionSet_t active = { };
    active.ulActionSet          = m_actions.sets[context];
    active.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;

    vr::EVRInputError error = vrInput->UpdateActionState(&active, sizeof(active), 1);

    if (error != vr::VRInputError_None) {
      if (!m_loggedInputError) {
        Logger::err(str::format("VR: Reading the controller actions failed, error ", int32_t(error)));
        m_loggedInputError = true;
      }

      return;
    }

    // Actions only mean something while a controller drives them
    input.hasActions = input.controllers[0].isActive || input.controllers[1].isActive;

    for (uint32_t i = 0; i < VrActionCount; i++) {
      if (uint32_t(vrOpenVrActionContext(VrAction(i))) != context)
        continue;

      vr::InputDigitalActionData_t data = { };

      if (vrInput->GetDigitalActionData(m_actions.digital[i], &data, sizeof(data)) == vr::VRInputError_None)
        input.actions.setPressed(VrAction(i), data.bActive && data.bState);
    }

    auto readStick = [vrInput] (uint64_t handle) {
      vr::InputAnalogActionData_t data = { };
      VrVector2 result;

      if (vrInput->GetAnalogActionData(handle, &data, sizeof(data)) == vr::VRInputError_None && data.bActive)
        result = { data.x, data.y };

      return result;
    };

    input.actions.move = readStick(m_actions.move[context]);

    if (m_context == VrInputContext::Game)
      input.actions.turn = readStick(m_actions.turn);
  }


  std::array<VrEyeView, VrEyeCount> VrOpenVrBackend::locateViews(int64_t displayTime) {
    std::array<VrEyeView, VrEyeCount> views;

    if (!m_runtime || !m_runtime->system)
      return views;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      vr::EVREye eye = i == uint32_t(VrEye::Left) ? vr::Eye_Left : vr::Eye_Right;

      VrPose eyeInHead = poseFromOpenVr(getEyeToHeadTransform(m_runtime->system, eye));
      views[i].pose = vrCompose(m_poses.headPose, eyeInHead);

      float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
      m_runtime->system->GetProjectionRaw(eye, &left, &right, &top, &bottom);
      views[i].fov = vrFovFromProjectionRaw(left, right, top, bottom);
    }

    return views;
  }


  bool VrOpenVrBackend::submitFrame(const VrFrameSubmission& frame) {
    if (m_state != VrSessionState::Running || !m_transition)
      return false;

    // OpenVR takes whole images, not layers of an array
    for (const VrEyeImage& eye : frame.images) {
      if (eye.arrayLayer != 0 || eye.image == VK_NULL_HANDLE)
        return false;
    }

    if (!m_loggedSubmit) {
      Logger::info(str::format("VR: Submitting the first frame, eye image ",
        frame.images[0].extent.width, "x", frame.images[0].extent.height,
        ", format ", uint32_t(frame.images[0].format), ", layout ", uint32_t(frame.images[0].layout)));
    }

    if (!m_transition->toTransferSource(frame.images.data(), VrEyeCount))
      return false;

    if (!m_loggedSubmit)
      Logger::info("VR: Eye images ready for the compositor");

    bool accepted = true;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      vr::VRVulkanTextureData_t data = { };
      fillTextureData(frame.images[i], data);

      // The game writes final display values, so the image is gamma encoded
      vr::Texture_t texture = { &data, vr::TextureType_Vulkan, vr::ColorSpace_Gamma };

      vr::EVRCompositorError error = m_runtime->compositor->Submit(
        i == uint32_t(VrEye::Left) ? vr::Eye_Left : vr::Eye_Right, &texture, nullptr, vr::Submit_Default);

      if (error != vr::VRCompositorError_None) {
        Logger::err(str::format("VR: The compositor rejected an eye image, error ", int32_t(error)));
        accepted = false;
      }
    }

    if (!m_loggedSubmit) {
      Logger::info("VR: First frame handed to the compositor");
      m_loggedSubmit = true;
    }

    // Always put the images back, DXVK still owns them
    return m_transition->restore(frame.images.data(), VrEyeCount) && accepted;
  }


  void VrOpenVrBackend::fillTextureData(const VrEyeImage& image, vr::VRVulkanTextureData_t& data) const {
    data.m_nImage            = uint64_t(image.image);
    data.m_pDevice           = reinterpret_cast<VkDevice_T*>(m_binding.device);
    data.m_pPhysicalDevice   = reinterpret_cast<VkPhysicalDevice_T*>(m_binding.physicalDevice);
    data.m_pInstance         = reinterpret_cast<VkInstance_T*>(m_binding.instance);
    data.m_pQueue            = reinterpret_cast<VkQueue_T*>(m_binding.queue);
    data.m_nQueueFamilyIndex = m_binding.queueFamilyIndex;
    data.m_nWidth            = image.extent.width;
    data.m_nHeight           = image.extent.height;
    data.m_nFormat           = uint32_t(image.format);
    data.m_nSampleCount      = 1;
  }


  void VrOpenVrBackend::submitEmptyFrame(int64_t displayTime) {
    // OpenVR has no frame to close: the next WaitGetPoses starts the next one
  }


  bool VrOpenVrBackend::submitPanel(VrPanelId id, const VrPanelSubmission& panel) {
    if (m_state != VrSessionState::Running || !m_transition || !createPanel(id))
      return false;

    if (panel.image.arrayLayer != 0 || panel.image.image == VK_NULL_HANDLE)
      return false;

    vr::IVROverlay* overlay = m_runtime->overlay;
    Panel& state = m_panels[uint32_t(id)];

    vr::HmdMatrix34_t transform = { };
    vrPoseToMatrix34(panel.pose, transform.m);

    if (panel.anchor == VrPanelAnchor::Head)
      overlay->SetOverlayTransformTrackedDeviceRelative(state.handle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
    else
      overlay->SetOverlayTransformAbsolute(state.handle, vr::TrackingUniverseStanding, &transform);

    overlay->SetOverlayWidthInMeters(state.handle, panel.width);

    if (!m_transition->toTransferSource(&panel.image, 1))
      return false;

    vr::VRVulkanTextureData_t data = { };
    fillTextureData(panel.image, data);

    vr::Texture_t texture = { &data, vr::TextureType_Vulkan, vr::ColorSpace_Gamma };
    vr::EVROverlayError error = overlay->SetOverlayTexture(state.handle, &texture);

    if (error == vr::VROverlayError_None && !state.visible) {
      error = overlay->ShowOverlay(state.handle);
      state.visible = error == vr::VROverlayError_None;
    }

    if (!state.logged) {
      if (error == vr::VROverlayError_None) {
        Logger::info(str::format("VR: Showing the panel ", PanelOverlayKeys[uint32_t(id)], ", image ",
          panel.image.extent.width, "x", panel.image.extent.height, ", ", panel.width, " m wide"));
      } else {
        Logger::err(str::format("VR: The image of the panel ", PanelOverlayKeys[uint32_t(id)],
          " was rejected, error ", int32_t(error)));
      }

      state.logged = true;
    }

    return m_transition->restore(&panel.image, 1) && error == vr::VROverlayError_None;
  }


  void VrOpenVrBackend::hidePanel(VrPanelId id) {
    Panel& state = m_panels[uint32_t(id)];

    if (!state.visible)
      return;

    m_runtime->overlay->HideOverlay(state.handle);
    state.visible = false;
  }


  bool VrOpenVrBackend::createPanel(VrPanelId id) {
    Panel& state = m_panels[uint32_t(id)];

    if (state.handle)
      return true;

    if (state.failed || !m_runtime || !m_runtime->overlay)
      return false;

    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError error = m_runtime->overlay->CreateOverlay(
      PanelOverlayKeys[uint32_t(id)], PanelOverlayNames[uint32_t(id)], &handle);

    if (error != vr::VROverlayError_None || handle == vr::k_ulOverlayHandleInvalid) {
      Logger::err(str::format("VR: The overlay of the panel ", PanelOverlayKeys[uint32_t(id)],
        " could not be created, error ", int32_t(error)));
      state.failed = true;
      return false;
    }

    m_runtime->overlay->SetOverlaySortOrder(handle, uint32_t(id));
    state.handle = handle;
    return true;
  }


  void VrOpenVrBackend::destroyPanels() {
    for (Panel& state : m_panels) {
      if (state.handle)
        m_runtime->overlay->DestroyOverlay(state.handle);

      state.handle  = 0;
      state.visible = false;
    }
  }


  void VrOpenVrBackend::applyHaptic(VrHand hand, float amplitude, int64_t durationNs) {
    if (!m_inputReady)
      return;

    // Frequency the Touch controllers vibrate at by default
    constexpr float FrequencyHz = 160.0f;

    float seconds  = float(std::max<int64_t>(0, durationNs)) * 1e-9f;
    float strength = std::max(0.0f, std::min(1.0f, amplitude));

    if (seconds > 0.0f && strength > 0.0f)
      m_runtime->input->TriggerHapticVibrationAction(m_actions.haptic[uint32_t(hand)], 0.0f, seconds, FrequencyHz, strength);
  }

}
