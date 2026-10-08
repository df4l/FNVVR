#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_d3d9_bridge.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_stereo_renderer.h"

namespace dxvk {

  namespace {

    // Main::Render is __thiscall with three stack arguments, which a
    // __fastcall function with an unused second argument matches exactly
    using RenderFn = void (__fastcall*)(void* main, void* unused,
      uint32_t arg0, uint32_t arg1, uint32_t arg2);

    VrStereoRenderer* g_stereoRenderer = nullptr;
    RenderFn          g_originalRender = nullptr;

  }


  VrStereoRenderer::VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device, bool showPreview)
  : m_backend(backend), m_device(device), m_showPreview(showPreview) { }


  VrStereoRenderer::~VrStereoRenderer() {
    // The patched call stays in place and falls back to the game's own render
    g_stereoRenderer = nullptr;
  }


  std::unique_ptr<VrStereoRenderer> VrStereoRenderer::install(
          IVRBackend&           backend,
          IDirect3DDevice9*     device,
          bool                  showPreview) {
    if (g_stereoRenderer)
      return nullptr;

    if (!VrGameMemory::redirectCall(VrGame::RenderCallSite,
        VrGame::Render, reinterpret_cast<const void*>(&VrStereoRenderer::renderHook))) {
      Logger::info("VR: The game's render call was not found, stereo rendering is disabled");
      return nullptr;
    }

    g_originalRender = reinterpret_cast<RenderFn>(VrGame::Render);

    std::unique_ptr<VrStereoRenderer> renderer(new VrStereoRenderer(backend, device, showPreview));
    g_stereoRenderer = renderer.get();
    return renderer;
  }


  void __fastcall VrStereoRenderer::renderHook(
          void*                 main,
          void*                 unused,
          uint32_t              arg0,
          uint32_t              arg1,
          uint32_t              arg2) {
    if (g_stereoRenderer)
      g_stereoRenderer->renderFrame(main, arg0, arg1, arg2);
    else
      g_originalRender(main, nullptr, arg0, arg1, arg2);
  }


  void VrStereoRenderer::renderFrame(void* main, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    VrGameCamera camera;

    if (m_texturesFailed || !camera.acquire() || !createEyeTextures()) {
      g_originalRender(main, nullptr, arg0, arg1, arg2);
      return;
    }

    VrFrameTiming timing = m_backend.waitFrame();

    if (!timing.shouldRender) {
      m_backend.submitEmptyFrame(timing.predictedDisplayTime);
      g_originalRender(main, nullptr, arg0, arg1, arg2);
      return;
    }

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_gameTarget))) {
      g_originalRender(main, nullptr, arg0, arg1, arg2);
      return;
    }

    VrInputState input = m_backend.pollInput(timing.predictedDisplayTime);

    if (!m_hasReference && input.isHeadTracked) {
      m_reference    = vrMakeRecenterReference(input.headPose);
      m_hasReference = true;
    }

    auto views = m_backend.locateViews(timing.predictedDisplayTime);

    VrGameCameraState savedState = camera.save();
    VrGameCameraPose  basePose   = camera.readPose();

    for (uint32_t eye = 0; eye < VrEyeCount; eye++)
      renderEye(eye, views[eye], camera, basePose, main, arg0, arg1, arg2);

    camera.restore(savedState);
    m_gameTarget = nullptr;

    IDirect3DTexture9* eyes[VrEyeCount] = { m_eyeTextures[0].ptr(), m_eyeTextures[1].ptr() };

    if (!VrD3D9Bridge::submitStereoFrame(m_device, m_backend, eyes, views,
        timing.predictedDisplayTime) && !m_loggedFailure) {
      Logger::err("VR: The backend rejected a stereo frame");
      m_loggedFailure = true;
    }

    if (m_preview)
      m_preview->present(eyes);
  }


  bool VrStereoRenderer::createEyeTextures() {
    if (m_eyeTextures[0] != nullptr)
      return true;

    Com<IDirect3DSurface9> target;

    if (FAILED(m_device->GetRenderTarget(0, &target)))
      return false;

    D3DSURFACE_DESC desc = { };
    target->GetDesc(&desc);

    VrExtent extent = m_backend.recommendedEyeExtent();

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      if (FAILED(m_device->CreateTexture(extent.width, extent.height, 1,
          D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
          &m_eyeTextures[i], nullptr))) {
        Logger::err(str::format("VR: Failed to create the ", extent.width, "x",
          extent.height, " eye textures"));
        m_eyeTextures[0] = nullptr;
        m_eyeTextures[1] = nullptr;
        m_texturesFailed = true;
        return false;
      }
    }

    if (m_showPreview)
      m_preview = VrPreviewWindow::create(m_device, extent);

    if (!m_loggedStart) {
      Logger::info(str::format("VR: Stereo rendering started, eye size ",
        extent.width, "x", extent.height, ", game target ", desc.Width, "x", desc.Height));
      m_loggedStart = true;
    }

    return true;
  }


  void VrStereoRenderer::renderEye(
          uint32_t              eye,
    const VrEyeView&            view,
          VrGameCamera&         camera,
    const VrGameCameraPose&     basePose,
          void*                 main,
          uint32_t              arg0,
          uint32_t              arg1,
          uint32_t              arg2) {
    VrPose eyeInReference = m_hasReference
      ? vrComputeEyeInReference(m_reference, view.pose)
      : VrPose();

    camera.apply(
      vrComputeEyeCameraPose(basePose, eyeInReference, VrGameUnitsPerMetre),
      vrComputeGameFrustum(view.fov));

    g_originalRender(main, nullptr, arg0, arg1, arg2);

    if (!copyRenderTarget(eye) && !m_loggedFailure) {
      Logger::err("VR: Failed to copy the rendered eye image");
      m_loggedFailure = true;
    }
  }


  bool VrStereoRenderer::copyRenderTarget(uint32_t eye) {
    Com<IDirect3DSurface9> destination;

    if (FAILED(m_eyeTextures[eye]->GetSurfaceLevel(0, &destination)))
      return false;

    return SUCCEEDED(m_device->StretchRect(
      m_gameTarget.ptr(), nullptr, destination.ptr(), nullptr, D3DTEXF_LINEAR));
  }

}
