#include <algorithm>
#include <cstring>
#include <vector>

#include <vulkan/vulkan_core.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_openxr_loader.h"
#include "vr_openxr_probe.h"

namespace dxvk {

  namespace {

    std::string xrVersionString(XrVersion version) {
      return str::format(XR_VERSION_MAJOR(version), ".", XR_VERSION_MINOR(version), ".", XR_VERSION_PATCH(version));
    }


    /**
     * \brief Runs the probe on a loaded loader
     *
     * Holds the instance and destroys it on every return path.
     */
    class OpenXrProbe {

    public:

      explicit OpenXrProbe(const VrOpenXrLoader& loader)
      : m_loader(loader) { }

      ~OpenXrProbe() {
        if (m_instance != XR_NULL_HANDLE && m_destroyInstance)
          m_destroyInstance(m_instance);
      }

      bool run() {
        std::vector<XrExtensionProperties> extensions = enumerateExtensions();

        if (extensions.empty())
          return false;

        bool vulkanEnable  = hasExtension(extensions, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
        bool vulkanEnable2 = hasExtension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);

        Logger::info(str::format("VR: OpenXR ", XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, ": ", vulkanEnable ? "yes" : "no",
          ", ", XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, ": ", vulkanEnable2 ? "yes" : "no"));

        if (!createInstance(vulkanEnable))
          return false;

        logInstance();

        if (!getSystem())
          return false;

        logSystem();
        logViews();

        if (vulkanEnable)
          logVulkanRequirements();

        return true;
      }

    private:

      const VrOpenXrLoader&       m_loader;
      XrInstance                  m_instance        = XR_NULL_HANDLE;
      XrSystemId                  m_system          = XR_NULL_SYSTEM_ID;
      PFN_xrDestroyInstance       m_destroyInstance = nullptr;

      template<typename Fn>
      Fn get(const char* name) const {
        Fn function = m_loader.get<Fn>(m_instance, name);

        if (!function)
          Logger::err(str::format("VR: OpenXR function ", name, " is not available"));

        return function;
      }

      std::string resultString(XrResult result) const {
        auto toString = m_instance != XR_NULL_HANDLE
          ? m_loader.get<PFN_xrResultToString>(m_instance, "xrResultToString")
          : nullptr;

        char buffer[XR_MAX_RESULT_STRING_SIZE] = { };

        if (toString && XR_SUCCEEDED(toString(m_instance, result, buffer)))
          return str::format(buffer, " (", int32_t(result), ")");

        return str::format(int32_t(result));
      }

      bool check(XrResult result, const char* what) const {
        if (XR_SUCCEEDED(result))
          return true;

        Logger::err(str::format("VR: OpenXR ", what, " failed: ", resultString(result)));
        return false;
      }

      static bool hasExtension(const std::vector<XrExtensionProperties>& extensions, const char* name) {
        return std::any_of(extensions.begin(), extensions.end(),
          [name] (const XrExtensionProperties& e) { return !std::strcmp(e.extensionName, name); });
      }

      std::vector<XrExtensionProperties> enumerateExtensions() const {
        auto enumerate = get<PFN_xrEnumerateInstanceExtensionProperties>("xrEnumerateInstanceExtensionProperties");

        if (!enumerate)
          return { };

        uint32_t count = 0;

        if (!check(enumerate(nullptr, 0, &count, nullptr), "xrEnumerateInstanceExtensionProperties"))
          return { };

        std::vector<XrExtensionProperties> extensions(count, { XR_TYPE_EXTENSION_PROPERTIES });

        if (!check(enumerate(nullptr, count, &count, extensions.data()), "xrEnumerateInstanceExtensionProperties"))
          return { };

        extensions.resize(count);

        Logger::info(str::format("VR: OpenXR runtime offers ", count, " instance extensions:"));

        for (const auto& e : extensions)
          Logger::info(str::format("VR:   ", e.extensionName, " v", e.extensionVersion));

        return extensions;
      }

      bool createInstance(bool vulkanEnable) {
        auto create = get<PFN_xrCreateInstance>("xrCreateInstance");

        if (!create)
          return false;

        std::vector<const char*> enabled;

        if (vulkanEnable)
          enabled.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);

        XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
        std::strncpy(info.applicationInfo.applicationName, "Fallout New Vegas", XR_MAX_APPLICATION_NAME_SIZE - 1);
        std::strncpy(info.applicationInfo.engineName, "FNVVR", XR_MAX_ENGINE_NAME_SIZE - 1);
        info.applicationInfo.applicationVersion = 1;
        info.applicationInfo.engineVersion      = 1;
        info.applicationInfo.apiVersion         = XR_API_VERSION_1_0;
        info.enabledExtensionCount = uint32_t(enabled.size());
        info.enabledExtensionNames = enabled.data();

        if (!check(create(&info, &m_instance), "xrCreateInstance"))
          return false;

        m_destroyInstance = get<PFN_xrDestroyInstance>("xrDestroyInstance");
        return true;
      }

      void logInstance() const {
        auto getProperties = get<PFN_xrGetInstanceProperties>("xrGetInstanceProperties");
        XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };

        if (getProperties && check(getProperties(m_instance, &properties), "xrGetInstanceProperties")) {
          Logger::info(str::format("VR: OpenXR runtime: ", properties.runtimeName,
            " ", xrVersionString(properties.runtimeVersion)));
        }
      }

      bool getSystem() {
        auto getSystem = get<PFN_xrGetSystem>("xrGetSystem");

        if (!getSystem)
          return false;

        XrSystemGetInfo info = { XR_TYPE_SYSTEM_GET_INFO };
        info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        // XR_ERROR_FORM_FACTOR_UNAVAILABLE means the headset is not connected yet
        return check(getSystem(m_instance, &info, &m_system), "xrGetSystem (headset)");
      }

      void logSystem() const {
        auto getProperties = get<PFN_xrGetSystemProperties>("xrGetSystemProperties");
        XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };

        if (!getProperties || !check(getProperties(m_instance, m_system, &properties), "xrGetSystemProperties"))
          return;

        Logger::info(str::format("VR: OpenXR headset: ", properties.systemName,
          ", vendor ", properties.vendorId,
          ", max swapchain ", properties.graphicsProperties.maxSwapchainImageWidth,
          "x", properties.graphicsProperties.maxSwapchainImageHeight,
          ", max layers ", properties.graphicsProperties.maxLayerCount,
          ", orientation tracking ", properties.trackingProperties.orientationTracking ? "yes" : "no",
          ", position tracking ", properties.trackingProperties.positionTracking ? "yes" : "no"));
      }

      void logViews() const {
        auto enumerateViews = get<PFN_xrEnumerateViewConfigurationViews>("xrEnumerateViewConfigurationViews");
        auto enumerateBlend = get<PFN_xrEnumerateEnvironmentBlendModes>("xrEnumerateEnvironmentBlendModes");
        const XrViewConfigurationType type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

        if (enumerateViews) {
          uint32_t count = 0;

          if (check(enumerateViews(m_instance, m_system, type, 0, &count, nullptr), "xrEnumerateViewConfigurationViews")) {
            std::vector<XrViewConfigurationView> views(count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });

            if (check(enumerateViews(m_instance, m_system, type, count, &count, views.data()), "xrEnumerateViewConfigurationViews")) {
              for (uint32_t i = 0; i < count; i++) {
                Logger::info(str::format("VR: OpenXR view ", i,
                  ": recommended ", views[i].recommendedImageRectWidth, "x", views[i].recommendedImageRectHeight,
                  ", max ", views[i].maxImageRectWidth, "x", views[i].maxImageRectHeight,
                  ", samples ", views[i].recommendedSwapchainSampleCount));
              }
            }
          }
        }

        if (enumerateBlend) {
          uint32_t count = 0;

          if (check(enumerateBlend(m_instance, m_system, type, 0, &count, nullptr), "xrEnumerateEnvironmentBlendModes")) {
            std::vector<XrEnvironmentBlendMode> modes(count);

            if (check(enumerateBlend(m_instance, m_system, type, count, &count, modes.data()), "xrEnumerateEnvironmentBlendModes")) {
              for (uint32_t i = 0; i < count; i++)
                Logger::info(str::format("VR: OpenXR blend mode: ", uint32_t(modes[i])));
            }
          }
        }
      }

      // The instance and device extension queries share their signature
      std::string vulkanExtensions(PFN_xrGetVulkanInstanceExtensionsKHR query, const char* what) const {
        uint32_t length = 0;

        if (!check(query(m_instance, m_system, 0, &length, nullptr), what))
          return std::string();

        std::vector<char> buffer(length + 1, '\0');

        if (!check(query(m_instance, m_system, length, &length, buffer.data()), what))
          return std::string();

        return std::string(buffer.data());
      }

      void logVulkanRequirements() const {
        auto requirements = get<PFN_xrGetVulkanGraphicsRequirementsKHR>("xrGetVulkanGraphicsRequirementsKHR");
        auto instanceExtensions = get<PFN_xrGetVulkanInstanceExtensionsKHR>("xrGetVulkanInstanceExtensionsKHR");
        auto deviceExtensions = get<PFN_xrGetVulkanDeviceExtensionsKHR>("xrGetVulkanDeviceExtensionsKHR");

        if (requirements) {
          XrGraphicsRequirementsVulkanKHR graphics = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };

          if (check(requirements(m_instance, m_system, &graphics), "xrGetVulkanGraphicsRequirementsKHR")) {
            // XR_KHR_vulkan_enable reports Vulkan versions in OpenXR's encoding
            Logger::info(str::format("VR: OpenXR Vulkan API versions: min ", xrVersionString(graphics.minApiVersionSupported),
              ", max ", xrVersionString(graphics.maxApiVersionSupported)));
          }
        }

        if (instanceExtensions) {
          Logger::info(str::format("VR: OpenXR Vulkan instance extensions: ",
            vulkanExtensions(instanceExtensions, "xrGetVulkanInstanceExtensionsKHR")));
        }

        if (deviceExtensions) {
          Logger::info(str::format("VR: OpenXR Vulkan device extensions: ",
            vulkanExtensions(deviceExtensions, "xrGetVulkanDeviceExtensionsKHR")));
        }
      }

    };

  }


  bool vrProbeOpenXr() {
    VrOpenXrLoader loader;

    if (!loader.isLoaded())
      return false;

    Logger::info(str::format("VR: Loaded ", str::fromws(loader.path().c_str())));

    bool success = OpenXrProbe(loader).run();
    Logger::info(str::format("VR: OpenXR probe ", success ? "succeeded" : "failed"));
    return success;
  }

}
