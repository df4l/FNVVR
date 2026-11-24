#include <algorithm>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_openxr_vulkan_device.h"

namespace dxvk {

  std::unique_ptr<VrXrVulkanDevice> VrXrVulkanDevice::create(IVRBackend& backend) {
    std::unique_ptr<VrXrVulkanDevice> device(new VrXrVulkanDevice());

    if (!device->initialize(backend)) {
      Logger::err("VR: Could not create a Vulkan device for the OpenXR panel session");
      return nullptr;
    }

    return device;
  }


  VrXrVulkanDevice::~VrXrVulkanDevice() {
    if (m_binding.device) {
      m_deviceWaitIdle(m_binding.device);
      m_destroyDevice(m_binding.device, nullptr);
    }

    if (m_binding.instance)
      m_destroyInstance(m_binding.instance, nullptr);
  }


  bool VrXrVulkanDevice::initialize(IVRBackend& backend) {
    m_library = new vk::LibraryLoader();

    if (!m_library->getLoaderProc())
      return false;

    m_binding.getInstanceProcAddr = m_library->getLoaderProc();
    return createInstance(backend) && createDevice(backend);
  }


  bool VrXrVulkanDevice::createInstance(IVRBackend& backend) {
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(m_library->sym("vkCreateInstance"));

    if (!createInstance)
      return false;

    VrVulkanRequirements requirements = backend.queryVulkanRequirements();

    std::vector<const char*> extensions;

    for (const std::string& name : requirements.instanceExtensions)
      extensions.push_back(name.c_str());

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Fallout New Vegas Launcher";
    appInfo.pEngineName      = "FNVVR";
    appInfo.apiVersion       = VK_API_VERSION_1_1;

    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo        = &appInfo;
    info.enabledExtensionCount   = uint32_t(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    VkResult result = createInstance(&info, nullptr, &m_binding.instance);

    if (result != VK_SUCCESS) {
      Logger::err(str::format("VR: vkCreateInstance failed for the OpenXR panel session: ", result));
      m_binding.instance = VK_NULL_HANDLE;
      return false;
    }

    m_destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
      m_binding.getInstanceProcAddr(m_binding.instance, "vkDestroyInstance"));
    return m_destroyInstance != nullptr;
  }


  bool VrXrVulkanDevice::createDevice(IVRBackend& backend) {
    auto proc = [this] (const char* name) {
      return m_binding.getInstanceProcAddr(m_binding.instance, name);
    };

    auto getQueueFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
      proc("vkGetPhysicalDeviceQueueFamilyProperties"));
    auto createDevice   = reinterpret_cast<PFN_vkCreateDevice>(proc("vkCreateDevice"));
    auto getDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(proc("vkGetDeviceQueue"));
    m_destroyDevice     = reinterpret_cast<PFN_vkDestroyDevice>(proc("vkDestroyDevice"));
    m_deviceWaitIdle    = reinterpret_cast<PFN_vkDeviceWaitIdle>(proc("vkDeviceWaitIdle"));

    if (!getQueueFamilies || !createDevice || !getDeviceQueue || !m_destroyDevice || !m_deviceWaitIdle)
      return false;

    m_binding.physicalDevice = backend.selectPhysicalDevice(m_binding.instance);

    if (!m_binding.physicalDevice) {
      Logger::err("VR: The OpenXR runtime named no GPU for the panel session");
      return false;
    }

    uint32_t familyCount = 0;
    getQueueFamilies(m_binding.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    getQueueFamilies(m_binding.physicalDevice, &familyCount, families.data());

    // A graphics queue, which runtimes expect, and which can copy
    auto family = std::find_if(families.begin(), families.end(),
      [] (const VkQueueFamilyProperties& f) { return (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) && f.queueCount; });

    if (family == families.end())
      return false;

    m_binding.queueFamilyIndex = uint32_t(family - families.begin());
    m_binding.queueIndex       = 0;

    std::vector<std::string> names = backend.queryDeviceExtensions(m_binding.physicalDevice);
    std::vector<const char*> extensions;

    for (const std::string& name : names)
      extensions.push_back(name.c_str());

    const float priority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_binding.queueFamilyIndex;
    queueInfo.queueCount       = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.queueCreateInfoCount    = 1;
    info.pQueueCreateInfos       = &queueInfo;
    info.enabledExtensionCount   = uint32_t(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    VkResult result = createDevice(m_binding.physicalDevice, &info, nullptr, &m_binding.device);

    if (result != VK_SUCCESS) {
      Logger::err(str::format("VR: vkCreateDevice failed for the OpenXR panel session: ", result));
      m_binding.device = VK_NULL_HANDLE;
      return false;
    }

    getDeviceQueue(m_binding.device, m_binding.queueFamilyIndex, m_binding.queueIndex, &m_binding.queue);
    return m_binding.queue != VK_NULL_HANDLE;
  }

}
