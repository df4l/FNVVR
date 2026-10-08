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
   * \brief Renders the game's frame once per eye
   *
   * Takes over the call to Main::Swap in the game's main loop, which draws
   * the world, the HUD and presents. Everything before it, the game logic,
   * still runs once per frame. For each eye the world camera is moved to the
   * eye's pose and field of view and the game draws a whole frame. The 3D
   * image is copied into the eye texture just before the game draws the HUD
   * over it, so the eye textures hold the scene without the interface.
   *
   * The game positions the camera again at several points while it draws,
   * so two more hooks keep the eye pose in place.
   */
  class VrStereoRenderer {

  public:

    ~VrStereoRenderer();

    /**
     * \brief Hooks the game's frame
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

    /**
     * \brief Checks whether the frame being drawn is the left eye's
     *
     * The game presents after every frame. The left eye's present is
     * dropped, so the window shows the right eye and the game's frame
     * state machine still sees a completed frame.
     */
    bool skipsPresent() const { return m_eye == 0; }

  private:

    static constexpr uint32_t NoEye = ~0u;

    VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device, bool showPreview);

    IVRBackend&        m_backend;
    IDirect3DDevice9*  m_device;
    bool               m_showPreview;

    Com<IDirect3DTexture9> m_eyeTextures[VrEyeCount];
    std::unique_ptr<VrPreviewWindow> m_preview;

    // The game composes its final 3D image into the backbuffer
    Com<IDirect3DSurface9> m_gameTarget;

    // The eye the game is drawing, or NoEye outside of the per-eye frames
    uint32_t            m_eye         = NoEye;
    const VrEyeView*    m_eyeView     = nullptr;
    bool                m_eyeCopied[VrEyeCount] = { };

    // The camera the game draws with, as the game placed it for this frame.
    // It is restored after the eyes.
    VrGameCamera        m_renderCamera;
    VrGameCameraState   m_gameCamera  = { };
    VrGameCameraPose    m_gameCameraPose;

    VrPose m_reference;
    bool   m_hasReference  = false;
    bool   m_texturesFailed = false;
    bool   m_loggedStart   = false;
    bool   m_loggedFailure = false;

    static void __fastcall swapHook(
            void*                 main,
            void*                 unused);

    static void __fastcall renderInterfaceHook(
            void*                 interfaceManager,
            void*                 unused,
            void*                 arg0,
            uint32_t              arg1);

    static void __fastcall placeCameraHook(
            void*                 main,
            void*                 unused);

    static void __fastcall cameraUpdateHook(
            void*                 camera,
            void*                 unused,
            void*                 updateData);

    void renderFrame(void* main);

    bool createEyeTextures();

    void renderEye(
            uint32_t              eye,
      const VrEyeView&            view,
            void*                 main);

    void applyEyePose();

    void captureEye();

    bool copyRenderTarget(uint32_t eye);

  };

}
