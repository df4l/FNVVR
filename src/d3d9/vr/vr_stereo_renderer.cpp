#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_d3d9_bridge.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_math.h"
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

    // Head pose used to place the panel before the headset reported one:
    // standing at the origin and looking ahead
    constexpr float DefaultHeadHeight = 1.6f;

    /**
     * \brief Checks whether a state has no 3D scene and is shown on the panel
     */
    bool showsPanel(VrGameStateKind state) {
      return state == VrGameStateKind::MainMenu
          || state == VrGameStateKind::Loading;
    }

    SwapFn            g_originalSwap            = reinterpret_cast<SwapFn>(VrGame::Swap);
    RenderInterfaceFn g_originalRenderInterface = reinterpret_cast<RenderInterfaceFn>(VrGame::RenderInterface);
    PlaceCameraFn     g_originalPlaceCamera     = reinterpret_cast<PlaceCameraFn>(VrGame::PlaceCamera);
    CameraUpdateFn    g_originalCameraUpdate    = reinterpret_cast<CameraUpdateFn>(VrGame::CameraUpdateWorldData);

  }


  VrStereoRenderer::VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device,
    bool showPreview, const VrPanelConfig& panel)
  : m_backend(backend), m_device(device), m_showPreview(showPreview), m_panelConfig(panel) { }


  VrStereoRenderer::~VrStereoRenderer() {
    // The patched calls stay in place and fall through to the game's own code
    g_stereoRenderer = nullptr;
    hidePanel();
  }


  std::unique_ptr<VrStereoRenderer> VrStereoRenderer::install(
          IVRBackend&           backend,
          IDirect3DDevice9*     device,
          bool                  showPreview,
    const VrPanelConfig&        panel) {
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

    std::unique_ptr<VrStereoRenderer> renderer(new VrStereoRenderer(backend, device, showPreview, panel));

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


  bool VrStereoRenderer::onPresent(IDirect3DSwapChain9* swapchain) {
    if (m_eye == 0)
      return true;

    if (m_eye == NoEye)
      updatePanel(swapchain);

    return false;
  }


  void VrStereoRenderer::renderFrame(void* main) {
    if (showsPanel(readGameState())) {
      renderPanelFrame(main);
      return;
    }

    hidePanel();

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
    trackHead(input);

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


  void VrStereoRenderer::renderPanelFrame(void* main) {
    // The runtime draws the panel. The game draws one frame, which
    // onPresent copies to the panel. Waiting still paces the game to the
    // headset and provides the head pose the panel is placed with.
    VrFrameTiming timing = m_backend.waitFrame();
    trackHead(m_backend.pollInput(timing.predictedDisplayTime));
    m_backend.submitEmptyFrame(timing.predictedDisplayTime);

    g_originalSwap(main, nullptr);
  }


  void VrStereoRenderer::trackHead(const VrInputState& input) {
    if (!input.isHeadTracked)
      return;

    m_headPose    = input.headPose;
    m_hasHeadPose = true;
  }


  VrGameStateKind VrStereoRenderer::readGameState() {
    VrGameStateKind state = m_gameState.read();

    if (state != m_lastGameState) {
      Logger::info(str::format("VR: Game state: ", VrGameState::name(state)));
      m_lastGameState = state;
    }

    return state;
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



  void VrStereoRenderer::updatePanel(IDirect3DSwapChain9* swapchain) {
    if (!showsPanel(readGameState())) {
      hidePanel();
      return;
    }

    if (m_panelFailed)
      return;

    Com<IDirect3DSurface9> backBuffer;

    if (FAILED(swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))
     || !copyPanelImage(backBuffer.ptr())) {
      if (!m_loggedPanelFailure) {
        Logger::err("VR: Failed to copy the frame to the panel");
        m_loggedPanelFailure = true;
      }

      return;
    }

    // Placed once when it appears, and again once the head is known
    if (!m_panelShown || (!m_panelPlacedWithHead && m_hasHeadPose))
      placePanel();

    m_panelShown = VrD3D9Bridge::submitPanel(m_device, m_backend, VrPanelId::Menu,
      m_panelTexture.ptr(), m_panelPose, m_panelConfig.width, VrPanelAnchor::Room);

    if (!m_panelShown && !m_loggedPanelFailure) {
      Logger::err("VR: The backend rejected the panel");
      m_loggedPanelFailure = true;
    }
  }


  void VrStereoRenderer::placePanel() {
    VrPose head;
    head.position.y = DefaultHeadHeight;

    if (m_hasHeadPose)
      head = m_headPose;

    m_panelPose = vrComputePanelPose(head, m_panelConfig.distance);
    m_panelPlacedWithHead = m_hasHeadPose;
  }


  void VrStereoRenderer::hidePanel() {
    if (!m_panelShown)
      return;

    m_backend.hidePanel(VrPanelId::Menu);
    m_panelShown = false;
  }


  bool VrStereoRenderer::createPanelTextures(const D3DSURFACE_DESC& desc) {
    if (m_panelTexture != nullptr) {
      D3DSURFACE_DESC current = { };
      m_panelTexture->GetLevelDesc(0, &current);

      if (current.Width == desc.Width && current.Height == desc.Height)
        return true;

      m_panelTexture = nullptr;
      m_panelStaging = nullptr;
    }

    bool created = SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_panelTexture, nullptr));

    if (created && desc.Format != D3DFMT_X8R8G8B8) {
      created = SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_panelStaging, nullptr));
    }

    if (!created) {
      Logger::err(str::format("VR: Failed to create the ", desc.Width, "x", desc.Height,
        " panel texture, menus are not shown in the headset"));
      m_panelTexture = nullptr;
      m_panelStaging = nullptr;
      m_panelFailed  = true;
    }

    return created;
  }


  bool VrStereoRenderer::copyPanelImage(IDirect3DSurface9* backBuffer) {
    D3DSURFACE_DESC desc = { };
    backBuffer->GetDesc(&desc);

    if (!createPanelTextures(desc))
      return false;

    Com<IDirect3DSurface9> panel;

    if (FAILED(m_panelTexture->GetSurfaceLevel(0, &panel)))
      return false;

    // The runtime blends the panel with its alpha channel, which the game
    // leaves undefined. DXVK reads the alpha of an X8R8G8B8 image as one
    // when it converts it, so an image with alpha is first copied into an
    // X8R8G8B8 texture.
    Com<IDirect3DSurface9> source = backBuffer;

    if (m_panelStaging != nullptr) {
      Com<IDirect3DSurface9> staging;

      if (FAILED(m_panelStaging->GetSurfaceLevel(0, &staging))
       || FAILED(m_device->StretchRect(backBuffer, nullptr, staging.ptr(), nullptr, D3DTEXF_NONE)))
        return false;

      source = staging;
    }

    return SUCCEEDED(m_device->StretchRect(
      source.ptr(), nullptr, panel.ptr(), nullptr, D3DTEXF_NONE));
  }

}
