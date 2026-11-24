#include <algorithm>
#include <cstring>

#include <vulkan/vulkan_core.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_openxr_backend.h"
#include "vr_openxr_convert.h"
#include "vr_openxr_image_copy.h"
#include "vr_openxr_loader.h"

namespace dxvk {

  namespace {

    constexpr XrViewConfigurationType ViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

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

    Logger::err(str::format("VR: OpenXR ", what, " failed: ", resultString(result)));
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

    XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };

    if (check(m_xr->getSystemProperties(m_instance, m_system, &properties), "xrGetSystemProperties")) {
      Logger::info(str::format("VR: OpenXR headset: ", properties.systemName,
        ", vendor ", properties.vendorId,
        ", max swapchain ", properties.graphicsProperties.maxSwapchainImageWidth,
        "x", properties.graphicsProperties.maxSwapchainImageHeight,
        ", max layers ", properties.graphicsProperties.maxLayerCount,
        ", position tracking ", properties.trackingProperties.positionTracking ? "yes" : "no"));
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
    // Only the launcher uses it, and it shows its panel through OpenVR only
    return false;
  }


  void VrOpenXrBackend::endSession() {
    // Destroying the session ends it, whatever state it is in. No frame is
    // closed: the device and its queue may be gone already.
    destroySession();
  }


  void VrOpenXrBackend::destroySession() {
    // The copies into the swapchains must be done first: the helper waits for them
    m_copy = nullptr;

    { std::lock_guard<dxvk::mutex> lock(m_panelMutex);

      for (Panel& panel : m_panels) {
        destroySwapchain(panel.swapchain);
        panel.visible = false;
      }
    }

    for (Swapchain& eye : m_eyes)
      destroySwapchain(eye);

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
    m_frameWaited = false;
    m_frameBegun  = false;
    m_xrState     = XR_SESSION_STATE_UNKNOWN;
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

      if (input.isHeadTracked)
        input.headPose = vrPoseFromXr(location.pose);
    }

    if (!m_loggedHead) {
      Logger::info(str::format("VR: First OpenXR head pose, tracked: ", input.isHeadTracked ? "yes" : "no"));
      m_loggedHead = true;
    }

    return input;
  }


  void VrOpenXrBackend::setInputContext(VrInputContext context) {

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
    return false;
  }


  bool VrOpenXrBackend::locateGaze(std::array<VrGaze, VrEyeCount>& gaze) {
    return false;
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

    XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    std::array<XrCompositionLayerQuad, VrPanelCount> quads;
    std::array<const XrCompositionLayerBaseHeader*, VrPanelCount + 1> layers = { };
    uint32_t layerCount = 0;

    if (eyes) {
      projection.space     = m_space;
      projection.viewCount = VrEyeCount;
      projection.views     = eyes;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    }

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);

    // In the order of VrPanelId: the head HUD over the menu, the hand panels over the head HUD
    for (uint32_t i = 0; i < VrPanelCount; i++) {
      const Panel& panel = m_panels[i];

      if (!panel.visible || !panel.swapchain.hasImage)
        continue;

      XrCompositionLayerQuad& quad = quads[i];
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
    }

    XrFrameEndInfo info = { XR_TYPE_FRAME_END_INFO };
    info.displayTime          = m_frameState.predictedDisplayTime;
    info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    info.layerCount           = layerCount;
    info.layers               = layers.data();

    bool ended = check(m_xr->endFrame(m_session, &info), "xrEndFrame");

    if (ended && eyes && !m_loggedEndFrame) {
      Logger::info(str::format("VR: First stereo frame shown, ", layerCount - 1, " panels over it"));
      m_loggedEndFrame = true;
    }

    return ended;
  }


  bool VrOpenXrBackend::submitFrame(const VrFrameSubmission& frame) {
    if (!m_frameBegun || !m_copy)
      return false;

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

    return true;
  }


  void VrOpenXrBackend::hidePanel(VrPanelId id) {
    uint32_t index = uint32_t(id);

    if (index >= VrPanelCount)
      return;

    std::lock_guard<dxvk::mutex> lock(m_panelMutex);
    m_panels[index].visible = false;
  }


  bool VrOpenXrBackend::submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) {
    return false;
  }


  void VrOpenXrBackend::showPointer(VrHand hand, const VrPointerSubmission& pointer) {

  }


  void VrOpenXrBackend::hidePointer(VrHand hand) {

  }


  void VrOpenXrBackend::applyHaptic(VrHand hand, float amplitude, int64_t durationNs) {

  }


  void VrOpenXrBackend::fadeView(float seconds, float opacity) {

  }


  bool VrOpenXrBackend::showKeyboard(const VrKeyboardRequest& request) {
    return false;
  }


  void VrOpenXrBackend::hideKeyboard() {

  }


  VrKeyboardEvent VrOpenXrBackend::takeKeyboardEvent(std::string& typed) {
    return VrKeyboardEvent::None;
  }

}
