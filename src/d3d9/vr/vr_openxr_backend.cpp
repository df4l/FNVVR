#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

#include <vulkan/vulkan_core.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_math.h"
#include "vr_openxr_backend.h"
#include "vr_openxr_convert.h"
#include "vr_openxr_image_copy.h"
#include "vr_openxr_input.h"
#include "vr_openxr_loader.h"
#include "vr_openxr_vulkan_device.h"

namespace dxvk {

  namespace {

    constexpr XrViewConfigurationType ViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    // The lasers: as OpenVR's, a strip a few millimetres wide in the menu
    // pointer's colour, sRGB-encoded, brighter while it points at something
    constexpr float                PointerWidth         = 0.005f;
    constexpr std::array<float, 3> PointerColor         = { 1.0f, 182.0f / 255.0f, 66.0f / 255.0f };
    constexpr float                PointerActiveAlpha   = 0.9f;
    constexpr float                PointerInactiveAlpha = 0.35f;

    // Images filled with one colour are only a few texels wide
    constexpr VrExtent             SolidImageExtent     = { 8, 8 };

    // The fade's quad: in front of the eyes and wider than any field of view
    constexpr float                FadeDistance         = 0.5f;
    constexpr float                FadeSize             = 8.0f;
    // Smallest change of opacity that refills the fade's image
    constexpr float                FadeStep             = 0.5f / 255.0f;

    // Longest wait for the runtime to start the launcher's session
    constexpr double               PanelSessionStartSeconds = 5.0;

    // Seconds of frames in each frame statistics line
    constexpr double               StatsPeriodSeconds   = 10.0;

    double steadySeconds() {
      return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    double wallClockSeconds() {
      // FILETIME counts 100 ns units since 1601
      constexpr uint64_t UnitsPerSecond = 10000000;
      constexpr uint64_t UnixEpoch      = 116444736000000000;

      FILETIME now;
      GetSystemTimePreciseAsFileTime(&now);
      uint64_t value = (uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
      return double(value - UnixEpoch) / double(UnitsPerSecond);
    }

    std::string xrVersionString(XrVersion version) {
      return str::format(XR_VERSION_MAJOR(version), ".", XR_VERSION_MINOR(version), ".", XR_VERSION_PATCH(version));
    }

    const char* referenceSpaceName(XrReferenceSpaceType type) {
      switch (type) {
        case XR_REFERENCE_SPACE_TYPE_STAGE:           return "stage";
        case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT: return "local floor";
        case XR_REFERENCE_SPACE_TYPE_LOCAL:           return "local";
        default:                                      return "other";
      }
    }

    const char* panelName(VrPanelId id) {
      switch (id) {
        case VrPanelId::Menu:          return "menu";
        case VrPanelId::HudHead:       return "head HUD";
        case VrPanelId::HudWrist:      return "wrist HUD";
        case VrPanelId::HudPrompt:     return "prompt";
        case VrPanelId::HudCharge:     return "charge";
        case VrPanelId::HudAmmo:       return "ammunition";
        case VrPanelId::QuickWheel:    return "quick wheel";
        case VrPanelId::Terminal:      return "terminal";
        case VrPanelId::CaravanValues: return "Caravan values";
      }

      return "panel";
    }

  }


  struct VrOpenXrBackend::Functions {
    PFN_xrEnumerateInstanceExtensionProperties enumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance                    createInstance                    = nullptr;
    PFN_xrDestroyInstance                   destroyInstance                   = nullptr;
    PFN_xrResultToString                    resultToString                    = nullptr;
    PFN_xrGetInstanceProperties             getInstanceProperties             = nullptr;
    PFN_xrGetSystem                         getSystem                         = nullptr;
    PFN_xrGetSystemProperties               getSystemProperties               = nullptr;
    PFN_xrEnumerateViewConfigurationViews   enumerateViewConfigurationViews   = nullptr;
    PFN_xrGetVulkanGraphicsRequirementsKHR  getVulkanGraphicsRequirements     = nullptr;
    PFN_xrGetVulkanInstanceExtensionsKHR    getVulkanInstanceExtensions       = nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR      getVulkanDeviceExtensions         = nullptr;
    PFN_xrGetVulkanGraphicsDeviceKHR        getVulkanGraphicsDevice           = nullptr;
    PFN_xrCreateSession                     createSession                     = nullptr;
    PFN_xrDestroySession                    destroySession                    = nullptr;
    PFN_xrBeginSession                      beginSession                      = nullptr;
    PFN_xrEndSession                        endSession                        = nullptr;
    PFN_xrPollEvent                         pollEvent                         = nullptr;
    PFN_xrEnumerateReferenceSpaces          enumerateReferenceSpaces          = nullptr;
    PFN_xrCreateReferenceSpace              createReferenceSpace              = nullptr;
    PFN_xrDestroySpace                      destroySpace                      = nullptr;
    PFN_xrLocateSpace                       locateSpace                       = nullptr;
    PFN_xrLocateViews                       locateViews                       = nullptr;
    PFN_xrWaitFrame                         waitFrame                         = nullptr;
    PFN_xrBeginFrame                        beginFrame                        = nullptr;
    PFN_xrEndFrame                          endFrame                          = nullptr;
    PFN_xrEnumerateSwapchainFormats         enumerateSwapchainFormats         = nullptr;
    PFN_xrCreateSwapchain                   createSwapchain                   = nullptr;
    PFN_xrDestroySwapchain                  destroySwapchain                  = nullptr;
    PFN_xrEnumerateSwapchainImages          enumerateSwapchainImages          = nullptr;
    PFN_xrAcquireSwapchainImage             acquireSwapchainImage             = nullptr;
    PFN_xrWaitSwapchainImage                waitSwapchainImage                = nullptr;
    PFN_xrReleaseSwapchainImage             releaseSwapchainImage             = nullptr;

    bool load(const VrOpenXrLoader& loader, XrInstance instance) {
      bool complete = true;

      auto load = [&] (auto& target, const char* name) {
        target = loader.get<std::remove_reference_t<decltype(target)>>(instance, name);

        if (!target) {
          Logger::err(str::format("VR: OpenXR function ", name, " is not available"));
          complete = false;
        }
      };

      load(destroyInstance, "xrDestroyInstance");
      load(resultToString, "xrResultToString");
      load(getInstanceProperties, "xrGetInstanceProperties");
      load(getSystem, "xrGetSystem");
      load(getSystemProperties, "xrGetSystemProperties");
      load(enumerateViewConfigurationViews, "xrEnumerateViewConfigurationViews");
      load(getVulkanGraphicsRequirements, "xrGetVulkanGraphicsRequirementsKHR");
      load(getVulkanInstanceExtensions, "xrGetVulkanInstanceExtensionsKHR");
      load(getVulkanDeviceExtensions, "xrGetVulkanDeviceExtensionsKHR");
      load(getVulkanGraphicsDevice, "xrGetVulkanGraphicsDeviceKHR");
      load(createSession, "xrCreateSession");
      load(destroySession, "xrDestroySession");
      load(beginSession, "xrBeginSession");
      load(endSession, "xrEndSession");
      load(pollEvent, "xrPollEvent");
      load(enumerateReferenceSpaces, "xrEnumerateReferenceSpaces");
      load(createReferenceSpace, "xrCreateReferenceSpace");
      load(destroySpace, "xrDestroySpace");
      load(locateSpace, "xrLocateSpace");
      load(locateViews, "xrLocateViews");
      load(waitFrame, "xrWaitFrame");
      load(beginFrame, "xrBeginFrame");
      load(endFrame, "xrEndFrame");
      load(enumerateSwapchainFormats, "xrEnumerateSwapchainFormats");
      load(createSwapchain, "xrCreateSwapchain");
      load(destroySwapchain, "xrDestroySwapchain");
      load(enumerateSwapchainImages, "xrEnumerateSwapchainImages");
      load(acquireSwapchainImage, "xrAcquireSwapchainImage");
      load(waitSwapchainImage, "xrWaitSwapchainImage");
      load(releaseSwapchainImage, "xrReleaseSwapchainImage");
      return complete;
    }
  };


  VrOpenXrBackend::VrOpenXrBackend()
  : m_xr(std::make_unique<Functions>()) { }


  VrOpenXrBackend::~VrOpenXrBackend() {
    endSession();

    // The actions belong to the instance
    m_input = nullptr;

    if (m_instance != XR_NULL_HANDLE && m_xr->destroyInstance)
      m_xr->destroyInstance(m_instance);
  }


  const char* VrOpenXrBackend::name() const {
    return "OpenXR";
  }


  std::string VrOpenXrBackend::resultString(XrResult result) const {
    char buffer[XR_MAX_RESULT_STRING_SIZE] = { };

    if (m_instance != XR_NULL_HANDLE && m_xr->resultToString
     && XR_SUCCEEDED(m_xr->resultToString(m_instance, result, buffer)))
      return str::format(buffer, " (", int32_t(result), ")");

    return str::format(int32_t(result));
  }


  bool VrOpenXrBackend::check(XrResult result, const char* what) const {
    if (XR_SUCCEEDED(result))
      return true;

    std::string message = str::format("VR: OpenXR ", what, " failed: ", resultString(result));
    std::lock_guard<dxvk::mutex> lock(m_failureMutex);

    switch (m_failures.count(message)) {
      case VrXrFailureVerdict::Log:
        Logger::err(message);
        break;

      case VrXrFailureVerdict::LogLast:
        Logger::err(str::format(message, " (repeated, no longer logged until the next session)"));
        break;

      case VrXrFailureVerdict::Skip:
        break;
    }

    return false;
  }


  bool VrOpenXrBackend::initializeRuntime() {
    if (m_initialized)
      return m_runtimeReady;

    m_initialized = true;
    m_loader = std::make_unique<VrOpenXrLoader>();

    if (!m_loader->isLoaded())
      return false;

    Logger::info(str::format("VR: Loaded ", str::fromws(m_loader->path().c_str())));

    if (!createInstance() || !findSystem())
      return false;

    logViewConfiguration();

    // Without them the game is still shown, played with a gamepad
    m_input = VrOpenXrInput::create(*m_loader, m_instance, m_eyeGaze);

    // Required before a session can be created
    XrGraphicsRequirementsVulkanKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };

    if (!check(m_xr->getVulkanGraphicsRequirements(m_instance, m_system, &requirements), "xrGetVulkanGraphicsRequirementsKHR"))
      return false;

    // XR_KHR_vulkan_enable reports Vulkan versions in OpenXR's encoding
    Logger::info(str::format("VR: OpenXR Vulkan API versions: min ", xrVersionString(requirements.minApiVersionSupported),
      ", max ", xrVersionString(requirements.maxApiVersionSupported)));

    m_runtimeReady = true;
    return true;
  }


  bool VrOpenXrBackend::createInstance() {
    auto enumerate = m_loader->get<PFN_xrEnumerateInstanceExtensionProperties>(
      XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties");
    auto create = m_loader->get<PFN_xrCreateInstance>(XR_NULL_HANDLE, "xrCreateInstance");

    if (!enumerate || !create) {
      Logger::err("VR: The OpenXR loader lacks xrEnumerateInstanceExtensionProperties or xrCreateInstance");
      return false;
    }

    uint32_t count = 0;

    // XR_ERROR_RUNTIME_UNAVAILABLE: no runtime is registered, or it is not running
    if (!check(enumerate(nullptr, 0, &count, nullptr), "xrEnumerateInstanceExtensionProperties"))
      return false;

    std::vector<XrExtensionProperties> extensions(count, { XR_TYPE_EXTENSION_PROPERTIES });

    if (!check(enumerate(nullptr, count, &count, extensions.data()), "xrEnumerateInstanceExtensionProperties"))
      return false;

    extensions.resize(count);

    std::string list;

    for (const auto& e : extensions)
      list += str::format(list.empty() ? "" : " ", e.extensionName);

    Logger::info(str::format("VR: OpenXR runtime extensions: ", list));

    auto offers = [&extensions] (const char* name) {
      return std::any_of(extensions.begin(), extensions.end(),
        [name] (const XrExtensionProperties& e) { return !std::strcmp(e.extensionName, name); });
    };

    if (!offers(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) {
      Logger::err(str::format("VR: The OpenXR runtime does not offer ", XR_KHR_VULKAN_ENABLE_EXTENSION_NAME));
      return false;
    }

    std::vector<const char*> enabled = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
    m_localFloor = offers(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);

    if (m_localFloor)
      enabled.push_back(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);

    m_eyeGazeExtension = offers(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

    if (m_eyeGazeExtension)
      enabled.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

    XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
    std::strncpy(info.applicationInfo.applicationName, "Fallout New Vegas", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(info.applicationInfo.engineName, "FNVVR", XR_MAX_ENGINE_NAME_SIZE - 1);
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.engineVersion      = 1;
    info.applicationInfo.apiVersion         = XR_API_VERSION_1_0;
    info.enabledExtensionCount = uint32_t(enabled.size());
    info.enabledExtensionNames = enabled.data();

    if (!check(create(&info, &m_instance), "xrCreateInstance")) {
      m_instance = XR_NULL_HANDLE;
      return false;
    }

    if (!m_xr->load(*m_loader, m_instance))
      return false;

    XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };

    if (check(m_xr->getInstanceProperties(m_instance, &properties), "xrGetInstanceProperties")) {
      Logger::info(str::format("VR: OpenXR runtime: ", properties.runtimeName,
        " ", xrVersionString(properties.runtimeVersion)));
    }

    return true;
  }


  bool VrOpenXrBackend::findSystem() {
    XrSystemGetInfo info = { XR_TYPE_SYSTEM_GET_INFO };
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = m_xr->getSystem(m_instance, &info, &m_system);

    if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
      Logger::err("VR: The OpenXR runtime sees no headset: connect it before starting the game");
      return false;
    }

    if (!check(result, "xrGetSystem"))
      return false;

    XrSystemEyeGazeInteractionPropertiesEXT eyeGaze = { XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
    XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };

    if (m_eyeGazeExtension)
      properties.next = &eyeGaze;

    if (check(m_xr->getSystemProperties(m_instance, m_system, &properties), "xrGetSystemProperties")) {
      m_eyeGaze = m_eyeGazeExtension && eyeGaze.supportsEyeGazeInteraction;

      Logger::info(str::format("VR: OpenXR headset: ", properties.systemName,
        ", vendor ", properties.vendorId,
        ", max swapchain ", properties.graphicsProperties.maxSwapchainImageWidth,
        "x", properties.graphicsProperties.maxSwapchainImageHeight,
        ", max layers ", properties.graphicsProperties.maxLayerCount,
        ", position tracking ", properties.trackingProperties.positionTracking ? "yes" : "no",
        ", eye tracker ", m_eyeGaze ? "yes" : "no"));
    }

    return true;
  }


  void VrOpenXrBackend::logViewConfiguration() {
    uint32_t count = 0;

    if (!check(m_xr->enumerateViewConfigurationViews(m_instance, m_system, ViewConfiguration, 0, &count, nullptr),
          "xrEnumerateViewConfigurationViews"))
      return;

    std::vector<XrViewConfigurationView> views(count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });

    if (!check(m_xr->enumerateViewConfigurationViews(m_instance, m_system, ViewConfiguration, count, &count, views.data()),
          "xrEnumerateViewConfigurationViews"))
      return;

    for (uint32_t i = 0; i < count; i++) {
      Logger::info(str::format("VR: OpenXR view ", i,
        ": recommended ", views[i].recommendedImageRectWidth, "x", views[i].recommendedImageRectHeight,
        ", max ", views[i].maxImageRectWidth, "x", views[i].maxImageRectHeight));
    }

    if (count >= VrEyeCount) {
      m_recommended.width  = views[0].recommendedImageRectWidth;
      m_recommended.height = views[0].recommendedImageRectHeight;
    }
  }


  VrVulkanRequirements VrOpenXrBackend::queryVulkanRequirements() {
    VrVulkanRequirements requirements;

    if (!initializeRuntime())
      return requirements;

    uint32_t length = 0;

    if (!check(m_xr->getVulkanInstanceExtensions(m_instance, m_system, 0, &length, nullptr), "xrGetVulkanInstanceExtensionsKHR"))
      return requirements;

    std::vector<char> buffer(length + 1, '\0');

    if (!check(m_xr->getVulkanInstanceExtensions(m_instance, m_system, length, &length, buffer.data()), "xrGetVulkanInstanceExtensionsKHR"))
      return requirements;

    Logger::info(str::format("VR: OpenXR Vulkan instance extensions: ", buffer.data()));
    requirements.instanceExtensions = vrSplitXrExtensionList(buffer.data());
    return requirements;
  }


  std::vector<std::string> VrOpenXrBackend::queryDeviceExtensions(VkPhysicalDevice physicalDevice) {
    if (!m_runtimeReady)
      return { };

    // XR_KHR_vulkan_enable names the extensions for the runtime's own GPU
    uint32_t length = 0;

    if (!check(m_xr->getVulkanDeviceExtensions(m_instance, m_system, 0, &length, nullptr), "xrGetVulkanDeviceExtensionsKHR"))
      return { };

    std::vector<char> buffer(length + 1, '\0');

    if (!check(m_xr->getVulkanDeviceExtensions(m_instance, m_system, length, &length, buffer.data()), "xrGetVulkanDeviceExtensionsKHR"))
      return { };

    Logger::info(str::format("VR: OpenXR Vulkan device extensions: ", buffer.data()));
    return vrSplitXrExtensionList(buffer.data());
  }


  VkPhysicalDevice VrOpenXrBackend::selectPhysicalDevice(VkInstance instance) {
    if (!m_runtimeReady)
      return VK_NULL_HANDLE;

    VkPhysicalDevice device = VK_NULL_HANDLE;

    if (!check(m_xr->getVulkanGraphicsDevice(m_instance, m_system, instance, &device), "xrGetVulkanGraphicsDeviceKHR"))
      return VK_NULL_HANDLE;

    return device;
  }


  bool VrOpenXrBackend::beginSession(const VrGraphicsBinding& binding) {
    if (!initializeRuntime())
      return false;

    m_copy = VrXrImageCopy::create(binding);

    if (!m_copy)
      return false;

    m_binding = binding;

    XrGraphicsBindingVulkanKHR graphics = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
    graphics.instance         = binding.instance;
    graphics.physicalDevice   = binding.physicalDevice;
    graphics.device           = binding.device;
    graphics.queueFamilyIndex = binding.queueFamilyIndex;
    graphics.queueIndex       = binding.queueIndex;

    XrSessionCreateInfo info = { XR_TYPE_SESSION_CREATE_INFO };
    info.next     = &graphics;
    info.systemId = m_system;

    if (!check(m_xr->createSession(m_instance, &info, &m_session), "xrCreateSession")) {
      m_session = XR_NULL_HANDLE;
      m_copy    = nullptr;
      return false;
    }

    uint32_t count = 0;

    if (check(m_xr->enumerateSwapchainFormats(m_session, 0, &count, nullptr), "xrEnumerateSwapchainFormats")) {
      m_swapchainFormats.resize(count);

      if (!check(m_xr->enumerateSwapchainFormats(m_session, count, &count, m_swapchainFormats.data()), "xrEnumerateSwapchainFormats"))
        count = 0;

      m_swapchainFormats.resize(count);
    }

    if (!createSpaces()) {
      destroySession();
      return false;
    }

    if (m_input && !m_input->attach(m_session))
      m_input = nullptr;

    { std::lock_guard<dxvk::mutex> lock(m_failureMutex);
      m_failures.clear();
    }

    Logger::info("VR: OpenXR session created, waiting for the runtime to start it");

    // The runtime usually tells the session to start right away
    pollEvents();
    return true;
  }


  bool VrOpenXrBackend::createSpaces() {
    uint32_t count = 0;
    std::vector<XrReferenceSpaceType> spaces;

    if (check(m_xr->enumerateReferenceSpaces(m_session, 0, &count, nullptr), "xrEnumerateReferenceSpaces")) {
      spaces.resize(count);

      if (!check(m_xr->enumerateReferenceSpaces(m_session, count, &count, spaces.data()), "xrEnumerateReferenceSpaces"))
        count = 0;

      spaces.resize(count);
    }

    XrReferenceSpaceType type = vrChooseXrReferenceSpace(spaces, m_localFloor);

    XrReferenceSpaceCreateInfo info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    info.referenceSpaceType   = type;
    info.poseInReferenceSpace = vrPoseToXr(VrPose());

    if (!check(m_xr->createReferenceSpace(m_session, &info, &m_space), "xrCreateReferenceSpace")) {
      m_space = XR_NULL_HANDLE;
      return false;
    }

    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;

    if (!check(m_xr->createReferenceSpace(m_session, &info, &m_viewSpace), "xrCreateReferenceSpace (view)")) {
      m_viewSpace = XR_NULL_HANDLE;
      return false;
    }

    Logger::info(str::format("VR: OpenXR poses are given in the ", referenceSpaceName(type), " space",
      type == XR_REFERENCE_SPACE_TYPE_LOCAL ? ", the floor height is unknown" : ""));
    return true;
  }


  bool VrOpenXrBackend::beginPanelSession() {
    if (m_session != XR_NULL_HANDLE || !initializeRuntime())
      return false;

    m_panelDevice = VrXrVulkanDevice::create(*this);

    if (!m_panelDevice)
      return false;

    if (!beginSession(m_panelDevice->binding())) {
      m_panelDevice = nullptr;
      return false;
    }

    // The launcher's loop runs while the session does
    double deadline = steadySeconds() + PanelSessionStartSeconds;

    while (!m_running && !m_lost && steadySeconds() < deadline) {
      Sleep(10);
      pollEvents();
    }

    if (!m_running) {
      Logger::warn("VR: The OpenXR runtime did not start the panel session");
      endSession();
      return false;
    }

    Logger::info("VR: OpenXR panel session running on a Vulkan device of its own");
    return true;
  }


  void VrOpenXrBackend::endSession() {
    // Destroying the session ends it, whatever state it is in. No frame is
    // closed: the device and its queue may be gone already.
    destroySession();

    // The panel session's device outlives the swapchains made on it
    m_panelDevice = nullptr;
  }


  void VrOpenXrBackend::destroySession() {
    // The copies into the swapchains must be done first: the helper waits for them
    m_copy = nullptr;

    { std::lock_guard<dxvk::mutex> lock(m_panelMutex);

      for (Panel& panel : m_panels) {
        destroySwapchain(panel.swapchain);
        panel.visible = false;
      }

      for (Swapchain& image : m_pointerImages)
        destroySwapchain(image);

      for (Pointer& pointer : m_pointers)
        pointer.visible = false;

      destroySwapchain(m_fadeImage);
      m_fadeFilled = -1.0f;
    }

    for (Swapchain& eye : m_eyes)
      destroySwapchain(eye);

    if (m_input)
      m_input->detach();

    if (m_viewSpace != XR_NULL_HANDLE)
      m_xr->destroySpace(m_viewSpace);

    if (m_space != XR_NULL_HANDLE)
      m_xr->destroySpace(m_space);

    m_viewSpace = XR_NULL_HANDLE;
    m_space     = XR_NULL_HANDLE;

    if (m_session != XR_NULL_HANDLE)
      m_xr->destroySession(m_session);

    m_session     = XR_NULL_HANDLE;
    m_running     = false;
    m_frameWaited  = false;
    m_frameBegun   = false;
    m_xrState      = XR_SESSION_STATE_UNKNOWN;
    m_frameClock   = FrameClock();
    m_statsRunning = false;
    m_hasLastCycle = false;
  }


  VrSessionState VrOpenXrBackend::sessionState() const {
    if (m_lost)
      return VrSessionState::Lost;

    if (m_session == XR_NULL_HANDLE)
      return VrSessionState::Idle;

    return vrSessionStateFromXr(m_xrState, m_running);
  }


  VrExtent VrOpenXrBackend::recommendedEyeExtent() const {
    return m_recommended;
  }


  void VrOpenXrBackend::pollEvents() {
    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };

    while (m_xr->pollEvent(m_instance, &event) == XR_SUCCESS) {
      switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
          auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);

          if (changed.session == m_session)
            onSessionStateChanged(changed.state);
        } break;

        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
          Logger::warn("VR: The OpenXR runtime is going away, VR stops");
          m_lost = true;
          break;

        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
          Logger::info("VR: The OpenXR play area changes");
          break;

        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
          if (m_input)
            m_input->logInteractionProfiles();
          break;

        default:
          break;
      }

      event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
  }


  void VrOpenXrBackend::onSessionStateChanged(XrSessionState state) {
    Logger::info(str::format("VR: OpenXR session state ", uint32_t(m_xrState), " -> ", uint32_t(state)));
    m_xrState = state;

    switch (state) {
      case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo info = { XR_TYPE_SESSION_BEGIN_INFO };
        info.primaryViewConfigurationType = ViewConfiguration;

        m_running = check(m_xr->beginSession(m_session, &info), "xrBeginSession");

        if (m_running)
          Logger::info("VR: OpenXR session running");
      } break;

      case XR_SESSION_STATE_STOPPING:
        // A frame cannot be ended once the session has ended
        m_frameWaited = false;
        m_frameBegun  = false;

        if (m_running)
          check(m_xr->endSession(m_session), "xrEndSession");

        m_running = false;
        Logger::info("VR: The OpenXR runtime stopped the session");
        break;

      case XR_SESSION_STATE_EXITING:
        Logger::info("VR: The OpenXR runtime asked the application to quit");
        break;

      case XR_SESSION_STATE_LOSS_PENDING:
        Logger::warn("VR: The OpenXR session is being lost, VR stops");
        m_lost = true;
        break;

      default:
        break;
    }
  }


  void VrOpenXrBackend::closeOpenFrame() {
    if (m_frameWaited && !m_frameBegun && m_running) {
      if (check(m_xr->beginFrame(m_session, nullptr), "xrBeginFrame"))
        m_frameBegun = true;

      m_frameWaited = false;
    }

    if (m_frameBegun)
      endFrame(nullptr);
  }


  VrFrameTiming VrOpenXrBackend::waitFrame() {
    VrFrameTiming timing;

    if (m_session == XR_NULL_HANDLE || m_lost)
      return timing;

    double waitStart = steadySeconds();

    // A frame waited for must be begun and ended before the next wait
    if (m_frameWaited || m_frameBegun) {
      if (m_binding.lockQueue)
        m_binding.lockQueue();

      closeOpenFrame();

      if (m_binding.releaseQueue)
        m_binding.releaseQueue();
    }

    pollEvents();

    if (!m_running)
      return timing;

    // Blocks until the runtime wants the next frame
    m_frameState = { XR_TYPE_FRAME_STATE };
    XrResult result = m_xr->waitFrame(m_session, nullptr, &m_frameState);

    if (!check(result, "xrWaitFrame"))
      return timing;

    m_hasLastCycle = false;
    measureFrame(waitStart, steadySeconds());

    if (result == XR_SESSION_LOSS_PENDING)
      m_lost = true;

    if (!m_loggedWait) {
      Logger::info(str::format("VR: First OpenXR frame, display period ",
        double(m_frameState.predictedDisplayPeriod) * 1e-6, " ms"));
      m_loggedWait = true;
    }

    m_frameWaited = true;

    timing.shouldRender         = m_frameState.shouldRender == XR_TRUE;
    timing.paced                = true;
    timing.predictedDisplayTime = m_frameState.predictedDisplayTime;
    timing.predictedPeriod      = m_frameState.predictedDisplayPeriod;
    timing.hasPreviousCycle     = m_hasLastCycle;
    timing.previousCycle        = m_lastCycle;
    timing.previousCycleEnd     = m_lastCycleEnd;
    return timing;
  }


  void VrOpenXrBackend::beginFrame() {
    if (!m_frameWaited || !m_running)
      return;

    m_frameWaited = false;

    // XR_FRAME_DISCARDED only means the previous frame was never ended
    if (check(m_xr->beginFrame(m_session, nullptr), "xrBeginFrame"))
      m_frameBegun = true;
  }


  VrInputState VrOpenXrBackend::pollInput(int64_t displayTime) {
    VrInputState input;

    if (!displayTime)
      displayTime = m_frameState.predictedDisplayTime;

    if (!m_running || !displayTime)
      return input;

    XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };

    if (XR_SUCCEEDED(m_xr->locateSpace(m_viewSpace, m_space, XrTime(displayTime), &location))) {
      const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
      input.isHeadTracked = (location.locationFlags & valid) == valid;

      if (input.isHeadTracked) {
        input.headPose = vrPoseFromXr(location.pose);
        m_headPosition = input.headPose.position;
      }
    }

    if (!m_loggedHead) {
      Logger::info(str::format("VR: First OpenXR head pose, tracked: ", input.isHeadTracked ? "yes" : "no"));
      m_loggedHead = true;
    }

    if (m_input)
      m_input->read(m_context, m_space, XrTime(displayTime), input);

    return input;
  }


  void VrOpenXrBackend::setInputContext(VrInputContext context) {
    m_context = context;
  }


  std::array<VrEyeView, VrEyeCount> VrOpenXrBackend::locateViews(int64_t displayTime) {
    if (!m_running || !displayTime)
      return m_views;

    XrViewLocateInfo info = { XR_TYPE_VIEW_LOCATE_INFO };
    info.viewConfigurationType = ViewConfiguration;
    info.displayTime           = XrTime(displayTime);
    info.space                 = m_space;

    XrViewState state = { XR_TYPE_VIEW_STATE };
    std::array<XrView, VrEyeCount> views;
    views.fill({ XR_TYPE_VIEW });
    uint32_t count = 0;

    if (!XR_SUCCEEDED(m_xr->locateViews(m_session, &info, &state, VrEyeCount, &count, views.data()))
     || count != VrEyeCount || !(state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
      return m_views;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      m_views[i].pose = vrPoseFromXr(views[i].pose);
      m_views[i].fov  = vrFovFromXr(views[i].fov);
    }

    return m_views;
  }


  bool VrOpenXrBackend::supportsEyeTracking() {
    std::array<VrGaze, VrEyeCount> gaze;
    return m_eyeGaze && (m_loggedGaze || locateGaze(gaze));
  }


  bool VrOpenXrBackend::locateGaze(std::array<VrGaze, VrEyeCount>& gaze) {
    if (!m_eyeGaze || !m_input || !m_running)
      return false;

    VrPose pose;

    // The actions were synchronised by pollInput, for the same display time
    if (!m_input->readGaze(m_space, m_frameState.predictedDisplayTime, pose))
      return false;

    VrVector3 direction = vrRotate(pose.orientation, { 0.0f, 0.0f, -1.0f });

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      if (!vrGazeFromXrDirection(m_views[i].pose.orientation, direction, gaze[i]))
        return false;
    }

    if (!m_loggedGaze) {
      m_loggedGaze = true;
      Logger::info(str::format("VR: The headset gives the gaze: left eye ", gaze[0].tanX, ", ", gaze[0].tanY,
        ", right eye ", gaze[1].tanX, ", ", gaze[1].tanY, " (tangents right and up)"));
    }

    return true;
  }


  bool VrOpenXrBackend::prepareSwapchain(Swapchain& swapchain, VrExtent extent, VkFormat sourceFormat, const char* what) {
    if (swapchain.handle != XR_NULL_HANDLE && swapchain.extent.width == extent.width
     && swapchain.extent.height == extent.height && swapchain.format == sourceFormat)
      return true;

    destroySwapchain(swapchain);

    VrXrSwapchainFormat format = vrChooseXrSwapchainFormat(m_swapchainFormats, sourceFormat);

    if (format.format == VK_FORMAT_UNDEFINED) {
      if (!m_loggedFormat) {
        Logger::err(str::format("VR: The OpenXR runtime offers no swapchain format for images of format ", uint32_t(sourceFormat)));
        m_loggedFormat = true;
      }

      return false;
    }

    if (!format.srgb && !m_loggedFormat) {
      Logger::warn(str::format("VR: The OpenXR runtime offers no sRGB format for images of format ",
        uint32_t(sourceFormat), ", the image will look too bright"));
      m_loggedFormat = true;
    }

    XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format      = int64_t(format.format);
    info.sampleCount = 1;
    info.width       = extent.width;
    info.height      = extent.height;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;

    if (!check(m_xr->createSwapchain(m_session, &info, &swapchain.handle), "xrCreateSwapchain")) {
      swapchain.handle = XR_NULL_HANDLE;
      return false;
    }

    uint32_t count = 0;
    std::vector<XrSwapchainImageVulkanKHR> images;

    if (check(m_xr->enumerateSwapchainImages(swapchain.handle, 0, &count, nullptr), "xrEnumerateSwapchainImages")) {
      images.resize(count, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });

      if (!check(m_xr->enumerateSwapchainImages(swapchain.handle, count,
            &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())), "xrEnumerateSwapchainImages"))
        count = 0;
    }

    if (!count) {
      destroySwapchain(swapchain);
      return false;
    }

    for (uint32_t i = 0; i < count; i++)
      swapchain.images.push_back(images[i].image);

    swapchain.extent = extent;
    swapchain.format = sourceFormat;
    swapchain.srgb   = format.srgb;

    Logger::info(str::format("VR: OpenXR swapchain for the ", what, ": ", extent.width, "x", extent.height,
      ", format ", uint32_t(format.format), ", ", count, " images"));
    return true;
  }


  void VrOpenXrBackend::destroySwapchain(Swapchain& swapchain) {
    if (swapchain.handle != XR_NULL_HANDLE)
      m_xr->destroySwapchain(swapchain.handle);

    swapchain = Swapchain();
  }


  VkImage VrOpenXrBackend::acquireImage(Swapchain& swapchain) {
    uint32_t index = 0;

    if (!check(m_xr->acquireSwapchainImage(swapchain.handle, nullptr, &index), "xrAcquireSwapchainImage")
     || index >= swapchain.images.size())
      return VK_NULL_HANDLE;

    swapchain.acquired = int32_t(index);

    XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;

    if (!check(m_xr->waitSwapchainImage(swapchain.handle, &wait), "xrWaitSwapchainImage")) {
      releaseImage(swapchain);
      return VK_NULL_HANDLE;
    }

    return swapchain.images[index];
  }


  bool VrOpenXrBackend::releaseImage(Swapchain& swapchain) {
    if (swapchain.acquired < 0)
      return false;

    swapchain.acquired = -1;

    if (!check(m_xr->releaseSwapchainImage(swapchain.handle, nullptr), "xrReleaseSwapchainImage"))
      return false;

    swapchain.hasImage = true;
    return true;
  }


  bool VrOpenXrBackend::endFrame(const XrCompositionLayerProjectionView* eyes) {
    if (!m_frameBegun)
      return false;

    m_frameBegun = false;

    // The eyes, the fade over them, the panels, then the lasers over all
    constexpr uint32_t QuadCount = 1 + VrPanelCount + VrHandCount;

    XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    std::array<XrCompositionLayerQuad, QuadCount> quads;
    std::array<const XrCompositionLayerBaseHeader*, QuadCount + 1> layers = { };
    uint32_t layerCount = 0;
    uint32_t quadCount  = 0;
    uint32_t panelCount = 0;

    if (eyes) {
      projection.space     = m_space;
      projection.viewCount = VrEyeCount;
      projection.views     = eyes;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    }

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);

    if (eyes && prepareFade(quads[quadCount]))
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[quadCount++]);

    // In the order of VrPanelId: the head HUD over the menu, the hand panels over the head HUD
    for (uint32_t i = 0; i < VrPanelCount; i++) {
      const Panel& panel = m_panels[i];

      if (!panel.visible || !panel.swapchain.hasImage)
        continue;

      XrCompositionLayerQuad& quad = quads[quadCount++];
      quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
      quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

      if (!panel.premultiplied)
        quad.layerFlags |= XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

      quad.space         = panel.anchor == VrPanelAnchor::Head ? m_viewSpace : m_space;
      quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
      quad.subImage.swapchain = panel.swapchain.handle;
      quad.subImage.imageRect = { { 0, 0 }, { int32_t(panel.swapchain.extent.width), int32_t(panel.swapchain.extent.height) } };
      quad.pose          = vrPoseToXr(panel.pose);
      quad.size          = vrXrQuadSize(panel.width, panel.swapchain.extent);
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
      panelCount += 1;
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (preparePointer(VrHand(i), quads[quadCount]))
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[quadCount++]);
    }

    XrFrameEndInfo info = { XR_TYPE_FRAME_END_INFO };
    info.displayTime          = m_frameState.predictedDisplayTime;
    info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    info.layerCount           = layerCount;
    info.layers               = layers.data();

    bool ended = check(m_xr->endFrame(m_session, &info), "xrEndFrame");

    if (ended && eyes) {
      m_frameClock.ended     = steadySeconds();
      m_frameClock.submitted = true;
    }

    if (ended && eyes && !m_loggedEndFrame) {
      Logger::info(str::format("VR: First stereo frame shown, ", panelCount, " panels over it"));
      m_loggedEndFrame = true;
    }

    return ended;
  }


  bool VrOpenXrBackend::submitFrame(const VrFrameSubmission& frame) {
    if (!m_frameBegun || !m_copy)
      return false;

    m_frameClock.submitStart = steadySeconds();
    m_frameClock.syncMs      = frame.syncMs;

    for (const VrEyeImage& eye : frame.images) {
      if (eye.image == VK_NULL_HANDLE || eye.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        endFrame(nullptr);
        return false;
      }
    }

    if (!m_loggedSubmit) {
      Logger::info(str::format("VR: Submitting the first frame, eye image ",
        frame.images[0].extent.width, "x", frame.images[0].extent.height,
        ", format ", uint32_t(frame.images[0].format), ", layout ", uint32_t(frame.images[0].layout)));
      m_loggedSubmit = true;
    }

    std::array<VrXrImageCopyRegion, VrEyeCount> regions;
    std::array<XrCompositionLayerProjectionView, VrEyeCount> views;
    bool ready = true;

    for (uint32_t i = 0; i < VrEyeCount && ready; i++) {
      const VrEyeImage& image = frame.images[i];

      ready = prepareSwapchain(m_eyes[i], image.extent, image.format, i ? "right eye" : "left eye");

      if (ready) {
        regions[i].source = image;
        regions[i].target = acquireImage(m_eyes[i]);
        ready = regions[i].target != VK_NULL_HANDLE;
      }

      // The views the eyes were rendered with, so the runtime reprojects from them
      views[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
      views[i].pose = vrPoseToXr(frame.views[i].pose);
      views[i].fov  = vrFovToXr(frame.views[i].fov);
      views[i].subImage.swapchain = m_eyes[i].handle;
      views[i].subImage.imageRect = { { 0, 0 }, { int32_t(image.extent.width), int32_t(image.extent.height) } };
    }

    if (ready)
      ready = m_copy->copy(regions.data(), VrEyeCount);

    // Acquired images go back to the runtime whether they were written or not
    for (Swapchain& eye : m_eyes) {
      if (eye.acquired >= 0 && !releaseImage(eye))
        ready = false;
    }

    m_frameClock.copied = steadySeconds();

    return endFrame(ready ? views.data() : nullptr) && ready;
  }


  void VrOpenXrBackend::submitEmptyFrame(int64_t displayTime) {
    // The panels still need a frame to be shown
    if (m_frameWaited && m_running)
      beginFrame();

    endFrame(nullptr);
  }


  bool VrOpenXrBackend::submitPanel(VrPanelId id, const VrPanelSubmission& submission) {
    uint32_t index = uint32_t(id);

    if (!m_running || !m_copy || index >= VrPanelCount
     || submission.image.image == VK_NULL_HANDLE || submission.image.layout == VK_IMAGE_LAYOUT_UNDEFINED)
      return false;

    double start = steadySeconds();

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    Panel& panel = m_panels[index];

    if (!prepareSwapchain(panel.swapchain, submission.image.extent, submission.image.format, panelName(id)))
      return false;

    VrXrImageCopyRegion region;
    region.source = submission.image;
    region.target = acquireImage(panel.swapchain);

    if (region.target == VK_NULL_HANDLE)
      return false;

    bool copied = m_copy->copy(&region, 1);

    if (!releaseImage(panel.swapchain) || !copied)
      return false;

    panel.pose          = submission.pose;
    panel.width         = submission.width;
    panel.anchor        = submission.anchor;
    panel.premultiplied = submission.premultiplied;
    panel.visible       = true;

    if (!panel.logged) {
      Logger::info(str::format("VR: The ", panelName(id), " panel is shown as an OpenXR quad"));
      panel.logged = true;
    }

    m_frameClock.panelMs += submission.syncMs + (steadySeconds() - start) * 1000.0;
    return true;
  }


  void VrOpenXrBackend::hidePanel(VrPanelId id) {
    uint32_t index = uint32_t(id);

    if (index >= VrPanelCount)
      return;

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    m_panels[index].visible = false;
  }


  bool VrOpenXrBackend::panelsNeedFrames() const {
    // Quad layers are only given to the runtime with xrEndFrame
    return true;
  }


  bool VrOpenXrBackend::submitPanelPixels(VrPanelId id, const VrPanelPixels& pixels) {
    uint32_t index = uint32_t(id);

    if (!m_running || !m_copy || index >= VrPanelCount || !pixels.rgba || !pixels.width || !pixels.height)
      return false;

    if (m_binding.lockQueue)
      m_binding.lockQueue();

    bool shown = false;

    { std::lock_guard<dxvk::mutex> lock(m_panelMutex);
      Panel& panel = m_panels[index];
      VrExtent extent = { pixels.width, pixels.height };

      if (prepareSwapchain(panel.swapchain, extent, VK_FORMAT_R8G8B8A8_UNORM, panelName(id))) {
        VkImage target = acquireImage(panel.swapchain);

        if (target != VK_NULL_HANDLE) {
          bool uploaded = m_copy->upload(target, extent, pixels.rgba);
          shown = releaseImage(panel.swapchain) && uploaded;
        }
      }

      if (shown) {
        panel.pose          = pixels.pose;
        panel.width         = pixels.metres;
        panel.anchor        = VrPanelAnchor::Room;
        panel.premultiplied = false;
        panel.visible       = true;

        if (!panel.logged) {
          Logger::info(str::format("VR: The ", panelName(id), " panel is shown as an OpenXR quad, from system memory"));
          panel.logged = true;
        }
      }
    }

    if (m_binding.releaseQueue)
      m_binding.releaseQueue();

    return shown;
  }


  bool VrOpenXrBackend::fillSwapchain(Swapchain& swapchain, const std::array<float, 4>& rgba, const char* what) {
    if (!prepareSwapchain(swapchain, SolidImageExtent, VK_FORMAT_R8G8B8A8_UNORM, what))
      return false;

    // An sRGB image is filled with linear values, which the GPU encodes
    VkClearColorValue color = { };

    for (uint32_t i = 0; i < 3; i++)
      color.float32[i] = swapchain.srgb ? vrXrLinearFromSrgb(rgba[i]) : rgba[i];

    color.float32[3] = rgba[3];

    VkImage target = acquireImage(swapchain);

    if (target == VK_NULL_HANDLE)
      return false;

    bool filled = m_copy->fill(target, color);
    return releaseImage(swapchain) && filled;
  }


  bool VrOpenXrBackend::prepareFade(XrCompositionLayerQuad& quad) {
    float opacity = m_fade.opacity(steadySeconds());

    if (opacity <= 0.0f || !m_copy)
      return false;

    if (std::abs(opacity - m_fadeFilled) > FadeStep || !m_fadeImage.hasImage) {
      if (!fillSwapchain(m_fadeImage, { 0.0f, 0.0f, 0.0f, opacity }, "fade"))
        return false;

      m_fadeFilled = opacity;
    }

    quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    quad.layerFlags    = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    quad.space         = m_viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = m_fadeImage.handle;
    quad.subImage.imageRect = { { 0, 0 }, { int32_t(SolidImageExtent.width), int32_t(SolidImageExtent.height) } };
    quad.pose          = vrPoseToXr(VrPose());
    quad.pose.position = { 0.0f, 0.0f, -FadeDistance };
    quad.size          = { FadeSize, FadeSize };
    return true;
  }


  bool VrOpenXrBackend::preparePointer(VrHand hand, XrCompositionLayerQuad& quad) {
    const Pointer& pointer = m_pointers[uint32_t(hand)];

    if (!pointer.visible || !m_copy)
      return false;

    Swapchain& image = m_pointerImages[pointer.active ? 1 : 0];

    if (!image.hasImage) {
      float alpha = pointer.active ? PointerActiveAlpha : PointerInactiveAlpha;

      if (!fillSwapchain(image, { PointerColor[0], PointerColor[1], PointerColor[2], alpha },
            pointer.active ? "active laser" : "laser"))
        return false;
    }

    quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    quad.layerFlags    = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    quad.space         = m_space;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = image.handle;
    quad.subImage.imageRect = { { 0, 0 }, { int32_t(SolidImageExtent.width), int32_t(SolidImageExtent.height) } };
    quad.pose          = vrPoseToXr(pointer.pose);
    quad.size          = { PointerWidth, pointer.length };
    return true;
  }


  void VrOpenXrBackend::showPointer(VrHand hand, const VrPointerSubmission& submission) {
    if (!m_running || submission.length <= 0.0f)
      return;

    VrPose pose = vrXrBeamPose(submission.origin, submission.length, m_headPosition);

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    Pointer& pointer = m_pointers[uint32_t(hand)];
    pointer.pose    = pose;
    pointer.length  = submission.length;
    pointer.active  = submission.active;
    pointer.visible = true;
  }


  void VrOpenXrBackend::hidePointer(VrHand hand) {
    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    m_pointers[uint32_t(hand)].visible = false;
  }


  void VrOpenXrBackend::applyHaptic(VrHand hand, float amplitude, int64_t durationNs) {
    if (m_running && m_input)
      m_input->vibrate(hand, amplitude, durationNs);
  }


  void VrOpenXrBackend::fadeView(float seconds, float opacity) {
    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    m_fade.start(steadySeconds(), seconds, opacity);
  }


  bool VrOpenXrBackend::showKeyboard(const VrKeyboardRequest& request) {
    // OpenXR has no text keyboard (only Meta's runtime has an extension for one)
    return false;
  }


  void VrOpenXrBackend::hideKeyboard() {

  }


  VrKeyboardEvent VrOpenXrBackend::takeKeyboardEvent(std::string& typed) {
    return VrKeyboardEvent::None;
  }


  void VrOpenXrBackend::measureFrame(double waitStart, double waitEnd) {
    const FrameClock& frame = m_frameClock;
    double periodMs = double(m_frameState.predictedDisplayPeriod) * 1e-6;

    auto ms = [] (double from, double to) {
      return (to - from) * 1000.0;
    };

    if (frame.submitted && m_statsRunning) {
      VrFrameCycle cycle = { };
      cycle[uint32_t(VrFrameStep::Interval)]   = ms(frame.waitEnd,     waitEnd);
      cycle[uint32_t(VrFrameStep::Render)]     = ms(frame.waitEnd,     frame.submitStart) - frame.syncMs - frame.panelMs;
      cycle[uint32_t(VrFrameStep::Sync)]       = frame.syncMs;
      cycle[uint32_t(VrFrameStep::Transition)] = 0.0;
      cycle[uint32_t(VrFrameStep::Submit)]     = ms(frame.submitStart, frame.copied);
      cycle[uint32_t(VrFrameStep::HandOff)]    = ms(frame.copied,      frame.ended);
      cycle[uint32_t(VrFrameStep::After)]      = ms(frame.ended,       waitStart);
      cycle[uint32_t(VrFrameStep::Panels)]     = frame.panelMs;
      cycle[uint32_t(VrFrameStep::Wait)]       = ms(waitStart,         waitEnd);

      m_hasLastCycle = true;
      m_lastCycle    = cycle;
      m_lastCycleEnd = wallClockSeconds();

      if (m_stats.add(cycle))
        logFrameStats();
    } else if (frame.submitted && periodMs > 0.0) {
      m_stats        = VrFrameStats(std::max(1u, uint32_t(StatsPeriodSeconds * 1000.0 / periodMs)), periodMs);
      m_statsRunning = true;
    } else {
      m_statsRunning = false;
    }

    m_frameClock = FrameClock();
    m_frameClock.waitStart = waitStart;
    m_frameClock.waitEnd   = waitEnd;
  }


  void VrOpenXrBackend::logFrameStats() {
    auto fixed = [] (double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    };

    auto step = [&] (VrFrameStep step) {
      return fixed(m_stats.average(step)) + "/" + fixed(m_stats.step(step).max);
    };

    double seconds = m_stats.seconds();

    Logger::info(str::format("VR: ", m_stats.frames(), " frames in ", fixed(seconds),
      " s, ", seconds > 0.0 ? std::lround(double(m_stats.frames()) / seconds) : 0, " fps, ",
      m_stats.lateFrames(), " late by a display period or more, ", m_stats.busyFrames(),
      " busy for longer than a period. Average/longest ms: frame ", step(VrFrameStep::Interval),
      ", eye rendering ", step(VrFrameStep::Render), ", panels ", step(VrFrameStep::Panels),
      ", DXVK sync ", step(VrFrameStep::Sync), ", copy into the swapchains ", step(VrFrameStep::Submit),
      ", xrEndFrame ", step(VrFrameStep::HandOff), ", after submission ", step(VrFrameStep::After),
      ", waiting for the runtime ", step(VrFrameStep::Wait)));

    m_stats.reset();
  }

}
