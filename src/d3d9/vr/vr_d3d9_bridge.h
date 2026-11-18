#pragma once

#include "../d3d9_include.h"
#include "../d3d9_interfaces.h"

#include "../../util/com/com_pointer.h"

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Connects a DXVK D3D9 device to an IVRBackend
   *
   * Everything here goes through DXVK's public Vulkan interop interfaces
   * (ID3D9VkInteropDevice and ID3D9VkInteropTexture), so it needs no access
   * to D3D9 internals and works with any device created by this DLL.
   */
  class VrD3D9Bridge {

  public:

    /**
     * \brief Queries the Vulkan objects DXVK renders with
     *
     * \param [in] device D3D9 device created by DXVK
     * \param [out] binding Vulkan handles and graphics queue
     * \returns \c false if the device does not expose them
     */
    static bool queryGraphicsBinding(
            IDirect3DDevice9*     device,
            VrGraphicsBinding&    binding);

    /**
     * \brief Calls IVRBackend::beginFrame with the queue locked
     *
     * The submission queue is locked without a flush: the frame's
     * rendering, issued afterwards, reaches the queue after the backend's
     * work.
     *
     * \param [in] device D3D9 device the frame is rendered with
     * \param [in] backend Backend to notify
     */
    static void beginFrame(
            IDirect3DDevice9*     device,
            IVRBackend&           backend);

    /**
     * \brief Calls IVRBackend::submitEmptyFrame with the queue locked
     *
     * \param [in] device D3D9 device the frame is rendered with
     * \param [in] backend Backend to notify
     * \param [in] displayTime Predicted display time from IVRBackend::waitFrame
     */
    static void submitEmptyFrame(
            IDirect3DDevice9*     device,
            IVRBackend&           backend,
            int64_t               displayTime);

    /**
     * \brief Shows a rendered texture on one of the backend's panels
     *
     * Flushes all pending rendering, waits until it has been submitted to the
     * GPU queue and then calls IVRBackend::submitPanel with the queue locked,
     * so the backend may use the device queue without racing DXVK. A stereo
     * frame's panels go through VrFrameSubmitter instead.
     *
     * \param [in] device D3D9 device that owns the texture
     * \param [in] backend Backend to submit to
     * \param [in] id Panel to show the texture on
     * \param [in] texture Panel image
     * \param [in] pose Panel pose, relative to \c anchor
     * \param [in] width Panel width, in metres
     * \param [in] anchor What the pose is relative to
     * \param [in] premultiplied Whether the image's colours are
     *    premultiplied by its alpha
     * \returns \c true if the backend accepted the panel
     */
    static bool submitPanel(
            IDirect3DDevice9*     device,
            IVRBackend&           backend,
            VrPanelId             id,
            IDirect3DTexture9*    texture,
      const VrPose&               pose,
            float                 width,
            VrPanelAnchor         anchor,
            bool                  premultiplied = false);

    /**
     * \brief Gets the Vulkan image of a texture
     *
     * \param [in] texture Texture of the device
     * \param [out] interop Keeps the texture alive while the image is used
     * \param [out] image The texture's image and its current layout
     * \returns \c false if the texture has no image
     */
    static bool queryImage(
            IDirect3DTexture9*    texture,
            Com<ID3D9VkInteropTexture>& interop,
            VrEyeImage&           image);

  private:

    /**
     * \brief Gets all rendering so far to the queue and locks it
     * \returns Milliseconds spent waiting for DXVK's threads
     */
    static double flushAndLock(
            ID3D9VkInteropDevice* interop);

  };

}
