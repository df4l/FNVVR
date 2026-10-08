#pragma once

#include <array>
#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_backend.h"
#include "vr_game_camera.h"
#include "vr_preview_window.h"

namespace dxvk {

  /**
   * \brief Renders the game's world once per eye
   *
   * Takes over the call to Main::Render in the gameplay branch of the game's
   * frame. For each eye it moves the world camera to the eye's pose and field
   * of view, lets the game render, copies the result into the eye texture and
   * puts the camera back. The game logic still runs once per frame, and the
   * HUD and the desktop window show the right eye.
   *
   * Menus and loading screens take other paths and stay monoscopic.
   */
  class VrStereoRenderer {

  public:

    ~VrStereoRenderer();

    /**
     * \brief Hooks the game's render call
     *
     * \param [in] backend Backend that provides poses and receives frames
     * \param [in] device D3D9 device the game renders with
     * \param [in] showPreview Show both eyes in a window of their own
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrStereoRenderer> install(
            IVRBackend&           backend,
            IDirect3DDevice9*     device,
            bool                  showPreview);

  private:

    VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device, bool showPreview);

    IVRBackend&        m_backend;
    IDirect3DDevice9*  m_device;
    bool               m_showPreview;

    Com<IDirect3DTexture9> m_eyeTextures[VrEyeCount];
    std::unique_ptr<VrPreviewWindow> m_preview;

    // The game composes its final image into the backbuffer. The render target
    // bound after Main::Render is not reliable, in dialogs it is an HDR target.
    Com<IDirect3DSurface9> m_gameTarget;

    VrPose m_reference;
    bool   m_hasReference  = false;
    bool   m_texturesFailed = false;
    bool   m_loggedStart   = false;
    bool   m_loggedFailure = false;

    static void __fastcall renderHook(
            void*                 main,
            void*                 unused,
            uint32_t              arg0,
            uint32_t              arg1,
            uint32_t              arg2);

    void renderFrame(void* main, uint32_t arg0, uint32_t arg1, uint32_t arg2);

    bool createEyeTextures();

    void renderEye(
            uint32_t              eye,
            const VrEyeView&      view,
            VrGameCamera&         camera,
      const VrGameCameraPose&     basePose,
            void*                 main,
            uint32_t              arg0,
            uint32_t              arg1,
            uint32_t              arg2);

    bool copyRenderTarget(uint32_t eye);

  };

}
