#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_d3d9_bridge.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_stereo_renderer.h"

namespace dxvk {

  namespace {

    // Main::Swap is __thiscall without stack arguments
    using SwapFn = void (__fastcall*)(void* main, void* unused);

    // InterfaceManager::RenderInterface is __thiscall with two stack arguments,
    // which a __fastcall function with an unused second argument matches exactly
    using RenderInterfaceFn = void (__fastcall*)(void* interfaceManager, void* unused,
      void* arg0, uint32_t arg1);

    // PlaceCamera is __thiscall without arguments
    using PlaceCameraFn = void (__fastcall*)(void* main, void* unused);

    // NiCamera::UpdateWorldData is __thiscall with one stack argument
    using CameraUpdateFn = void (__fastcall*)(void* camera, void* unused, void* updateData);

    VrStereoRenderer* g_stereoRenderer = nullptr;

    SwapFn            g_originalSwap            = reinterpret_cast<SwapFn>(VrGame::Swap);
    RenderInterfaceFn g_originalRenderInterface = reinterpret_cast<RenderInterfaceFn>(VrGame::RenderInterface);
    PlaceCameraFn     g_originalPlaceCamera     = reinterpret_cast<PlaceCameraFn>(VrGame::PlaceCamera);
    CameraUpdateFn    g_originalCameraUpdate    = reinterpret_cast<CameraUpdateFn>(VrGame::CameraUpdateWorldData);

  }


  VrStereoRenderer::VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device, bool showPreview)
  : m_backend(backend), m_device(device), m_showPreview(showPreview) { }


  VrStereoRenderer::~VrStereoRenderer() {
    // The patched calls stay in place and fall through to the game's own code
    g_stereoRenderer = nullptr;
  }


  std::unique_ptr<VrStereoRenderer> VrStereoRenderer::install(
          IVRBackend&           backend,
          IDirect3DDevice9*     device,
          bool                  showPreview) {
    if (g_stereoRenderer)
      return nullptr;

    // Every hook forwards to the game's own function while no renderer is
    // active, so a patch that stays in place after a later one failed is inert
    bool patched = VrGameMemory::redirectCall(VrGame::SwapCallSite, VrGame::Swap,
      reinterpret_cast<const void*>(&VrStereoRenderer::swapHook));

    patched = patched && VrGameMemory::redirectCall(VrGame::RenderInterfaceCallSite,
      VrGame::RenderInterface, reinterpret_cast<const void*>(&VrStereoRenderer::renderInterfaceHook));

    for (uintptr_t site : VrGame::PlaceCameraCallSites) {
      patched = patched && VrGameMemory::redirectCall(site, VrGame::PlaceCamera,
        reinterpret_cast<const void*>(&VrStereoRenderer::placeCameraHook));
    }

    patched = patched && VrGameMemory::redirectVirtual(VrGame::CameraUpdateWorldDataSlot,
      VrGame::CameraUpdateWorldData, reinterpret_cast<const void*>(&VrStereoRenderer::cameraUpdateHook));

    if (!patched) {
      Logger::info("VR: The game's frame code was not found, stereo rendering is disabled");
      return nullptr;
    }

    std::unique_ptr<VrStereoRenderer> renderer(new VrStereoRenderer(backend, device, showPreview));

    if (!renderer->m_gameState.initialize())
      Logger::info("VR: The game state flags were not found, game state changes are not logged");

    g_stereoRenderer = renderer.get();
    return renderer;
  }


  void __fastcall VrStereoRenderer::swapHook(
          void*                 main,
          void*                 unused) {
    if (g_stereoRenderer)
      g_stereoRenderer->renderFrame(main);
    else
      g_originalSwap(main, nullptr);
  }


  void __fastcall VrStereoRenderer::renderInterfaceHook(
          void*                 interfaceManager,
          void*                 unused,
          void*                 arg0,
          uint32_t              arg1) {
    // The 3D image is complete here and the interface is not drawn yet
    if (g_stereoRenderer)
      g_stereoRenderer->captureEye();

    g_originalRenderInterface(interfaceManager, nullptr, arg0, arg1);
  }


  void __fastcall VrStereoRenderer::placeCameraHook(
          void*                 main,
          void*                 unused) {
    g_originalPlaceCamera(main, nullptr);

    if (g_stereoRenderer && g_stereoRenderer->m_eyeView)
      g_stereoRenderer->applyEyePose();
  }


  void __fastcall VrStereoRenderer::cameraUpdateHook(
          void*                 camera,
          void*                 unused,
          void*                 updateData) {
    g_originalCameraUpdate(camera, nullptr, updateData);

    if (g_stereoRenderer && g_stereoRenderer->m_eyeView
     && g_stereoRenderer->m_renderCamera.isCamera(camera))
      g_stereoRenderer->applyEyePose();
  }


  void VrStereoRenderer::renderFrame(void* main) {
    logGameState();

    if (m_texturesFailed || !m_renderCamera.acquire() || !createEyeTextures()) {
      g_originalSwap(main, nullptr);
      return;
    }

    VrFrameTiming timing = m_backend.waitFrame();

    if (!timing.shouldRender) {
      m_backend.submitEmptyFrame(timing.predictedDisplayTime);
      g_originalSwap(main, nullptr);
      return;
    }

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_gameTarget))) {
      m_backend.submitEmptyFrame(timing.predictedDisplayTime);
      g_originalSwap(main, nullptr);
      return;
    }

    VrInputState input = m_backend.pollInput(timing.predictedDisplayTime);

    if (!m_hasReference && input.isHeadTracked) {
      m_reference    = vrMakeRecenterReference(input.headPose);
      m_hasReference = true;
    }

    auto views = m_backend.locateViews(timing.predictedDisplayTime);

    m_gameCamera     = m_renderCamera.save();
    m_gameCameraPose = m_renderCamera.readPose();

    for (uint32_t eye = 0; eye < VrEyeCount; eye++) {
      m_eyeCopied[eye] = false;
      renderEye(eye, views[eye], main);
    }

    m_renderCamera.restore(m_gameCamera);
    m_gameTarget = nullptr;

    if (!m_eyeCopied[0] || !m_eyeCopied[1]) {
      if (!m_loggedFailure) {
        Logger::err("VR: The game did not reach the interface while drawing an eye, the frame is dropped");
        m_loggedFailure = true;
      }

      m_backend.submitEmptyFrame(timing.predictedDisplayTime);
      return;
    }

    IDirect3DTexture9* eyes[VrEyeCount] = { m_eyeTextures[0].ptr(), m_eyeTextures[1].ptr() };

    if (!VrD3D9Bridge::submitStereoFrame(m_device, m_backend, eyes, views,
        timing.predictedDisplayTime) && !m_loggedFailure) {
      Logger::err("VR: The backend rejected a stereo frame");
      m_loggedFailure = true;
    }

    if (m_preview)
      m_preview->present(eyes);
  }


  void VrStereoRenderer::logGameState() {
    VrGameStateKind state = m_gameState.read();

    if (state == m_lastGameState)
      return;

    Logger::info(str::format("VR: Game state: ", VrGameState::name(state)));
    m_lastGameState = state;
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
          void*                 main) {
    m_eye     = eye;
    m_eyeView = &view;

    applyEyePose();
    g_originalSwap(main, nullptr);

    m_eye     = NoEye;
    m_eyeView = nullptr;
  }


  void VrStereoRenderer::applyEyePose() {
    VrPose eyeInReference = m_hasReference
      ? vrComputeEyeInReference(m_reference, m_eyeView->pose)
      : VrPose();

    m_renderCamera.apply(
      vrComputeEyeCameraPose(m_gameCameraPose, eyeInReference, VrGameUnitsPerMetre),
      vrComputeGameFrustum(m_eyeView->fov));
  }


  void VrStereoRenderer::captureEye() {
    if (m_eye == NoEye || m_eyeCopied[m_eye])
      return;

    m_eyeCopied[m_eye] = copyRenderTarget(m_eye);

    if (!m_eyeCopied[m_eye] && !m_loggedFailure) {
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
