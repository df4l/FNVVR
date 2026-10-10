#include <algorithm>
#include <cmath>
#include <iterator>

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

    // The interface cull is __cdecl with three arguments
    using InterfaceCullFn = void (__cdecl*)(void* camera, void* sceneGraph, void* data);

    // MTRenderManager::AddAccumTask is __thiscall with nine stack arguments
    using AccumTaskFn = void (__fastcall*)(void* manager, void* unused,
      uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
      uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);

    // PlaceCamera is __thiscall without arguments
    using PlaceCameraFn = void (__fastcall*)(void* main, void* unused);

    // NiCamera::UpdateWorldData is __thiscall with one stack argument
    using CameraUpdateFn = void (__fastcall*)(void* camera, void* unused, void* updateData);

    VrStereoRenderer* g_stereoRenderer = nullptr;

    // HUD groups shown on the panel in front of the head
    constexpr uintptr_t HeadHudGroups[] = { VrGame::HudMessages, VrGame::HudQuestReminder, VrGame::HudSubtitles };

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
    InterfaceCullFn   g_originalInterfaceCull   = reinterpret_cast<InterfaceCullFn>(VrGame::InterfaceCull);
    AccumTaskFn       g_originalAccumTask       = reinterpret_cast<AccumTaskFn>(VrGame::AddAccumTask);
    PlaceCameraFn     g_originalPlaceCamera     = reinterpret_cast<PlaceCameraFn>(VrGame::PlaceCamera);
    CameraUpdateFn    g_originalCameraUpdate    = reinterpret_cast<CameraUpdateFn>(VrGame::CameraUpdateWorldData);

  }


  VrStereoRenderer::VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device,
    bool showPreview, const VrPanelConfig& panel)
  : m_backend(backend), m_device(device), m_showPreview(showPreview), m_panelConfig(panel),
    m_controls(backend) { }


  VrStereoRenderer::~VrStereoRenderer() {
    // The patched calls stay in place and fall through to the game's own code
    g_stereoRenderer = nullptr;
    hidePanel();
    hideHud();
  }


  std::unique_ptr<VrStereoRenderer> VrStereoRenderer::install(
          IVRBackend&           backend,
          IDirect3DDevice9*     device,
          bool                  showPreview,
    const VrPanelConfig&        panel,
    const VrTurnConfig&         turn) {
    if (g_stereoRenderer)
      return nullptr;

    // Every hook forwards to the game's own function while no renderer is
    // active, so a patch that stays in place after a later one failed is inert
    bool patched = VrGameMemory::redirectCall(VrGame::SwapCallSite, VrGame::Swap,
      reinterpret_cast<const void*>(&VrStereoRenderer::swapHook));

    patched = patched && VrGameMemory::redirectCall(VrGame::RenderInterfaceCallSite,
      VrGame::RenderInterface, reinterpret_cast<const void*>(&VrStereoRenderer::renderInterfaceHook));

    patched = patched && VrGameMemory::redirectCall(VrGame::InterfaceCullCallSite,
      VrGame::InterfaceCull, reinterpret_cast<const void*>(&VrStereoRenderer::interfaceCullHook));

    patched = patched && VrGameMemory::redirectCall(VrGame::InterfaceAccumTaskCallSite,
      VrGame::AddAccumTask, reinterpret_cast<const void*>(&VrStereoRenderer::accumTaskHook));

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

    renderer->m_hudAvailable = renderer->m_hudLayers.initialize();

    if (!renderer->m_hudAvailable)
      Logger::info("VR: The HUD layout was not found, the HUD is not shown in the headset");

    if (!renderer->m_menuScenes.initialize())
      Logger::info("VR: The menu scene functions were not found, menus such as lockpicking show no 3D model in the headset");

    renderer->m_menuBackgroundFound =VrGameMemory::matches(VrGame::StaticMenuBackgroundReadSite,
        VrGame::StaticMenuBackgroundRead, sizeof(VrGame::StaticMenuBackgroundRead))
      && VrGameMemory::readable(VrGame::StaticMenuBackground, 1);

    if (!renderer->m_menuBackgroundFound)
      Logger::info("VR: The menu background setting was not found, menus may show a frozen image of the world");

    renderer->m_controls.setHeadLook(VrHeadLook::install(turn));

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
    if (g_stereoRenderer) {
      g_stereoRenderer->captureEye();

      if (g_stereoRenderer->renderLayer(interfaceManager, arg0, arg1))
        return;
    }

    g_originalRenderInterface(interfaceManager, nullptr, arg0, arg1);
  }


  void __cdecl VrStereoRenderer::interfaceCullHook(
          void*                 camera,
          void*                 sceneGraph,
          void*                 data) {
    if (g_stereoRenderer)
      g_stereoRenderer->isolateHud();

    g_originalInterfaceCull(camera, sceneGraph, data);
  }


  void __fastcall VrStereoRenderer::accumTaskHook(
          void*                 manager,
          void*                 unused,
          uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
          uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) {
    if (g_stereoRenderer)
      g_stereoRenderer->isolateHud();

    g_originalAccumTask(manager, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8);
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
    disableStaticMenuBackground();

    VrGameStateKind state = readGameState();

    if (showsPanel(state)) {
      m_layer = InterfaceLayer::None;
      hideHud();
      renderPanelFrame(main, state);
      return;
    }

    m_layer = chooseLayer(state);
    m_layerRendered = false;

    // The panel is placed again when it switches from the whole frame to a
    // menu drawn in game
    if (m_layer != InterfaceLayer::Menu || !m_panelShowsMenu)
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

    m_controls.update(input, state, m_hasReference ? &m_reference : nullptr,
      float(timing.predictedPeriod) * 1e-9f);

    auto views = m_backend.locateViews(timing.predictedDisplayTime);

    m_gameCamera     = m_renderCamera.save();
    m_gameCameraPose = m_renderCamera.readPose();

    // The game turned its camera with the head already
    m_controls.adjustCamera(m_gameCameraPose);

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

    if (m_layerRendered && m_layer == InterfaceLayer::Hud)
      submitHud();
    else
      hideHud();

    // A menu keeps its last image for a frame where it was not drawn
    if (m_layerRendered && m_layer == InterfaceLayer::Menu)
      submitMenu();

    if (m_preview)
      m_preview->present(eyes);
  }


  void VrStereoRenderer::renderPanelFrame(void* main, VrGameStateKind state) {
    // The runtime draws the panel. The game draws one frame, which
    // onPresent copies to the panel. Waiting still paces the game to the
    // headset and provides the head pose the panel is placed with.
    VrFrameTiming timing = m_backend.waitFrame();
    VrInputState input = m_backend.pollInput(timing.predictedDisplayTime);
    trackHead(input);

    m_controls.update(input, state, m_hasReference ? &m_reference : nullptr,
      float(timing.predictedPeriod) * 1e-9f);

    m_backend.submitEmptyFrame(timing.predictedDisplayTime);

    g_originalSwap(main, nullptr);
  }


  void VrStereoRenderer::trackHead(const VrInputState& input) {
    if (!input.isHeadTracked)
      return;

    m_headPose    = input.headPose;
    m_hasHeadPose = true;
  }


  void VrStereoRenderer::disableStaticMenuBackground() {
    // The game reads the setting once at startup, possibly after the
    // renderer was installed, so the flag is cleared before every frame.
    // A frozen background is a single mono image, which looks flat and
    // stuck to the head in the headset.
    if (!m_menuBackgroundFound)
      return;

    auto flag = reinterpret_cast<volatile uint8_t*>(VrGame::StaticMenuBackground);

    if (!*flag)
      return;

    *flag = 0;

    if (!m_loggedMenuBackground) {
      Logger::info("VR: Disabled the static menu background, the world stays live behind menus");
      m_loggedMenuBackground = true;
    }
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

    m_hudIsolated = false;

    applyEyePose();
    g_originalSwap(main, nullptr);

    // Normally restored right after the interface pass
    m_hudLayers.restore();

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



  VrStereoRenderer::InterfaceLayer VrStereoRenderer::chooseLayer(VrGameStateKind state) const {
    if (m_interfaceFailed)
      return InterfaceLayer::None;

    switch (state) {
      case VrGameStateKind::InGame:
        return m_hudAvailable ? InterfaceLayer::Hud : InterfaceLayer::None;
      case VrGameStateKind::Menu:
        return InterfaceLayer::Menu;
      default:
        return InterfaceLayer::None;
    }
  }


  bool VrStereoRenderer::renderLayer(
          void*                 interfaceManager,
          void*                 arg0,
          uint32_t              arg1) {
    if (m_eye != 0 || m_layer == InterfaceLayer::None || !createInterfaceTextures())
      return false;

    IDirect3DTexture9* texture = m_layer == InterfaceLayer::Hud
      ? m_hudTexture.ptr() : m_menuTexture.ptr();

    Com<IDirect3DSurface9> layerSurface;
    Com<IDirect3DSurface9> renderTarget;
    Com<IDirect3DSurface9> depthStencil;

    if (FAILED(texture->GetSurfaceLevel(0, &layerSurface))
     || FAILED(m_device->GetRenderTarget(0, &renderTarget)))
      return false;

    // The interface draws without depth, and the game's depth buffer may not
    // match the texture's sample count
    m_device->GetDepthStencilSurface(&depthStencil);
    m_device->SetDepthStencilSurface(nullptr);
    m_device->SetRenderTarget(0, layerSurface.ptr());

    if (m_layer == InterfaceLayer::Menu && m_menuScenes.isAnyOpen())
      renderMenuScenes();
    else
      m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    // The other HUD groups were culled by isolateHud when the interface
    // was culled, which may run on a worker thread that this pass waits for
    g_originalRenderInterface(interfaceManager, nullptr, arg0, arg1);
    m_hudLayers.restore();

    Com<IDirect3DSurface9> usedTarget;
    m_device->GetRenderTarget(0, &usedTarget);
    bool kept = usedTarget == layerSurface;
    // Menus are shown even if the HUD could not be hidden: the game hides
    // most of it while a menu is open
    m_layerRendered = kept && (m_hudIsolated || m_layer == InterfaceLayer::Menu);

    m_device->SetRenderTarget(0, renderTarget.ptr());
    m_device->SetDepthStencilSurface(depthStencil.ptr());

    if (!m_layerRendered && !m_loggedHudFailure) {
      Logger::err(kept
        ? "VR: The interface pass did not cull the HUD, the HUD is not shown in the headset"
        : "VR: The interface pass changed the render target, the HUD and menus are not shown in the headset");
      m_loggedHudFailure = true;
    }

    return true;
  }


  void VrStereoRenderer::renderMenuScenes() {
    // The panel stays transparent around the scene, so that the live world
    // is seen behind it as with the other menus
    m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    if (!createMenuSceneResources())
      return;

    m_device->SetDepthStencilSurface(m_menuDepth.ptr());
    m_device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    m_menuScenes.render();

    Com<IDirect3DSurface9> usedDepth;
    m_device->GetDepthStencilSurface(&usedDepth);
    bool depthKept = usedDepth == m_menuDepth;

    if (!depthKept && !m_loggedMenuDepthChange) {
      Logger::warn("VR: A menu scene changed the depth buffer, it is shown on an opaque background");
      m_loggedMenuDepthChange = true;
    }

    m_device->SetDepthStencilSurface(m_menuDepth.ptr());
    makeMenuSceneOpaque(depthKept);
    m_device->SetDepthStencilSurface(nullptr);
  }


  void VrStereoRenderer::makeMenuSceneOpaque(bool depthTested) {
    // The alpha written by the scene's shaders is not usable, so it is set
    // to one wherever the scene wrote depth, with a quad at the far plane
    D3DSURFACE_DESC desc = { };
    m_menuTexture->GetLevelDesc(0, &desc);

    m_menuSceneState->Capture();

    D3DVIEWPORT9 viewport = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
    m_device->SetViewport(&viewport);
    m_device->SetVertexShader(nullptr);
    m_device->SetPixelShader(nullptr);
    m_device->SetTexture(0, nullptr);
    m_device->SetFVF(D3DFVF_XYZRHW);

    m_device->SetRenderState(D3DRS_ZENABLE,           depthTested ? D3DZB_TRUE : D3DZB_FALSE);
    m_device->SetRenderState(D3DRS_ZFUNC,             D3DCMP_GREATER);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
    m_device->SetRenderState(D3DRS_STENCILENABLE,     FALSE);
    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_FOGENABLE,         FALSE);
    m_device->SetRenderState(D3DRS_LIGHTING,          FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE,  D3DCOLORWRITEENABLE_ALPHA);
    m_device->SetRenderState(D3DRS_TEXTUREFACTOR,     0xFFFFFFFF);

    m_device->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    // Pre-transformed vertices, offset by half a pixel to cover whole pixels
    float right  = float(desc.Width)  - 0.5f;
    float bottom = float(desc.Height) - 0.5f;
    float quad[4][4] = {
      { -0.5f,  -0.5f,  1.0f, 1.0f },
      { right,  -0.5f,  1.0f, 1.0f },
      { -0.5f,  bottom, 1.0f, 1.0f },
      { right,  bottom, 1.0f, 1.0f },
    };

    m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));

    m_menuSceneState->Apply();
  }


  bool VrStereoRenderer::createMenuSceneResources() {
    if (m_menuDepth != nullptr)
      return true;

    if (m_menuDepthFailed)
      return false;

    D3DSURFACE_DESC desc = { };
    m_menuTexture->GetLevelDesc(0, &desc);

    if (FAILED(m_device->CreateDepthStencilSurface(desc.Width, desc.Height, D3DFMT_D24S8,
        D3DMULTISAMPLE_NONE, 0, TRUE, &m_menuDepth, nullptr))
     || FAILED(m_device->CreateStateBlock(D3DSBT_ALL, &m_menuSceneState))) {
      Logger::err(str::format("VR: Failed to create the ", desc.Width, "x", desc.Height,
        " menu depth buffer or its state block, the 3D scenes of menus are not shown in the headset"));
      m_menuDepth = nullptr;
      m_menuSceneState = nullptr;
      m_menuDepthFailed = true;
      return false;
    }

    return true;
  }


  void VrStereoRenderer::isolateHud() {
    // Only the left eye's interface pass goes to the panels. The flags stay
    // set until that pass is drawn, see renderLayer. Menus are drawn
    // without any HUD group.
    if (m_eye != 0 || m_layer == InterfaceLayer::None || !m_hudAvailable || m_hudIsolated)
      return;

    m_hudIsolated = m_layer == InterfaceLayer::Hud
      ? m_hudLayers.isolate(HeadHudGroups, std::size(HeadHudGroups))
      : m_hudLayers.isolate(nullptr, 0);
  }


  bool VrStereoRenderer::createInterfaceTextures() {
    if (m_hudTexture != nullptr)
      return true;

    D3DSURFACE_DESC desc = { };
    m_gameTarget->GetDesc(&desc);

    // One texture per panel, since a panel keeps showing its last image.
    // The HUD is drawn into one texture and rearranged into the panel's.
    bool created = SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_hudTexture, nullptr))
      && SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_hudPanelTexture, nullptr))
      && SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_menuTexture, nullptr));

    if (!created) {
      Logger::err(str::format("VR: Failed to create the ", desc.Width, "x", desc.Height,
        " interface textures, the HUD and menus are not shown in the headset"));
      m_hudTexture  = nullptr;
      m_hudPanelTexture = nullptr;
      m_menuTexture = nullptr;
      m_interfaceFailed = true;
    }

    return created;
  }


  bool VrStereoRenderer::composeHud() {
    // The game puts the messages and objectives in the top left corner and
    // the subtitles at the bottom, in the centre. Seen from the head, the
    // corner is too far to the side to read comfortably, so the upper half
    // of the image is moved towards the centre and the lower half is kept.
    Com<IDirect3DSurface9> source;
    Com<IDirect3DSurface9> destination;

    if (FAILED(m_hudTexture->GetSurfaceLevel(0, &source))
     || FAILED(m_hudPanelTexture->GetSurfaceLevel(0, &destination)))
      return false;

    D3DSURFACE_DESC desc = { };
    source->GetDesc(&desc);

    LONG width  = LONG(desc.Width);
    LONG height = LONG(desc.Height);
    LONG split  = height / 2;

    float shiftScale = m_panelConfig.hudWidth > 0.0f
      ? m_panelConfig.hudMessagesOffset / m_panelConfig.hudWidth : 0.0f;
    LONG shift = std::clamp(LONG(std::lround(shiftScale * float(width))), LONG(0), width / 2);

    RECT lower       = { 0,     split, width,         height };
    RECT upperSource = { 0,     0,     width - shift, split  };
    RECT upperTarget = { shift, 0,     width,         split  };

    return SUCCEEDED(m_device->ColorFill(destination.ptr(), nullptr, D3DCOLOR_ARGB(0, 0, 0, 0)))
        && SUCCEEDED(m_device->StretchRect(source.ptr(), &lower, destination.ptr(), &lower, D3DTEXF_NONE))
        && SUCCEEDED(m_device->StretchRect(source.ptr(), &upperSource, destination.ptr(), &upperTarget, D3DTEXF_NONE));
  }


  void VrStereoRenderer::submitHud() {
    if (!composeHud()) {
      if (!m_loggedHudFailure) {
        Logger::err("VR: Failed to compose the HUD panel image");
        m_loggedHudFailure = true;
      }

      hideHud();
      return;
    }

    // In front of the eyes and slightly below them, facing the head
    VrPose pose;
    pose.position.y = m_panelConfig.hudHeight;
    pose.position.z = -m_panelConfig.hudDistance;

    m_hudShown = VrD3D9Bridge::submitPanel(m_device, m_backend, VrPanelId::HudHead,
      m_hudPanelTexture.ptr(), pose, m_panelConfig.hudWidth, VrPanelAnchor::Head);

    if (!m_hudShown && !m_loggedHudFailure) {
      Logger::err("VR: The backend rejected the HUD panel");
      m_loggedHudFailure = true;
    }
  }


  void VrStereoRenderer::submitMenu() {
    // Placed once when it appears, and again once the head is known
    if (!m_panelShown || (!m_panelPlacedWithHead && m_hasHeadPose))
      placePanel(m_panelConfig.hudDistance);

    m_panelShowsMenu = true;

    m_panelShown = VrD3D9Bridge::submitPanel(m_device, m_backend, VrPanelId::Menu,
      m_menuTexture.ptr(), m_panelPose, m_panelConfig.hudWidth, VrPanelAnchor::Room);

    if (!m_panelShown && !m_loggedPanelFailure) {
      Logger::err("VR: The backend rejected the menu panel");
      m_loggedPanelFailure = true;
    }
  }


  void VrStereoRenderer::hideHud() {
    if (!m_hudShown)
      return;

    m_backend.hidePanel(VrPanelId::HudHead);
    m_hudShown = false;
  }


  void VrStereoRenderer::updatePanel(IDirect3DSwapChain9* swapchain) {
    // Other states are handled by renderFrame, which also runs when the
    // game draws a single frame in them
    if (!showsPanel(readGameState()) || m_panelFailed)
      return;

    // Placed again when it switches from a menu drawn in game to the whole frame
    if (m_panelShowsMenu)
      hidePanel();

    m_panelShowsMenu = false;

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
      placePanel(m_panelConfig.distance);

    m_panelShown = VrD3D9Bridge::submitPanel(m_device, m_backend, VrPanelId::Menu,
      m_panelTexture.ptr(), m_panelPose, m_panelConfig.width, VrPanelAnchor::Room);

    if (!m_panelShown && !m_loggedPanelFailure) {
      Logger::err("VR: The backend rejected the panel");
      m_loggedPanelFailure = true;
    }
  }


  void VrStereoRenderer::placePanel(float distance) {
    VrPose head;
    head.position.y = DefaultHeadHeight;

    if (m_hasHeadPose)
      head = m_headPose;

    m_panelPose = vrComputePanelPose(head, distance);
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
