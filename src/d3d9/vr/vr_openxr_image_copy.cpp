#include <type_traits>

#include "../../util/log/log.h"

#include "vr_openxr_image_copy.h"

namespace dxvk {

  namespace {

    // Longest wait for the GPU to finish the earlier use of a slot, in nanoseconds
    constexpr uint64_t WaitTimeoutNs = 1000000000;

  }


  struct VrXrImageCopy::Functions {
    PFN_vkCreateCommandPool      createCommandPool      = nullptr;
    PFN_vkDestroyCommandPool     destroyCommandPool     = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer     resetCommandBuffer     = nullptr;
    PFN_vkBeginCommandBuffer     beginCommandBuffer     = nullptr;
    PFN_vkEndCommandBuffer       endCommandBuffer       = nullptr;
    PFN_vkCmdPipelineBarrier     cmdPipelineBarrier     = nullptr;
    PFN_vkCmdCopyImage           cmdCopyImage           = nullptr;
    PFN_vkCreateFence            createFence            = nullptr;
    PFN_vkDestroyFence           destroyFence           = nullptr;
    PFN_vkWaitForFences          waitForFences          = nullptr;
    PFN_vkResetFences            resetFences            = nullptr;
    PFN_vkQueueSubmit            queueSubmit            = nullptr;

    bool load(const VrGraphicsBinding& binding) {
      if (!binding.getInstanceProcAddr)
        return false;

      auto getDeviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        binding.getInstanceProcAddr(binding.instance, "vkGetDeviceProcAddr"));

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
      load(cmdCopyImage, "vkCmdCopyImage");
      load(createFence, "vkCreateFence");
      load(destroyFence, "vkDestroyFence");
      load(waitForFences, "vkWaitForFences");
      load(resetFences, "vkResetFences");
      load(queueSubmit, "vkQueueSubmit");

      return createCommandPool && destroyCommandPool && allocateCommandBuffers
          && resetCommandBuffer && beginCommandBuffer && endCommandBuffer
          && cmdPipelineBarrier && cmdCopyImage && createFence && destroyFence
          && waitForFences && resetFences && queueSubmit;
    }
  };


  VrXrImageCopy::VrXrImageCopy(const VrGraphicsBinding& binding)
  : m_binding(binding), m_vk(std::make_unique<Functions>()) { }


  VrXrImageCopy::~VrXrImageCopy() {
    std::array<VkFence, SlotCount> fences = { };
    uint32_t fenceCount = 0;

    for (const Slot& slot : m_slots) {
      if (slot.fence)
        fences[fenceCount++] = slot.fence;
    }

    // Destroying the pool frees its command buffers
    if (m_pool) {
      if (fenceCount)
        m_vk->waitForFences(m_binding.device, fenceCount, fences.data(), VK_TRUE, WaitTimeoutNs);

      m_vk->destroyCommandPool(m_binding.device, m_pool, nullptr);
    }

    for (uint32_t i = 0; i < fenceCount; i++)
      m_vk->destroyFence(m_binding.device, fences[i], nullptr);
  }


  std::unique_ptr<VrXrImageCopy> VrXrImageCopy::create(const VrGraphicsBinding& binding) {
    if (!binding.queue || !binding.device || !binding.instance)
      return nullptr;

    std::unique_ptr<VrXrImageCopy> copy(new VrXrImageCopy(binding));

    if (!copy->initialize())
      return nullptr;

    return copy;
  }


  bool VrXrImageCopy::initialize() {
    if (!m_vk->load(m_binding)) {
      Logger::err("VR: Could not load the Vulkan functions for the OpenXR image copies");
      return false;
    }

    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_binding.queueFamilyIndex;

    if (m_vk->createCommandPool(m_binding.device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
      m_pool = VK_NULL_HANDLE;
      return false;
    }

    std::array<VkCommandBuffer, SlotCount> buffers = { };

    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool        = m_pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = SlotCount;

    if (m_vk->allocateCommandBuffers(m_binding.device, &allocInfo, buffers.data()) != VK_SUCCESS)
      return false;

    // The fences start signaled, so that the first wait on them returns
    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < SlotCount; i++) {
      m_slots[i].commandBuffer = buffers[i];

      if (m_vk->createFence(m_binding.device, &fenceInfo, nullptr, &m_slots[i].fence) != VK_SUCCESS) {
        m_slots[i].fence = VK_NULL_HANDLE;
        return false;
      }
    }

    return true;
  }


  bool VrXrImageCopy::copy(const VrXrImageCopyRegion* regions, uint32_t count) {
    if (!count || count > MaxRegions)
      return false;

    Slot& slot = m_slots[m_nextSlot];
    m_nextSlot = (m_nextSlot + 1) % SlotCount;

    // The previous use of this slot must be done before its buffer is reset
    if (m_vk->waitForFences(m_binding.device, 1, &slot.fence, VK_TRUE, WaitTimeoutNs) != VK_SUCCESS)
      return false;

    m_vk->resetFences(m_binding.device, 1, &slot.fence);
    m_vk->resetCommandBuffer(slot.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk->beginCommandBuffer(slot.commandBuffer, &beginInfo);

    // Sources and targets move to the transfer layouts, and back after the copy
    std::array<VkImageMemoryBarrier, 2 * MaxRegions> before = { };
    std::array<VkImageMemoryBarrier, 2 * MaxRegions> after  = { };

    for (uint32_t i = 0; i < count; i++) {
      const VrEyeImage& source = regions[i].source;
      VkImageSubresourceRange sourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, source.arrayLayer, 1 };
      VkImageSubresourceRange targetRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

      VkImageMemoryBarrier& sourceIn = before[2 * i];
      sourceIn.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      sourceIn.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
      sourceIn.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
      sourceIn.oldLayout           = source.layout;
      sourceIn.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      sourceIn.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      sourceIn.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      sourceIn.image               = source.image;
      sourceIn.subresourceRange    = sourceRange;

      // The whole target is written, its old contents do not matter
      VkImageMemoryBarrier& targetIn = before[2 * i + 1];
      targetIn.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      targetIn.srcAccessMask       = 0;
      targetIn.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      targetIn.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
      targetIn.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      targetIn.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      targetIn.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      targetIn.image               = regions[i].target;
      targetIn.subresourceRange    = targetRange;

      VkImageMemoryBarrier& sourceOut = after[2 * i];
      sourceOut = sourceIn;
      sourceOut.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      sourceOut.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      sourceOut.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      sourceOut.newLayout     = source.layout;

      VkImageMemoryBarrier& targetOut = after[2 * i + 1];
      targetOut = targetIn;
      targetOut.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      targetOut.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      targetOut.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      targetOut.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    m_vk->cmdPipelineBarrier(slot.commandBuffer,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 2 * count, before.data());

    for (uint32_t i = 0; i < count; i++) {
      const VrEyeImage& source = regions[i].source;

      VkImageCopy region = { };
      region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, source.arrayLayer, 1 };
      region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      region.extent         = { source.extent.width, source.extent.height, 1 };

      m_vk->cmdCopyImage(slot.commandBuffer,
        source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        regions[i].target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);
    }

    m_vk->cmdPipelineBarrier(slot.commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0, 0, nullptr, 0, nullptr, 2 * count, after.data());

    m_vk->endCommandBuffer(slot.commandBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &slot.commandBuffer;

    if (m_vk->queueSubmit(m_binding.queue, 1, &submitInfo, slot.fence) != VK_SUCCESS) {
      Logger::err("VR: Could not submit the copy into the OpenXR swapchain");
      return false;
    }

    return true;
  }

}
