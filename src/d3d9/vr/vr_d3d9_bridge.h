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
     * \brief Hands two rendered eye textures to the backend
     *
     * Flushes all pending rendering, waits until it has been submitted to the
     * GPU queue and then calls IVRBackend::submitFrame with the queue locked,
     * so the backend may use the device queue without racing DXVK.
     *
     * \param [in] device D3D9 device that owns the textures
     * \param [in] backend Backend to submit to
     * \param [in] eyes Eye textures, indexed by VrEye
     * \param [in] views Eye poses and field of view used for rendering
     * \param [in] displayTime Predicted display time from IVRBackend::waitFrame
     * \returns \c true if the backend accepted the frame
     */
    static bool submitStereoFrame(
            IDirect3DDevice9*     device,
            IVRBackend&           backend,
            IDirect3DTexture9*    eyes[VrEyeCount],
      const std::array<VrEyeView, VrEyeCount>& views,
            int64_t               displayTime);

    /**
     * \brief Shows a rendered texture on the backend's panel
     *
     * Same synchronisation as submitStereoFrame, with IVRBackend::submitPanel.
     *
     * \param [in] device D3D9 device that owns the texture
     * \param [in] backend Backend to submit to
     * \param [in] texture Panel image
     * \param [in] pose Panel pose in tracking space
     * \param [in] width Panel width, in metres
     * \returns \c true if the backend accepted the panel
     */
    static bool submitPanel(
            IDirect3DDevice9*     device,
            IVRBackend&           backend,
            IDirect3DTexture9*    texture,
      const VrPose&               pose,
            float                 width);

  private:

    static bool queryImage(
            IDirect3DTexture9*    texture,
            Com<ID3D9VkInteropTexture>& interop,
            VrEyeImage&           image);

  };

}
