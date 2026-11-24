#include <cstring>
#include <type_traits>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

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
    PFN_vkCmdClearColorImage     cmdClearColorImage     = nullptr;
    PFN_vkCmdCopyBufferToImage   cmdCopyBufferToImage   = nullptr;
    PFN_vkCreateBuffer           createBuffer           = nullptr;
    PFN_vkDestroyBuffer          destroyBuffer          = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory         allocateMemory         = nullptr;
    PFN_vkFreeMemory             freeMemory             = nullptr;
    PFN_vkBindBufferMemory       bindBufferMemory       = nullptr;
    PFN_vkMapMemory              mapMemory              = nullptr;
    PFN_vkUnmapMemory            unmapMemory            = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;

    bool load(const VrGraphicsBinding& binding) {
      if (!binding.getInstanceProcAddr)
        return false;

      getPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        binding.getInstanceProcAddr(binding.instance, "vkGetPhysicalDeviceMemoryProperties"));

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
      load(cmdClearColorImage, "vkCmdClearColorImage");
      load(cmdCopyBufferToImage, "vkCmdCopyBufferToImage");
      load(createBuffer, "vkCreateBuffer");
      load(destroyBuffer, "vkDestroyBuffer");
      load(getBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
      load(allocateMemory, "vkAllocateMemory");
      load(freeMemory, "vkFreeMemory");
      load(bindBufferMemory, "vkBindBufferMemory");
      load(mapMemory, "vkMapMemory");
      load(unmapMemory, "vkUnmapMemory");

      return getPhysicalDeviceMemoryProperties && cmdClearColorImage && cmdCopyBufferToImage
          && createBuffer && destroyBuffer && getBufferMemoryRequirements && allocateMemory
          && freeMemory && bindBufferMemory && mapMemory && unmapMemory
          && createCommandPool && destroyCommandPool && allocateCommandBuffers
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

    destroyStaging();

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

    Slot* slot = beginSlot();

    if (!slot)
      return false;

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

    m_vk->cmdPipelineBarrier(slot->commandBuffer,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 2 * count, before.data());

    for (uint32_t i = 0; i < count; i++) {
      const VrEyeImage& source = regions[i].source;

      VkImageCopy region = { };
      region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, source.arrayLayer, 1 };
      region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      region.extent         = { source.extent.width, source.extent.height, 1 };

      m_vk->cmdCopyImage(slot->commandBuffer,
        source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        regions[i].target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);
    }

    m_vk->cmdPipelineBarrier(slot->commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0, 0, nullptr, 0, nullptr, 2 * count, after.data());

    return submitSlot(*slot, "copy");
  }


  bool VrXrImageCopy::fill(VkImage target, const VkClearColorValue& color) {
    Slot* slot = beginSlot();

    if (!slot)
      return false;

    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    targetBarrier(slot->commandBuffer, target, true);
    m_vk->cmdClearColorImage(slot->commandBuffer, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    targetBarrier(slot->commandBuffer, target, false);

    return submitSlot(*slot, "fill");
  }


  bool VrXrImageCopy::upload(VkImage target, VrExtent extent, const uint8_t* pixels) {
    VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;

    if (!size || !pixels)
      return false;

    // The last upload must be done with the staging buffer before it is written
    if (m_stagingFence && m_vk->waitForFences(m_binding.device, 1, &m_stagingFence, VK_TRUE, WaitTimeoutNs) != VK_SUCCESS)
      return false;

    if (!prepareStaging(size))
      return false;

    std::memcpy(m_stagingData, pixels, size_t(size));

    Slot* slot = beginSlot();

    if (!slot)
      return false;

    VkBufferImageCopy region = { };
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { extent.width, extent.height, 1 };

    targetBarrier(slot->commandBuffer, target, true);
    m_vk->cmdCopyBufferToImage(slot->commandBuffer, m_staging, target,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    targetBarrier(slot->commandBuffer, target, false);

    if (!submitSlot(*slot, "upload"))
      return false;

    m_stagingFence = slot->fence;
    return true;
  }


  VrXrImageCopy::Slot* VrXrImageCopy::beginSlot() {
    Slot& slot = m_slots[m_nextSlot];
    m_nextSlot = (m_nextSlot + 1) % SlotCount;

    // The previous use of this slot must be done before its buffer is reset
    if (m_vk->waitForFences(m_binding.device, 1, &slot.fence, VK_TRUE, WaitTimeoutNs) != VK_SUCCESS)
      return nullptr;

    m_vk->resetFences(m_binding.device, 1, &slot.fence);
    m_vk->resetCommandBuffer(slot.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk->beginCommandBuffer(slot.commandBuffer, &beginInfo);
    return &slot;
  }


  bool VrXrImageCopy::submitSlot(Slot& slot, const char* what) {
    m_vk->endCommandBuffer(slot.commandBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &slot.commandBuffer;

    if (m_vk->queueSubmit(m_binding.queue, 1, &submitInfo, slot.fence) != VK_SUCCESS) {
      Logger::err(str::format("VR: Could not submit the ", what, " into the OpenXR swapchain"));
      return false;
    }

    return true;
  }


  void VrXrImageCopy::targetBarrier(VkCommandBuffer commandBuffer, VkImage target, bool before) {
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = target;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (before) {
      // The whole image is written, its old contents do not matter
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    } else {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    m_vk->cmdPipelineBarrier(commandBuffer,
      before ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
      before ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);
  }


  bool VrXrImageCopy::prepareStaging(VkDeviceSize size) {
    if (m_staging && m_stagingSize >= size)
      return true;

    destroyStaging();

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size        = size;
    bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_vk->createBuffer(m_binding.device, &bufferInfo, nullptr, &m_staging) != VK_SUCCESS) {
      m_staging = VK_NULL_HANDLE;
      return false;
    }

    VkMemoryRequirements requirements = { };
    m_vk->getBufferMemoryRequirements(m_binding.device, m_staging, &requirements);

    VkPhysicalDeviceMemoryProperties properties = { };
    m_vk->getPhysicalDeviceMemoryProperties(m_binding.physicalDevice, &properties);

    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = properties.memoryTypeCount;

    for (uint32_t i = 0; i < properties.memoryTypeCount && type == properties.memoryTypeCount; i++) {
      if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted)
        type = i;
    }

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize  = requirements.size;
    allocInfo.memoryTypeIndex = type;

    if (type == properties.memoryTypeCount
     || m_vk->allocateMemory(m_binding.device, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
      m_stagingMemory = VK_NULL_HANDLE;
      destroyStaging();
      Logger::err("VR: Could not allocate the staging memory of the OpenXR uploads");
      return false;
    }

    if (m_vk->bindBufferMemory(m_binding.device, m_staging, m_stagingMemory, 0) != VK_SUCCESS
     || m_vk->mapMemory(m_binding.device, m_stagingMemory, 0, VK_WHOLE_SIZE, 0, &m_stagingData) != VK_SUCCESS) {
      m_stagingData = nullptr;
      destroyStaging();
      return false;
    }

    m_stagingSize = size;
    return true;
  }


  void VrXrImageCopy::destroyStaging() {
    if (m_stagingData)
      m_vk->unmapMemory(m_binding.device, m_stagingMemory);

    if (m_staging)
      m_vk->destroyBuffer(m_binding.device, m_staging, nullptr);

    if (m_stagingMemory)
      m_vk->freeMemory(m_binding.device, m_stagingMemory, nullptr);

    m_staging       = VK_NULL_HANDLE;
    m_stagingMemory = VK_NULL_HANDLE;
    m_stagingData   = nullptr;
    m_stagingSize   = 0;
  }

}
