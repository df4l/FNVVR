#include <windows.h>

#include "../../util/log/log.h"

#include "vr_eye_transition.h"

namespace dxvk {

  namespace {

    // Longest wait for the GPU to finish a layout transition, in nanoseconds
    constexpr uint64_t WaitTimeoutNs = 1000000000;

  }


  struct VrEyeTransition::Functions {
    PFN_vkCreateCommandPool     createCommandPool     = nullptr;
    PFN_vkDestroyCommandPool    destroyCommandPool    = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer    resetCommandBuffer    = nullptr;
    PFN_vkBeginCommandBuffer    beginCommandBuffer    = nullptr;
    PFN_vkEndCommandBuffer      endCommandBuffer      = nullptr;
    PFN_vkCmdPipelineBarrier    cmdPipelineBarrier    = nullptr;
    PFN_vkCreateFence           createFence           = nullptr;
    PFN_vkDestroyFence          destroyFence          = nullptr;
    PFN_vkWaitForFences         waitForFences         = nullptr;
    PFN_vkResetFences           resetFences           = nullptr;
    PFN_vkQueueSubmit           queueSubmit           = nullptr;

    bool load(const VrGraphicsBinding& binding) {
      HMODULE library = GetModuleHandleA("vulkan-1.dll");

      if (!library)
        return false;

      auto getInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(library, "vkGetInstanceProcAddr"));

      if (!getInstanceProc)
        return false;

      auto getDeviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        getInstanceProc(binding.instance, "vkGetDeviceProcAddr"));

      if (!getDeviceProc)
        return false;

      auto load = [&] (auto& target, const char* name) {
        target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(getDeviceProc(binding.device, name));
      };

      load(createCommandPool, "vkCreateCommandPool");
      load(destroyCommandPool, "vkDestroyCommandPool");
      load(allocateCommandBuffers, "vkAllocateCommandBuffers");
      load(resetCommandBuffer, "vkResetCommandBuffer");
      load(beginCommandBuffer, "vkBeginCommandBuffer");
      load(endCommandBuffer, "vkEndCommandBuffer");
      load(cmdPipelineBarrier, "vkCmdPipelineBarrier");
      load(createFence, "vkCreateFence");
      load(destroyFence, "vkDestroyFence");
      load(waitForFences, "vkWaitForFences");
      load(resetFences, "vkResetFences");
      load(queueSubmit, "vkQueueSubmit");

      return createCommandPool && destroyCommandPool && allocateCommandBuffers
          && resetCommandBuffer && beginCommandBuffer && endCommandBuffer
          && cmdPipelineBarrier && createFence && destroyFence
          && waitForFences && resetFences && queueSubmit;
    }
  };


  VrEyeTransition::VrEyeTransition(const VrGraphicsBinding& binding)
  : m_binding(binding), m_vk(std::make_unique<Functions>()) { }


  VrEyeTransition::~VrEyeTransition() {
    if (m_pool) {
      // Destroying the pool frees its command buffers
      m_vk->waitForFences(m_binding.device, 1, &m_restore.fence, VK_TRUE, WaitTimeoutNs);
      m_vk->destroyCommandPool(m_binding.device, m_pool, nullptr);
    }

    if (m_toSource.fence)
      m_vk->destroyFence(m_binding.device, m_toSource.fence, nullptr);

    if (m_restore.fence)
      m_vk->destroyFence(m_binding.device, m_restore.fence, nullptr);
  }


  std::unique_ptr<VrEyeTransition> VrEyeTransition::create(const VrGraphicsBinding& binding) {
    if (!binding.queue || !binding.device || !binding.instance)
      return nullptr;

    std::unique_ptr<VrEyeTransition> transition(new VrEyeTransition(binding));

    if (!transition->initialize())
      return nullptr;

    return transition;
  }


  bool VrEyeTransition::initialize() {
    if (!m_vk->load(m_binding)) {
      Logger::err("VR: Could not load the Vulkan functions for the eye image transitions");
      return false;
    }

    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_binding.queueFamilyIndex;

    if (m_vk->createCommandPool(m_binding.device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
      m_pool = VK_NULL_HANDLE;
      return false;
    }

    VkCommandBuffer buffers[2] = { };

    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool        = m_pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 2;

    if (m_vk->allocateCommandBuffers(m_binding.device, &allocInfo, buffers) != VK_SUCCESS)
      return false;

    m_toSource.commandBuffer = buffers[0];
    m_restore.commandBuffer  = buffers[1];

    // Both fences start signaled, so that the first wait on them returns
    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return m_vk->createFence(m_binding.device, &fenceInfo, nullptr, &m_toSource.fence) == VK_SUCCESS
        && m_vk->createFence(m_binding.device, &fenceInfo, nullptr, &m_restore.fence) == VK_SUCCESS;
  }


  bool VrEyeTransition::toTransferSource(const std::array<VrEyeImage, VrEyeCount>& images) {
    return submit(images, m_toSource, true);
  }


  bool VrEyeTransition::restore(const std::array<VrEyeImage, VrEyeCount>& images) {
    return submit(images, m_restore, false);
  }


  bool VrEyeTransition::submit(const std::array<VrEyeImage, VrEyeCount>& images, Slot& slot, bool toSource) {
    // The previous use of this slot must be done before its buffer is reset
    if (m_vk->waitForFences(m_binding.device, 1, &slot.fence, VK_TRUE, WaitTimeoutNs) != VK_SUCCESS)
      return false;

    m_vk->resetFences(m_binding.device, 1, &slot.fence);
    m_vk->resetCommandBuffer(slot.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk->beginCommandBuffer(slot.commandBuffer, &beginInfo);

    std::array<VkImageMemoryBarrier, VrEyeCount> barriers = { };
    uint32_t barrierCount = 0;

    for (const VrEyeImage& eye : images) {
      if (eye.layout == VK_IMAGE_LAYOUT_UNDEFINED || eye.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        continue;

      VkImageMemoryBarrier& barrier = barriers[barrierCount++];
      barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.srcAccessMask       = toSource ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask       = toSource ? VK_ACCESS_TRANSFER_READ_BIT
                                             : (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
      barrier.oldLayout           = toSource ? eye.layout : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout           = toSource ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : eye.layout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = eye.image;
      barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eye.arrayLayer, 1 };
    }

    if (barrierCount) {
      VkPipelineStageFlags other = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

      m_vk->cmdPipelineBarrier(slot.commandBuffer,
        toSource ? other : VK_PIPELINE_STAGE_TRANSFER_BIT,
        toSource ? VK_PIPELINE_STAGE_TRANSFER_BIT : other,
        0, 0, nullptr, 0, nullptr, barrierCount, barriers.data());
    }

    m_vk->endCommandBuffer(slot.commandBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &slot.commandBuffer;

    if (m_vk->queueSubmit(m_binding.queue, 1, &submitInfo, slot.fence) != VK_SUCCESS) {
      Logger::err("VR: Could not submit the eye image transition");
      return false;
    }

    if (toSource)
      return m_vk->waitForFences(m_binding.device, 1, &slot.fence, VK_TRUE, WaitTimeoutNs) == VK_SUCCESS;

    return true;
  }

}
