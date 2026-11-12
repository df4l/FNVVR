#include "../d3d9_device.h"
#include "../d3d9_interfaces.h"

#include "../../util/com/com_pointer.h"
#include "../../util/log/log.h"
#include "../../util/util_time.h"

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

    // The device always comes from this DLL. Its loader is the one DXVK picked
    // at startup, which is winevulkan.dll rather than vulkan-1.dll under Wine.
    binding.getInstanceProcAddr = static_cast<D3D9DeviceEx*>(device)
      ->GetDXVKDevice()->vki()->getLoaderProc();

    interop->GetVulkanHandles(&binding.instance, &binding.physicalDevice, &binding.device);
    interop->GetSubmissionQueue(&binding.queue, &binding.queueIndex, &binding.queueFamilyIndex);

    // The interop interface belongs to the device, which outlives the
    // session; no reference is kept so the device can still be released
    ID3D9VkInteropDevice* lock = interop.ptr();
    binding.lockQueue    = [lock] { lock->LockSubmissionQueue(); };
    binding.releaseQueue = [lock] { lock->ReleaseSubmissionQueue(); };
    return true;
  }


  void VrD3D9Bridge::beginFrame(
          IDirect3DDevice9*     device,
          IVRBackend&           backend) {
    Com<ID3D9VkInteropDevice> interop;

    if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
        reinterpret_cast<void**>(&interop))))
      return;

    interop->LockSubmissionQueue();
    backend.beginFrame();
    interop->ReleaseSubmissionQueue();
  }


  void VrD3D9Bridge::submitEmptyFrame(
          IDirect3DDevice9*     device,
          IVRBackend&           backend,
          int64_t               displayTime) {
    Com<ID3D9VkInteropDevice> interop;

    if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
        reinterpret_cast<void**>(&interop))))
      return;

    interop->LockSubmissionQueue();
    backend.submitEmptyFrame(displayTime);
    interop->ReleaseSubmissionQueue();
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
      if (!queryImage(eyes[i], textures[i], frame.images[i]))
        return false;
    }

    // Rendering has to reach the queue before the backend reads the images.
    // Locking the submission queue waits for DXVK's submission thread to
    // drain, so everything issued so far is already queued ahead of us.
    frame.syncMs = flushAndLock(interop.ptr());
    bool result = backend.submitFrame(frame);
    interop->ReleaseSubmissionQueue();
    return result;
  }


  bool VrD3D9Bridge::submitPanel(
          IDirect3DDevice9*     device,
          IVRBackend&           backend,
          VrPanelId             id,
          IDirect3DTexture9*    texture,
    const VrPose&               pose,
          float                 width,
          VrPanelAnchor         anchor,
          bool                  premultiplied) {
    Com<ID3D9VkInteropDevice> interop;

    if (FAILED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),
        reinterpret_cast<void**>(&interop))))
      return false;

    VrPanelSubmission panel;
    panel.pose   = pose;
    panel.width  = width;
    panel.anchor = anchor;
    panel.premultiplied = premultiplied;

    Com<ID3D9VkInteropTexture> textureInterop;

    if (!queryImage(texture, textureInterop, panel.image))
      return false;

    // See submitStereoFrame
    panel.syncMs = flushAndLock(interop.ptr());
    bool result = backend.submitPanel(id, panel);
    interop->ReleaseSubmissionQueue();
    return result;
  }


  bool VrD3D9Bridge::queryImage(
          IDirect3DTexture9*    texture,
          Com<ID3D9VkInteropTexture>& interop,
          VrEyeImage&           image) {
    if (!texture || FAILED(texture->QueryInterface(__uuidof(ID3D9VkInteropTexture),
        reinterpret_cast<void**>(&interop))))
      return false;

    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };

    if (FAILED(interop->GetVulkanImageInfo(&image.image, &image.layout, &info)))
      return false;

    image.format     = info.format;
    image.extent     = { info.extent.width, info.extent.height };
    image.arrayLayer = 0;
    return true;
  }


  double VrD3D9Bridge::flushAndLock(
          ID3D9VkInteropDevice* interop) {
    auto start = high_resolution_clock::now();

    interop->FlushRenderingCommands();
    interop->LockSubmissionQueue();

    return std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count();
  }

}
