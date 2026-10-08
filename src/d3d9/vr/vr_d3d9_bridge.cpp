#include "../d3d9_interfaces.h"

#include "../../util/com/com_pointer.h"
#include "../../util/log/log.h"

#include "vr_d3d9_bridge.h"

namespace dxvk {

  bool VrD3D9Bridge::queryGraphicsBinding(
          IDirect3DDevice9*     device,
          VrGraphicsBinding&    binding) {
    Com<ID3D9VkInteropDevice> interop;

    if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
        reinterpret_cast<void**>(&interop)))) {
      Logger::err("VR: Device does not expose the Vulkan interop interface");
      return false;
    }

    VkQueue queue = VK_NULL_HANDLE;

    interop->GetVulkanHandles(&binding.instance, &binding.physicalDevice, &binding.device);
    interop->GetSubmissionQueue(&queue, &binding.queueIndex, &binding.queueFamilyIndex);
    return true;
  }


  bool VrD3D9Bridge::submitStereoFrame(
          IDirect3DDevice9*     device,
          IVRBackend&           backend,
          IDirect3DTexture9*    eyes[VrEyeCount],
    const std::array<VrEyeView, VrEyeCount>& views,
          int64_t               displayTime) {
    Com<ID3D9VkInteropDevice> interop;

    if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
        reinterpret_cast<void**>(&interop))))
      return false;

    VrFrameSubmission frame;
    frame.views       = views;
    frame.displayTime = displayTime;

    // The textures must outlive the submission, hold references to
    // their interop interfaces until the backend is done.
    Com<ID3D9VkInteropTexture> textures[VrEyeCount];

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      if (!eyes[i] || FAILED(eyes[i]->QueryInterface(__uuidof(ID3D9VkInteropTexture),
          reinterpret_cast<void**>(&textures[i]))))
        return false;

      VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      VrEyeImage& image = frame.images[i];

      if (FAILED(textures[i]->GetVulkanImageInfo(&image.image, &image.layout, &info)))
        return false;

      image.format      = info.format;
      image.extent      = { info.extent.width, info.extent.height };
      image.arrayLayer  = 0;
    }

    // Rendering has to reach the queue before the backend reads the images.
    // Locking the submission queue waits for DXVK's submission thread to
    // drain, so everything issued so far is already queued ahead of us.
    interop->FlushRenderingCommands();
    interop->LockSubmissionQueue();
    bool result = backend.submitFrame(frame);
    interop->ReleaseSubmissionQueue();
    return result;
  }

}
