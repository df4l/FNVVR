#include <algorithm>
#include <cmath>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_d3d9_bridge.h"
#include "vr_game_addresses.h"
#include "vr_game_ammo.h"
#include "vr_game_console.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_head_hud.h"
#include "vr_math.h"
#include "vr_stereo_renderer.h"
#include "vr_weapon_types.h"

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
    constexpr uintptr_t HeadHudGroups[] = {
      VrGame::HudMessages, VrGame::HudQuestReminder, VrGame::HudSubtitles,
      VrGame::HudRadiationMeter, VrGame::HudXpMeter, VrGame::HudSneakMeter };

    // HUD groups shown on the hands
    constexpr uintptr_t HandHudGroups[] = {
      VrGame::HudHitPoints, VrGame::HudActionPoints, VrGame::HudAmmoTypeLabel,
      VrGame::HudConditionArrows, VrGame::HudInfo };

    // Head pose used to place the panel before the headset reported one:
    // standing at the origin and looking ahead
    constexpr float DefaultHeadHeight = 1.6f;

    // Head rotation the culling view allows for on every side, in radians:
    // the game culls with the head pose of the previous frame
    constexpr float CullingMargin = 0.1745329f;

    // Steepest slope of the line from the head to a dialogue's speaker that
    // the panel follows, in radians
    constexpr float MaxSpeakerPanelPitch = 0.7853982f;

    // Frames, counted once the Pip-Boy is closed, during which the game's
    // Pip-Boy control cannot reopen it after the gaze pressed back
    constexpr uint32_t PipBoyBackReleaseFrames = 3;

    // Pulse on the right controller when a melee swing hits, stronger for
    // a power attack, with its duration in nanoseconds
    constexpr float   MeleeHitHaptic      = 0.5f;
    constexpr float   MeleePowerHaptic    = 1.0f;
    constexpr int64_t MeleeHapticDuration = 40'000'000;

    // Short tick on the right controller when a power attack is charged
    constexpr float   ChargedHaptic         = 0.3f;
    constexpr int64_t ChargedHapticDuration = 15'000'000;

    constexpr uint32_t MaxLoggedPowerHits = 200;

    // Pulses of a throw by hand: a faint one on the left controller when it
    // takes the pin or the lighter reaches the fuse, a firm one when the
    // weapon is armed, and one on the right controller when it leaves
    constexpr float   ThrowTouchHaptic    = 0.15f;
    constexpr float   ThrowArmedHaptic    = 0.6f;
    constexpr float   ThrowReleaseHaptic  = 0.4f;
    constexpr int64_t ThrowHapticDuration = 25'000'000;

    // Pulses of the manual reload: on the left controller when it takes a
    // magazine, on both when the magazine goes in, a faint one when the
    // magazine enters or leaves its well, and a long dull one when no
    // rounds are left behind the back, unlike any other
    constexpr float   MagazineTakenHaptic    = 0.4f;
    constexpr float   MagazineInsertedHaptic = 0.7f;
    constexpr float   MagazineFaintHaptic    = 0.15f;
    constexpr int64_t MagazineHapticDuration = 25'000'000;
    constexpr float   NoRoundsHaptic         = 0.3f;
    constexpr int64_t NoRoundsHapticDuration = 150'000'000;

    // Pulses of the right hand at the holster: a light one when it gets
    // there, a firm one when the weapon goes in or comes out
    constexpr float   HolsterReachedHaptic   = 0.15f;
    constexpr float   HolsterHaptic          = 0.6f;
    constexpr int64_t HolsterHapticDuration  = 25'000'000;

    // A hand taking hold of an object, and letting go of it
    constexpr float   GrabHaptic             = 0.4f;
    constexpr float   ReleaseHaptic          = 0.1f;
    constexpr int64_t GrabHapticDuration     = 20'000'000;

    // The quick wheel opens with the right stick pushed this far down, and
    // stays open while it is pushed at least this far any way
    constexpr float   WheelOpenDeflection    = 0.6f;
    constexpr float   WheelKeepDeflection    = 0.3f;

    // Pulses of the quick wheel and of the ammunition in the left hand:
    // a tick for each cell or type, a firm one when it is used, a long dull
    // one when it is refused
    constexpr float   QuickTickHaptic        = 0.12f;
    constexpr int64_t QuickTickDuration      = 12'000'000;
    constexpr float   QuickUsedHaptic        = 0.5f;
    constexpr int64_t QuickUsedDuration      = 30'000'000;

    /**
     * \brief Checks whether a state has no 3D scene and is shown on the panel
     */
    bool showsPanel(VrGameStateKind state) {
      return state == VrGameStateKind::MainMenu
          || state == VrGameStateKind::Loading;
    }

    /**
     * \brief Converts a rectangle in interface units to pixels of an image
     */
    RECT interfaceToPixels(const VrInterfaceRect& rect, float scale, const D3DSURFACE_DESC& desc) {
      return {
        std::clamp(LONG(std::lround(rect.left   * scale)), LONG(0), LONG(desc.Width)),
        std::clamp(LONG(std::lround(rect.top    * scale)), LONG(0), LONG(desc.Height)),
        std::clamp(LONG(std::lround(rect.right  * scale)), LONG(0), LONG(desc.Width)),
        std::clamp(LONG(std::lround(rect.bottom * scale)), LONG(0), LONG(desc.Height)) };
    }

    VrInterfaceRect pixelsToRect(const RECT& rect) {
      return { float(rect.left), float(rect.top), float(rect.right), float(rect.bottom) };
    }

    RECT rectToPixels(const VrInterfaceRect& rect) {
      return { LONG(rect.left), LONG(rect.top), LONG(rect.right), LONG(rect.bottom) };
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
    hideHandHud();
  }


  std::unique_ptr<VrStereoRenderer> VrStereoRenderer::install(
          IVRBackend&           backend,
          IDirect3DDevice9*     device,
          bool                  showPreview,
    const VrPanelConfig&        panel,
    const VrTurnConfig&         turn,
          bool                  weaponInHand,
    const VrGameTimerConfig&    timer,
    const VrRenderPassConfig&   passes,
    const VrDeveloperConfig&    developer,
    const VrSwingConfig&        swing) {
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
    renderer->m_tileTextureRetry.initialize();

    if (!renderer->m_hudAvailable)
      Logger::info("VR: The HUD layout was not found, the HUD is not shown in the headset");
    else if (!renderer->m_hudLayers.menusFound())
      Logger::info("VR: The menu list was not found, HUD messages are not shown while a menu is open");

    renderer->m_hudMetersAvailable = renderer->m_hudAvailable && renderer->m_hudLayers.rectsFound()
      && renderer->m_hudMeters.initialize();

    if (renderer->m_hudAvailable && !renderer->m_hudMetersAvailable)
      Logger::info("VR: The radiation, XP and sneak meters' layout was not found, they are shown where the game places them on the HUD panel");

    renderer->m_handHudAvailable = renderer->m_hudAvailable && renderer->m_hudLayers.rectsFound();
    renderer->m_promptAvailable  = renderer->m_handHudAvailable
      && renderer->m_activateTarget.initialize();

    if (!renderer->m_handHudAvailable)
      Logger::info("VR: The health bar, compass, action points, ammunition and interaction prompt are not shown in the headset");
    else if (!renderer->m_promptAvailable)
      Logger::info("VR: The activation target was not found, the interaction prompt is not shown in the headset");
    else if (!renderer->m_promptIcon.initialize())
      Logger::info("VR: The prompt's button icon was not found, the prompt shows the activate button at both hands");

    if (!renderer->m_menuScenes.initialize())
      Logger::info("VR: The menu scene functions were not found, menus such as lockpicking show no 3D model in the headset");

    renderer->m_menuBackgroundFound =VrGameMemory::matches(VrGame::StaticMenuBackgroundReadSite,
        VrGame::StaticMenuBackgroundRead, sizeof(VrGame::StaticMenuBackgroundRead))
      && VrGameMemory::readable(VrGame::StaticMenuBackground, 1);

    if (!renderer->m_menuBackgroundFound)
      Logger::info("VR: The menu background setting was not found, menus may show a frozen image of the world");

    renderer->m_controls.setHeadLook(VrHeadLook::install(turn));

    auto sneak = VrSneak::install();

    if (!sneak)
      Logger::info("VR: The player's sneak state was not found, the player sneaks with the sneak button only");

    renderer->m_controls.setSneak(std::move(sneak));

    if (weaponInHand)
      renderer->m_weaponHand = VrWeaponHand::install();

    // The blade is measured where the weapon is drawn in the hand
    if (renderer->m_weaponHand)
      renderer->m_meleeSwing = VrMeleeSwing::install(swing);

    if (renderer->m_weaponHand)
      renderer->m_handThrow = VrHandThrow::install(*renderer->m_weaponHand);

    if (!renderer->m_handThrow)
      Logger::info("VR: Grenades and thrown weapons are thrown by the game's animation, as with a gamepad");

    if (!renderer->m_meleeSwing)
      Logger::info("VR: Melee weapons are swung by the game's animation, as with a gamepad");
    else
      renderer->m_powerRules = VrPowerAttackRules::install(swing.powerCost);

    if (renderer->m_meleeSwing && !renderer->m_powerRules)
      Logger::warn("VR: The game's power attack rules were not found, melee swings are never power attacks");

    renderer->m_painter    = std::make_unique<VrPainter>(device);
    renderer->m_chargeRing = std::make_unique<VrChargeRing>(*renderer->m_painter, backend);
    renderer->m_quickWheel = std::make_unique<VrQuickWheel>(*renderer->m_painter, backend, renderer->m_gameImages);
    renderer->m_ammoLabel  = std::make_unique<VrAmmoLabel>(*renderer->m_painter, backend, renderer->m_gameImages);

    if (VrGameConsole::install())
      renderer->m_terminal = std::make_unique<VrConsoleTerminal>(*renderer->m_painter, backend);

    renderer->m_handPick = VrHandActivatePick::install();

    if (!renderer->m_handPick)
      Logger::info("VR: The activation pick was not found, the activation target follows the view");

    renderer->m_skyAnchor = VrSkyAnchor::install();

    if (!renderer->m_skyAnchor)
      Logger::warn("VR: The sky placement was not found, the sky is drawn around the camera instead of each eye");

    VrStereoRenderer* self = renderer.get();
    renderer->m_cullingCamera = VrCullingCamera::install(
      [self] (VrGameCameraPose& pose, VrGameFrustum& frustum) { return self->cullingView(pose, frustum); });

    // The gaze finds the Pip-Boy's screen through the free left hand's pose
    renderer->m_pipBoyGazeAvailable = renderer->m_weaponHand && renderer->m_pipBoyControl.initialize();

    if (!renderer->m_pipBoyGazeAvailable)
      Logger::info("VR: The Pip-Boy does not open when the left forearm is looked at");

    renderer->m_pipBoyRender = VrPipBoyRender::install(
      [self] (const VrVector3* shift) { self->setScreenPass(shift); });

    renderer->m_pipBoyAnimation = VrPipBoyAnimation::install();

    if (!renderer->m_pipBoyAnimation)
      Logger::info("VR: The Pip-Boy animation waits were not found, the arm is raised and lowered as in the game");

    renderer->m_weaponDrawn = VrWeaponDrawn::install();

    if (!renderer->m_weaponDrawn)
      Logger::info("VR: The weapon draw code was not found, the weapon is drawn and holstered as in the game");

    renderer->m_comfortSettings = VrComfortSettings::install();

    if (!renderer->m_comfortSettings)
      Logger::info("VR: The idle and kill camera settings were not found, the game may leave the first person");

    renderer->m_npcGaze = VrNpcGaze::install();

    if (!renderer->m_npcGaze)
      Logger::info("VR: The player's look-at location was not found, actors look at the game's camera");

    renderer->m_manualReload = VrManualReload::install();

    if (!renderer->m_manualReload)
      Logger::info("VR: The player's reload was not found, the weapons are reloaded as in the game");

    renderer->m_physicsGrab = VrPhysicsGrab::install();

    if (renderer->m_physicsGrab) {
      renderer->m_controls.setGrabByHands(true);
      renderer->m_npcReaction = VrNpcReaction::install();

      if (!renderer->m_npcReaction)
        Logger::info("VR: The game's comments on the player's actions were not found, grabbed actors do not react");

      renderer->m_physicsGrab->setNpcReaction(renderer->m_npcReaction.get());
    } else
      Logger::info("VR: The game's grab was not found, the right grip grabs as the game's grab key");

    if (renderer->m_manualReload && renderer->m_weaponHand) {
      renderer->m_partHighlight = VrPartHighlight::install(
        [self] (uint8_t*& root, std::vector<uint8_t*>& parts, VrGameWorldTransforms& drawn) {
          return self->m_weaponHand->takeHighlight(root, parts, drawn);
        });

      if (!renderer->m_partHighlight)
        Logger::info("VR: The game's V.A.T.S. highlight was not found, the weapon's parts to work do not glow");
    }

    if (developer.enabled) {
      renderer->m_developerMode = VrDeveloperMode::install(developer.weaponTypes);

      if (renderer->m_developerMode) {
        std::string types;

        for (uint32_t type = 0; type < VrWeaponTypeCount; type++) {
          if (developer.weaponTypes & (1u << type))
            types += str::format(types.empty() ? "" : ", ", vrWeaponTypeName(type));
        }

        Logger::info(str::format("VR: Developer mode is on: god mode, unlimited ammunition and the weapons of type ",
          types.empty() ? "(none)" : types, " after each loading screen"));
      } else
        Logger::warn("VR: The functions the developer mode calls were not found, it is off");
    }

    renderer->m_gameTimer = VrGameTimer::install(timer);

    renderer->m_renderPasses = VrRenderPasses::install(passes,
      [self] (VrGameCameraPose& pose, VrGameFrustum& frustum) { return self->sharedPassView(pose, frustum); });

    if (!renderer->m_renderPasses)
      Logger::info("VR: The world render's passes were not found, they are drawn for each eye and not timed");

    if (!renderer->m_gameTimer)
      Logger::info("VR: The game's frame timer update was not found, it is not measured");

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
    if (g_stereoRenderer && g_stereoRenderer->m_eyeView && g_stereoRenderer->m_weaponHand)
      g_stereoRenderer->m_weaponHand->beforePlaceCamera(main, g_stereoRenderer->m_eyePosition);

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


  VrPresentMode VrStereoRenderer::onPresent(IDirect3DSwapChain9* swapchain) {
    if (m_eye == 0)
      return VrPresentMode::Drop;

    bool paced = m_swapped && m_paced;

    if (m_eye == NoEye) {
      if (!m_swapped && m_mainThread == GetCurrentThreadId())
        updateBlockedInput();

      m_swapped = false;
      updatePanel(swapchain);
    }

    if (!paced)
      return VrPresentMode::Game;

    if (!m_loggedImmediatePresent) {
      Logger::info("VR: Frames paced by the headset are presented to the window without waiting for its vertical blank");
      m_loggedImmediatePresent = true;
    }

    return VrPresentMode::Immediate;
  }


  void VrStereoRenderer::renderFrame(void* main) {
    m_mainThread   = GetCurrentThreadId();
    m_swapped      = true;
    m_paced        = false;
    m_blockedInput = false;

    disableStaticMenuBackground();

    m_tileTextureRetry.update();

    VrGameStateKind state = readGameState();

    if (m_comfortSettings)
      m_comfortSettings->update(state == VrGameStateKind::InGame);

    if (m_developerMode)
      m_developerMode->update(state);

    if (m_manualReload)
      m_manualReload->update();

    if (showsPanel(state)) {
      m_layer = InterfaceLayer::None;
      hideHud();
      hideHandHud();
      m_powerCharge.reset();
      m_powerRefusal.reset();

      if (m_chargeRing)
        m_chargeRing->hide();

      hideQuickPanels();
      renderPanelFrame(main, state);
      return;
    }

    if (m_weaponDrawn && state == VrGameStateKind::InGame)
      m_weaponDrawn->update();

    m_layer = chooseLayer(state);
    m_hudRendered  = false;
    m_handHudRendered = false;
    m_menuRendered = false;
    m_overPipBoy   = state == VrGameStateKind::PipBoy;
    m_menuDrawn    = !m_overPipBoy;
    m_dialogueSpeaker = m_controls.takeDialogueSpeaker();

    // The panel is placed again when it switches from the whole frame to a
    // menu drawn in game
    if (m_layer != InterfaceLayer::Menu || !m_panelShowsMenu)
      hidePanel();

    if (m_texturesFailed || !m_renderCamera.acquire() || !createEyeTextures()) {
      g_originalSwap(main, nullptr);
      return;
    }

    VrFrameTiming timing = m_backend.waitFrame();
    VrD3D9Bridge::beginFrame(m_device, m_backend);
    m_paced = timing.paced;

    if (!timing.shouldRender) {
      VrD3D9Bridge::submitEmptyFrame(m_device, m_backend, timing.predictedDisplayTime);
      g_originalSwap(main, nullptr);
      return;
    }

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_gameTarget))) {
      VrD3D9Bridge::submitEmptyFrame(m_device, m_backend, timing.predictedDisplayTime);
      g_originalSwap(main, nullptr);
      return;
    }

    VrInputState input = m_backend.pollInput(timing.predictedDisplayTime);
    trackHead(input);

    if (!m_hasReference && input.isHeadTracked) {
      m_reference    = vrMakeRecenterReference(input.headPose);
      m_hasReference = true;
    }

    // The weapon hand decided on the previous frame whether the game aims,
    // the melee guard whether it blocks (the same control), and the gaze
    // whether the Pip-Boy closes
    if (m_weaponHand)
      m_controls.setGameAim(m_weaponHand->wantsGameAim() || (m_meleeSwing && m_meleeSwing->wantsBlock()));

    m_controls.setSwingsWithHand((m_meleeSwing && m_meleeSwing->swingsWithHand())
      || (m_handThrow && m_handThrow->throwsWithHand()));

    m_controls.setMenuBack(m_closePipBoy);

    // The prompt's hand was chosen on the previous frame too
    m_controls.setPromptAtLeftHand(m_promptShown && m_promptAt == VrHand::Left);

    // The right grip switches the Pip-Boy light while the right hand is at it
    m_rightHandAtPipBoy = isRightHandAtPipBoy(input, state);
    m_controls.setRightHandAtPipBoy(m_rightHandAtPipBoy ? m_pipBoyControl.lightHoldTime() : 0.0f);

    // The right grip holsters or draws at the holster, and the trigger does
    // not draw the holstered weapon
    m_controls.setRightHandAtHolster(m_holster.atHolster());

    // X changes the ammunition in the left hand, the quick wheel holds the
    // right stick
    m_controls.setReloadTaken(m_ammoInHand);
    m_controls.setRightStickTaken(m_quickWheel && m_quickWheel->isOpen());

    // The Pip-Boy inventory's hotkey wheel takes the navigation stick
    m_controls.setHotKeyWheelShown(state == VrGameStateKind::PipBoy && VrGameHotKeys::inventoryWheelShown());
    m_controls.setHandsFree(m_weaponDrawn && m_weaponDrawn->playerHolstered());

    float dt = float(timing.predictedPeriod) * 1e-9f;

    updateControls(input, state, true, dt);

    if (m_controls.switchesPipBoyLight() && !m_loggedPipBoyLight) {
      Logger::info("VR: The right grip at the Pip-Boy holds the Pip-Boy control to switch the light");
      m_loggedPipBoyLight = true;
    }

    auto views = m_backend.locateViews(timing.predictedDisplayTime);

    m_cullingFov    = vrComputeCullingFov(views[0].fov, views[1].fov, CullingMargin);
    m_hasCullingFov = true;

    m_gameCamera     = m_renderCamera.save();
    logGameFrustum();
    m_gameCameraPose = m_renderCamera.readPose();

    // The game placed its first-person model for its own camera
    VrGameCameraPose modelCamera = m_gameCameraPose;

    // The game turned its camera with the head already
    m_controls.updateCamera(m_gameCameraPose, dt);
    updateNpcGaze();
    updateHolster(input, state);
    updateQuickWheel(input, state);
    updateWeaponHand(input, state, modelCamera, dt);
    updateManualReload(input, state, dt);
    updateAmmoChoice(input, state);
    updateMeleeSwing(input, state, dt);
    updatePunches(input, state, dt);
    updateHandThrow(input, state, dt);
    updatePhysicsGrab(input, state);
    updateHandPick(input, state);
    updatePipBoyGaze(input, state, dt);

    // Before the interface's tiles are updated for this frame
    D3DSURFACE_DESC targetDesc = { };
    m_gameTarget->GetDesc(&targetDesc);

    m_hudMetersPlaced = m_hudMetersAvailable
      ? m_hudMeters.place(vrComputeInterfaceSize(targetDesc.Width, targetDesc.Height).y)
      : VrHudMetersPlaced();

    for (uint32_t eye = 0; eye < VrEyeCount; eye++) {
      m_eyeCopied[eye] = false;
      renderEye(eye, views[eye], main);
    }

    if (m_renderPasses)
      m_renderPasses->endFrame();

    m_renderCamera.restore(m_gameCamera);
    m_gameTarget = nullptr;

    if (!m_eyeCopied[0] || !m_eyeCopied[1]) {
      if (!m_loggedFailure) {
        Logger::err("VR: The game did not reach the interface while drawing an eye, the frame is dropped");
        m_loggedFailure = true;
      }

      VrD3D9Bridge::submitEmptyFrame(m_device, m_backend, timing.predictedDisplayTime);
      return;
    }

    IDirect3DTexture9* eyes[VrEyeCount] = { m_eyeTextures[0].ptr(), m_eyeTextures[1].ptr() };

    if (!VrD3D9Bridge::submitStereoFrame(m_device, m_backend, eyes, views,
        timing.predictedDisplayTime) && !m_loggedFailure) {
      Logger::err("VR: The backend rejected a stereo frame");
      m_loggedFailure = true;
    }

    if (m_hudRendered)
      submitHud();
    else
      hideHud();

    if (m_handHudRendered)
      submitHandHud(input, dt);
    else
      hideHandHud();

    // A menu keeps its last image for a frame where it was not drawn. The
    // console's terminal stands in front of the menu it is opened from,
    // and the runtime would draw the menu over it.
    if (m_terminal && m_terminal->showsScreen())
      hidePanel();
    else if (m_menuRendered)
      submitMenu();
    else if (m_overPipBoy)
      hidePanel();

    m_menuOverPipBoy = m_overPipBoy && m_menuRendered;

    if (m_preview)
      m_preview->present(eyes);
  }


  void VrStereoRenderer::renderPanelFrame(void* main, VrGameStateKind state) {
    // The runtime draws the panel. The game draws one frame, which
    // onPresent copies to the panel. Waiting still paces the game to the
    // headset and provides the head pose the panel is placed with.
    VrFrameTiming timing = m_backend.waitFrame();
    VrD3D9Bridge::beginFrame(m_device, m_backend);
    m_paced = timing.paced;
    VrInputState input = m_backend.pollInput(timing.predictedDisplayTime);
    trackHead(input);

    updateControls(input, state, false, float(timing.predictedPeriod) * 1e-9f);

    // Nothing in the world can be pointed at without the stereo view
    if (m_handPick)
      m_handPick->setRays({ });

    // Nor held
    if (m_physicsGrab) {
      for (uint32_t i = 0; i < VrHandCount; i++)
        m_physicsGrab->setHand(VrHand(i), nullptr, false, false, false, false);
    }

    if (m_npcGaze)
      m_npcGaze->setHead(nullptr);

    VrD3D9Bridge::submitEmptyFrame(m_device, m_backend, timing.predictedDisplayTime);

    g_originalSwap(main, nullptr);
  }


  void VrStereoRenderer::updateControls(const VrInputState& input, VrGameStateKind state, bool stereo, float dt) {
    m_controls.setTerminalPanel(m_terminal ? m_terminal->placement() : nullptr);
    m_controls.update(input, state, m_hasReference ? &m_reference : nullptr, menuPanel(), dt);

    if (!m_terminal)
      return;

    VrPose head;
    head.position.y = DefaultHeadHeight;

    if (m_hasHeadPose)
      head = m_headPose;

    m_terminal->update(head, stereo, m_controls.terminalPointer(), m_controls.terminalBack(), dt);
  }


  void VrStereoRenderer::updateBlockedInput() {
    // A movie (the intro of a new game) runs a loop of its own, which polls
    // the gamepad to skip the movie but never reaches Main::Swap, so the
    // controllers would keep the state of the last game frame. They are
    // read here instead. The runtime is not waited on: the movie paces
    // itself, and the panel shows its frames.
    auto now = high_resolution_clock::now();

    float dt = m_blockedInput
      ? std::chrono::duration<float>(now - m_blockedInputTime).count()
      : 0.0f;

    if (!m_loggedBlockedInput) {
      Logger::info("VR: The game presents outside of its main loop, the controllers are read at each present");
      m_loggedBlockedInput = true;
    }

    m_blockedInputTime = now;
    m_blockedInput     = true;

    VrInputState input = m_backend.pollInput(0);
    trackHead(input);

    m_controls.update(input, readGameState(),
      m_hasReference ? &m_reference : nullptr, menuPanel(), dt);
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

    if (m_skyAnchor)
      m_skyAnchor->setEye(&m_eyePosition);

    if (m_renderPasses)
      m_renderPasses->beginEye(eye);

    g_originalSwap(main, nullptr);

    if (m_renderPasses)
      m_renderPasses->endEye();

    if (m_skyAnchor)
      m_skyAnchor->setEye(nullptr);

    // Normally restored right after the interface pass
    m_hudLayers.restore();

    m_eye     = NoEye;
    m_eyeView = nullptr;
  }


  void VrStereoRenderer::applyEyePose() {
    VrPose eyeInReference = m_hasReference
      ? vrComputeEyeInReference(m_reference, m_eyeView->pose)
      : VrPose();

    VrGameCameraPose pose = vrComputeEyeCameraPose(m_gameCameraPose, eyeInReference, VrGameUnitsPerMetre);
    m_eyePosition = pose.position;

    if (m_screenPass)
      pose.position = pose.position - m_screenShift;

    m_renderCamera.apply(pose, vrComputeGameFrustum(m_eyeView->fov));
  }


  void VrStereoRenderer::setScreenPass(const VrVector3* shift) {
    // The pass updates the camera itself, which re-applies the pose through
    // cameraUpdateHook, and restores it before it returns
    m_screenPass = shift != nullptr;

    if (shift)
      m_screenShift = *shift;

    if (m_eyeView)
      applyEyePose();
  }


  void VrStereoRenderer::updateWeaponHand(
    const VrInputState&         input,
          VrGameStateKind       state,
    const VrGameCameraPose&     gameCamera,
          float                 dt) {
    if (!m_weaponHand)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];

    // A hand that holds an object is drawn on it, its fingers closed on it
    std::array<VrGrabCorrection, VrHandCount> glue;
    bool holds = state == VrGameStateKind::InGame && m_physicsGrab;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrControllerState& controller = input.controllers[i];
      const VrFingerCurls* tracked = controller.isActive && controller.hasFingers ? &controller.fingerCurl : nullptr;

      VrFingerCurls curls;
      bool posed = m_grabFingers[i].update(tracked, holds ? m_physicsGrab->heldFingers(VrHand(i)) : nullptr, dt, curls);
      m_weaponHand->setFingers(VrHand(i), posed ? &curls : nullptr);
    }

    for (uint32_t i = 0; i < VrHandCount; i++)
      glue[i] = m_grabGlue[i].update(holds ? m_physicsGrab->handCorrection(VrHand(i)) : nullptr, dt);

    if (state == VrGameStateKind::PipBoy && m_hasReference && left.isActive) {
      VrGameCameraPose otherHand = handInGame(left);
      m_weaponHand->beginFrame(gameCamera, nullptr, &otherHand, false, VrFirstPersonMode::PipBoy, dt);
      return;
    }

    // Menus are pointed at with the lasers, and the game animates the arms
    // in front of its camera meanwhile. V.A.T.S. is driven by the sticks
    // and shows the weapon as the game does.
    if (state == VrGameStateKind::Menu && input.hasActions
     && !*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState)) {
      m_weaponHand->beginFrame(gameCamera, nullptr, nullptr, false, VrFirstPersonMode::Hidden, dt);
      return;
    }

    if (state != VrGameStateKind::InGame || !m_hasReference || !right.isActive) {
      m_weaponHand->beginFrame(gameCamera, nullptr, nullptr, false, VrFirstPersonMode::Hands, dt);
      return;
    }

    VrGameCameraPose hand      = vrGrabCorrectHand(handInGame(right), glue[uint32_t(VrHand::Right)]);
    VrGameCameraPose otherHand = vrGrabCorrectHand(handInGame(left), glue[uint32_t(VrHand::Left)]);

    VrFirstPersonMode mode = m_weaponDrawn && m_weaponDrawn->playerHolstered()
      ? VrFirstPersonMode::FreeHands
      : VrFirstPersonMode::Hands;

    m_weaponHand->beginFrame(gameCamera, &hand, left.isActive ? &otherHand : nullptr,
      leftGripOnWeapon(input), mode, dt);
  }


  void VrStereoRenderer::updateQuickWheel(const VrInputState& input, VrGameStateKind state) {
    if (!m_quickWheel)
      return;

    if (state != VrGameStateKind::InGame) {
      m_quickWheel->close();
      return;
    }

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    bool tracked = input.hasActions && right.isActive && m_hasHeadPose;

    const VrVector2& stick = input.actions.turn;

    VrQuickWheelInput wheel;
    wheel.head = tracked ? &m_headPose : nullptr;
    wheel.hand = tracked ? &right.gripPose : nullptr;
    wheel.manualReload = m_manualReload && m_manualReload->enabled();
    wheel.held = m_quickWheel->isOpen()
      ? std::sqrt(stick.x * stick.x + stick.y * stick.y) >= WheelKeepDeflection
      : stick.y <= -WheelOpenDeflection;

    switch (m_quickWheel->update(wheel)) {
      case VrQuickWheelEvent::Opened:
      case VrQuickWheelEvent::Picked:
        m_backend.applyHaptic(VrHand::Right, QuickTickHaptic, QuickTickDuration);
        break;

      case VrQuickWheelEvent::Used:
        m_backend.applyHaptic(VrHand::Right, QuickUsedHaptic, QuickUsedDuration);

        // A weapon chosen on the wheel comes into the hand
        if (m_quickWheel->drawsWeapon() && m_weaponDrawn)
          m_weaponDrawn->draw();
        break;

      case VrQuickWheelEvent::Failed:
        m_backend.applyHaptic(VrHand::Right, NoRoundsHaptic, NoRoundsHapticDuration);
        break;

      case VrQuickWheelEvent::Closed:
      case VrQuickWheelEvent::None:
        break;
    }
  }


  void VrStereoRenderer::updateAmmoChoice(const VrInputState& input, VrGameStateKind state) {
    bool inGame = state == VrGameStateKind::InGame && input.hasActions;
    bool down   = inGame && input.actions.isPressed(VrAction::Reload);
    bool pressed = down && !m_ammoButtonDown;
    m_ammoButtonDown = down;

    bool holds = inGame && m_manualReload && m_manualReload->holdsAmmo();

    // The press was kept from the game if the hand already held it
    if (pressed && holds && m_ammoInHand) {
      if (m_manualReload->cycleAmmo())
        m_backend.applyHaptic(VrHand::Left, QuickTickHaptic * 2.0f, QuickTickDuration);
      else
        m_backend.applyHaptic(VrHand::Left, NoRoundsHaptic, NoRoundsHapticDuration);
    }

    m_ammoInHand = holds;

    const VrControllerState& left = input.controllers[uint32_t(VrHand::Left)];

    if (!m_ammoLabel || !holds || !left.isActive || !m_hasHeadPose || !VrGameAmmo::available()) {
      if (m_ammoLabel)
        m_ammoLabel->hide();
      return;
    }

    // Only a weapon that takes several types says which one the hand holds
    std::vector<VrAmmoType> types = VrGameAmmo::types();
    const VrAmmoType* held = nullptr;
    bool more = false;

    for (const VrAmmoType& type : types) {
      if (type.form == m_manualReload->heldAmmo())
        held = &type;
      else if (type.held > 0)
        more = true;
    }

    if (types.size() < 2 || !held) {
      m_ammoLabel->hide();
      return;
    }

    m_ammoLabel->show(m_headPose, left.gripPose.position, held->name, held->held, more);
  }


  void VrStereoRenderer::hideQuickPanels() {
    if (m_quickWheel)
      m_quickWheel->close();

    if (m_ammoLabel)
      m_ammoLabel->hide();
  }


  void VrStereoRenderer::updateHolster(const VrInputState& input, VrGameStateKind state) {
    if (!m_weaponDrawn)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];

    int32_t type = -1;
    VrWeaponDrawn::equippedWeapon(type);

    VrHolsterInput holster;
    holster.active = state == VrGameStateKind::InGame && m_hasReference && m_hasHeadPose
      && right.isActive && input.hasActions && !m_rightHandAtPipBoy;
    holster.kind   = vrHolsterForWeaponType(type);
    holster.drawn  = !m_weaponDrawn->playerHolstered();
    holster.grip   = input.actions.isPressed(VrAction::Grab);

    if (holster.active) {
      holster.head = vrComputeEyeCameraPose(m_gameCameraPose,
        vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre);
      holster.hand = handInGame(right).position;
    }

    switch (m_holster.update(holster)) {
      case VrHolsterEvent::Entered:
        m_backend.applyHaptic(VrHand::Right, HolsterReachedHaptic, HolsterHapticDuration);
        break;

      case VrHolsterEvent::Holstered:
        m_weaponDrawn->holster();
        m_backend.applyHaptic(VrHand::Right, HolsterHaptic, HolsterHapticDuration);
        break;

      case VrHolsterEvent::Drawn:
        m_weaponDrawn->draw();
        m_backend.applyHaptic(VrHand::Right, HolsterHaptic, HolsterHapticDuration);
        break;

      case VrHolsterEvent::None:
        break;
    }
  }


  void VrStereoRenderer::updatePhysicsGrab(
    const VrInputState&         input,
          VrGameStateKind       state) {
    if (!m_physicsGrab)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];

    bool inGame = state == VrGameStateKind::InGame && m_hasReference && input.hasActions;

    // The right hand holds objects only while the weapon is holstered. A
    // press at the holster, at the Pip-Boy or with the quick wheel open
    // does something else, but an object held there stays held.
    bool rightBusy = m_holster.atHolster() || m_rightHandAtPipBoy || (m_quickWheel && m_quickWheel->isOpen());
    bool rightFree = m_weaponDrawn && m_weaponDrawn->playerHolstered()
      && (m_physicsGrab->holds(VrHand::Right) || !rightBusy);

    // The left hand is free unless it holds the weapon, arms a grenade or
    // reloads
    bool leftFree = !(m_weaponHand && m_weaponHand->leftHandOnWeapon())
      && !(m_handThrow && m_handThrow->throwsWithHand())
      && !(m_manualReload && m_manualReload->leftHandBusy());

    VrGameCameraPose rightHand;
    VrGameCameraPose leftHand;

    if (inGame && right.isActive)
      rightHand = handInGame(right);

    if (inGame && left.isActive)
      leftHand = handInGame(left);

    // The hand whose ray named the game's activation target may pull it;
    // the target stays the game's until its next pick, after this frame
    bool aimedRight = m_handPick && m_handPick->pickedWithHands() && m_handPick->hand() == VrHand::Right;
    bool aimedLeft  = m_handPick && m_handPick->pickedWithHands() && m_handPick->hand() == VrHand::Left;

    for (uint32_t i = 0; i < VrHandCount; i++)
      m_physicsGrab->setFingerRig(VrHand(i), m_weaponHand ? m_weaponHand->fingerRig(VrHand(i)) : nullptr);

    // A held item let go of at the chest goes into the inventory
    bool chestRight = false;
    bool chestLeft  = false;

    if (inGame && m_hasHeadPose) {
      VrGameCameraPose head = vrComputeEyeCameraPose(m_gameCameraPose,
        vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre);
      chestRight = right.isActive && vrGrabAtChest(head, rightHand.position, VrGrabChestZone());
      chestLeft  = left.isActive  && vrGrabAtChest(head, leftHand.position, VrGrabChestZone());
    }

    m_physicsGrab->setHand(VrHand::Right, inGame && right.isActive ? &rightHand : nullptr,
      input.actions.isPressed(VrAction::Grab), rightFree, aimedRight, chestRight);
    m_physicsGrab->setHand(VrHand::Left, inGame && left.isActive ? &leftHand : nullptr,
      input.actions.isPressed(VrAction::TwoHandGrip), leftFree, aimedLeft, chestLeft);

    // What the player's last update did
    VrPhysicsGrabEvents events = m_physicsGrab->takeEvents();

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (events.grabbed[i])
        m_backend.applyHaptic(VrHand(i), GrabHaptic, GrabHapticDuration);
      else if (events.stored[i])
        m_backend.applyHaptic(VrHand(i), GrabHaptic * 2.0f, GrabHapticDuration * 2.0f);
      else if (events.released[i])
        m_backend.applyHaptic(VrHand(i), ReleaseHaptic, GrabHapticDuration);
    }
  }


  bool VrStereoRenderer::leftGripOnWeapon(const VrInputState& input) const {
    // A left hand holding an object keeps it off the weapon
    return input.hasActions && input.actions.isPressed(VrAction::TwoHandGrip)
      && !(m_physicsGrab && m_physicsGrab->holds(VrHand::Left));
  }


  void VrStereoRenderer::updateManualReload(
    const VrInputState&         input,
          VrGameStateKind       state,
          float                 dt) {
    if (!m_manualReload)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];

    VrManualReloadInput hands;
    VrGameCameraPose head, rightHand, leftHand;
    hands.frameTime = dt;

    bool tracked = state == VrGameStateKind::InGame && m_hasReference && m_hasHeadPose
      && right.isActive && left.isActive && input.hasActions;

    if (tracked) {
      head      = vrComputeEyeCameraPose(m_gameCameraPose,
        vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre);
      rightHand = handInGame(right);
      leftHand  = handInGame(left);

      hands.head         = &head;
      hands.rightHand    = &rightHand;
      hands.leftHand     = &leftHand;
      hands.leftOnWeapon = leftGripOnWeapon(input);
      hands.grip         = input.actions.isPressed(VrAction::Magazine);
    }

    // The magazine and the rack part were measured when the model was
    // last drawn
    if (m_weaponHand) {
      hands.well       = m_weaponHand->magazineWell();
      hands.axis       = m_weaponHand->magazineAxis();
      hands.held       = m_weaponHand->magazineHeld();
      hands.fired      = m_weaponHand->magazineFired();
      hands.animation  = m_weaponHand->reloadAnimation();
      hands.rack       = m_weaponHand->rackPart();
      hands.rackAxis   = m_weaponHand->rackAxis();
      hands.rackTravel = m_weaponHand->rackTravel();
      hands.barrel     = m_weaponHand->actionBarrel();
      hands.roll       = m_weaponHand->actionRoll();
      hands.port       = m_weaponHand->roundPort();
      hands.lock       = m_weaponHand->lockPivot();
      hands.lockAxis   = m_weaponHand->lockAxis();
      hands.lockArm    = m_weaponHand->lockArm();
      hands.lockAngle  = m_weaponHand->lockAngle();
    }

    VrReloadEvents events = m_manualReload->updateHands(hands);

    switch (events.magazine) {
      case VrMagazineEvent::Ejected:
      case VrMagazineEvent::Taken:
        m_backend.applyHaptic(VrHand::Left, MagazineTakenHaptic, MagazineHapticDuration);
        break;

      case VrMagazineEvent::Inserted:
        m_backend.applyHaptic(VrHand::Left, MagazineInsertedHaptic, MagazineHapticDuration);
        m_backend.applyHaptic(VrHand::Right, MagazineInsertedHaptic, MagazineHapticDuration);
        break;

      case VrMagazineEvent::NoRounds:
        m_backend.applyHaptic(VrHand::Left, NoRoundsHaptic, NoRoundsHapticDuration);
        break;

      case VrMagazineEvent::Cleared:
      case VrMagazineEvent::Engaged:
        m_backend.applyHaptic(VrHand::Left, MagazineFaintHaptic, MagazineHapticDuration);
        break;

      case VrMagazineEvent::Dropped:
      case VrMagazineEvent::None:
        break;
    }

    switch (events.round) {
      case VrRoundEvent::Taken:
        m_backend.applyHaptic(VrHand::Left, MagazineTakenHaptic, MagazineHapticDuration);
        break;

      case VrRoundEvent::Placed:
        m_backend.applyHaptic(VrHand::Left, MagazineInsertedHaptic, MagazineHapticDuration);
        break;

      case VrRoundEvent::NoRounds:
        m_backend.applyHaptic(VrHand::Left, NoRoundsHaptic, NoRoundsHapticDuration);
        break;

      case VrRoundEvent::Started:
      case VrRoundEvent::Full:
        m_backend.applyHaptic(VrHand::Left, MagazineFaintHaptic, MagazineHapticDuration);
        break;

      case VrRoundEvent::Stopped:
      case VrRoundEvent::Dropped:
      case VrRoundEvent::None:
        break;
    }

    switch (events.rack) {
      case VrRackEvent::Grabbed:
        m_backend.applyHaptic(VrHand::Left, MagazineFaintHaptic, MagazineHapticDuration);
        break;

      case VrRackEvent::Racked:
        m_backend.applyHaptic(VrHand::Left, MagazineInsertedHaptic, MagazineHapticDuration);
        m_backend.applyHaptic(VrHand::Right, MagazineInsertedHaptic, MagazineHapticDuration);
        break;

      case VrRackEvent::Released:
      case VrRackEvent::None:
        break;
    }

    switch (events.action) {
      case VrBreakEvent::Opened:
      case VrBreakEvent::Closed:
        m_backend.applyHaptic(VrHand::Left, MagazineInsertedHaptic, MagazineHapticDuration);
        m_backend.applyHaptic(VrHand::Right, MagazineInsertedHaptic, MagazineHapticDuration);
        break;

      case VrBreakEvent::None:
        break;
    }

    switch (events.lock) {
      case VrLockEvent::Grabbed:
        m_backend.applyHaptic(VrHand::Left, MagazineFaintHaptic, MagazineHapticDuration);
        break;

      case VrLockEvent::Opened:
      case VrLockEvent::Shut:
        m_backend.applyHaptic(VrHand::Left, MagazineInsertedHaptic, MagazineHapticDuration);
        break;

      case VrLockEvent::None:
        break;
    }

    if (events.jammed)
      m_backend.applyHaptic(VrHand::Right, MagazineInsertedHaptic, MagazineHapticDuration);

    if (m_weaponHand) {
      m_weaponHand->setMagazine(m_manualReload->magazineState(), m_manualReload->magazineSeat());
      m_weaponHand->setRack(m_manualReload->rackHeld(), m_manualReload->rackPull(), m_manualReload->rackRest());
      m_weaponHand->setActionOpen(m_manualReload->actionOpen());
      m_weaponHand->setLock(m_manualReload->lockTurn(), m_manualReload->lockDriven());
      m_weaponHand->setRoundLoop(m_manualReload->roundState(), m_manualReload->roundTime());
      m_weaponHand->setHighlight(vrPartsToWork(m_manualReload->situation()));
    }
  }


  void VrStereoRenderer::updateMeleeSwing(
    const VrInputState&         input,
          VrGameStateKind       state,
          float                 dt) {
    if (!m_meleeSwing)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];

    if (state != VrGameStateKind::InGame || !m_hasReference || !right.isActive || !input.isHeadTracked) {
      m_meleeSwing->update(nullptr, nullptr, false, false, m_gameCameraPose, m_gameCameraPose, dt);
      m_powerCharge.reset();
      m_powerRefusal.reset();
      m_chargeRing->hide();
      return;
    }

    int32_t cost = 0;
    bool charged = updatePowerCharge(input, cost);

    VrGameCameraPose hand = handInGame(right);
    VrGameCameraPose head = vrComputeEyeCameraPose(m_gameCameraPose,
      vrComputeEyeInReference(m_reference, input.headPose), VrGameUnitsPerMetre);

    VrMeleeHit hit = m_meleeSwing->update(&hand, m_weaponHand->blade(), m_weaponHand->bladeIsAutomatic(),
      charged, head, m_gameCameraPose, dt);

    // One power attack spends the charge and its action points, however
    // many actors the swing hit
    if (hit.power) {
      float before = m_powerRules->actionPoints();
      m_powerRules->spend(cost);
      m_powerCharge.spend();

      if (m_loggedPowerHits < MaxLoggedPowerHits) {
        Logger::info(str::format("VR: The charged swing made a power attack for ", cost,
          " action points, from ", before, " to ", m_powerRules->actionPoints()));
        m_loggedPowerHits++;
      }
    }

    if (hit.count)
      m_backend.applyHaptic(VrHand::Right, hit.power ? MeleePowerHaptic : MeleeHitHaptic, MeleeHapticDuration);

    if (m_powerCharge.charging())
      m_chargeRing->show(vrComputeChargeRingPose(right, input.headPose), m_powerCharge.fill(), m_powerCharge.charged());
    else
      m_chargeRing->hide();
  }


  void VrStereoRenderer::updatePunches(
    const VrInputState&         input,
          VrGameStateKind       state,
          float                 dt) {
    if (!m_meleeSwing)
      return;

    // Unarmed: no weapon, or a hand-to-hand weapon, drawn
    int32_t type = -1;
    VrWeaponDrawn::equippedWeapon(type);

    bool out = state == VrGameStateKind::InGame && m_hasReference && input.hasActions
      && m_weaponDrawn && !m_weaponDrawn->playerHolstered()
      && (type < 0 || type == VrGame::WeaponTypeHandToHand);

    // A hand busy elsewhere does not punch; one holding an object does
    // not either, the object is swung instead
    std::array<bool, VrHandCount> busy = { };
    busy[uint32_t(VrHand::Right)] = m_rightHandAtPipBoy || m_holster.atHolster()
      || (m_quickWheel && m_quickWheel->isOpen());
    busy[uint32_t(VrHand::Left)] = (m_handThrow && m_handThrow->throwsWithHand())
      || (m_manualReload && m_manualReload->leftHandBusy());

    const VrAction grips[VrHandCount] = { VrAction::TwoHandGrip, VrAction::Grab };
    static_assert(uint32_t(VrHand::Left) == 0, "Grip actions are listed left hand first");

    std::array<VrGameCameraPose, VrHandCount> poses;
    std::array<VrFistInput, VrHandCount> fists;

    for (uint32_t i = 0; i < VrHandCount && out; i++) {
      const VrControllerState& controller = input.controllers[i];

      if (!controller.isActive || busy[i] || (m_physicsGrab && m_physicsGrab->holds(VrHand(i))))
        continue;

      poses[i] = handInGame(controller);
      fists[i].hand   = &poses[i];
      fists[i].closed = controller.hasFingers
        ? vrIsFist(controller.fingerCurl)
        : input.actions.isPressed(grips[i]);
    }

    std::array<uint32_t, VrHandCount> hits = m_meleeSwing->updateFists(fists, out, m_gameCameraPose, dt);

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (hits[i])
        m_backend.applyHaptic(VrHand(i), MeleeHitHaptic, MeleeHapticDuration);
    }
  }


  void VrStereoRenderer::updateHandThrow(
    const VrInputState&         input,
          VrGameStateKind       state,
          float                 dt) {
    if (!m_handThrow)
      return;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];

    VrHandThrowInput hands;
    VrGameCameraPose rightHand, leftHand;

    hands.body = m_gameCameraPose;
    hands.dt   = dt;

    if (state == VrGameStateKind::InGame && m_hasReference && right.isActive && input.hasActions) {
      rightHand     = handInGame(right);
      hands.hand    = &rightHand;
      hands.attack  = input.actions.isPressed(VrAction::Attack);
      hands.grip    = leftGripOnWeapon(input);
      hands.trigger = input.actions.isPressed(VrAction::Magazine);

      if (left.isActive) {
        leftHand        = handInGame(left);
        hands.otherHand = &leftHand;
      }
    }

    VrHandThrowEvents events = m_handThrow->update(hands);

    switch (events.arm) {
      case VrArmEvent::PinTaken:
      case VrArmEvent::Lighting:
        m_backend.applyHaptic(VrHand::Left, ThrowTouchHaptic, ThrowHapticDuration);
        break;

      case VrArmEvent::Armed:
        m_backend.applyHaptic(VrHand::Left, ThrowArmedHaptic, ThrowHapticDuration);
        break;

      case VrArmEvent::PinDropped:
      case VrArmEvent::None:
        break;
    }

    if (events.thrown)
      m_backend.applyHaptic(VrHand::Right, ThrowReleaseHaptic, ThrowHapticDuration);
  }


  bool VrStereoRenderer::updatePowerCharge(
    const VrInputState&         input,
          int32_t&              cost) {
    // Only melee weapons swung with the hand charge: the trigger runs
    // automatic ones and fires the others
    bool swung = m_meleeSwing->chargesWithTrigger();
    bool held  = swung && input.hasActions && input.actions.isPressed(VrAction::Attack);
    bool lacksActionPoints = false;
    bool allowed = held && m_powerRules && m_powerRules->allowed(cost, lacksActionPoints);
    bool wasCharged = m_powerCharge.charged();

    if (!m_powerRules) {
      m_powerCharge.reset();
      return false;
    }

    m_powerCharge.update(held, allowed, m_powerRules->frameTime(), m_powerRules->holdTime());

    if (m_powerRefusal.update(held, lacksActionPoints, m_powerRules->frameTime(), m_powerRules->holdTime())) {
      Logger::info(str::format("VR: A power attack for ", cost, " action points was refused, the player has ",
        m_powerRules->actionPoints()));
      m_powerRules->tellLowActionPoints();
    }

    if (m_powerCharge.charged() && !wasCharged)
      m_backend.applyHaptic(VrHand::Right, ChargedHaptic, ChargedHapticDuration);

    return m_powerCharge.charged();
  }


  void VrStereoRenderer::updatePipBoyGaze(
    const VrInputState&         input,
          VrGameStateKind       state,
          float                 dt) {
    m_closePipBoy = false;

    if (!m_pipBoyGazeAvailable)
      return;

    const VrControllerState& left = input.controllers[uint32_t(VrHand::Left)];
    const VrVector3* offset = m_weaponHand->pipBoyScreen();
    const VrVector3* facing = m_weaponHand->pipBoyScreenNormal();

    VrVector3 screen;
    VrVector3 normal;

    if (left.isActive && offset && facing) {
      screen = vrComputeControllerPoint(left, *offset);
      normal = vrComputeControllerDirection(left, *facing);
    }

    // The left hand holding the weapon is not where the screen would be,
    // nor is one bringing a magazine or a round to it, or arming a grenade
    bool gripped = input.hasActions && input.actions.isPressed(VrAction::TwoHandGrip);
    bool loading = (m_manualReload && m_manualReload->leftHandBusy())
                || (m_handThrow && m_handThrow->leftHandBusy());

    VrPipBoyGazeInput gaze;
    gaze.head     = input.isHeadTracked ? &input.headPose : nullptr;
    gaze.screen   = left.isActive && offset && facing ? &screen : nullptr;
    gaze.normal   = left.isActive && offset && facing ? &normal : nullptr;
    gaze.state    = m_pipBoyControl.state();
    // The right hand at the Pip-Boy reaches for its light, not its screen
    gaze.canOpen  = state == VrGameStateKind::InGame && !gripped && !loading
                 && !m_rightHandAtPipBoy && !m_controls.switchesPipBoyLight();
    gaze.canClose = state == VrGameStateKind::PipBoy && !m_menuOverPipBoy;
    gaze.dt       = dt;

    switch (m_pipBoyGaze.update(gaze)) {
      case VrPipBoyRequest::Open:
        m_pipBoyControl.open();

        if (!m_loggedPipBoyGaze) {
          Logger::info("VR: The left forearm is looked at, the Pip-Boy opens");
          m_loggedPipBoyGaze = true;
        }
        break;

      case VrPipBoyRequest::Close:
        // ClosePipboy called here, between the game's updates, left the
        // first-person model invisible once the Pip-Boy had closed: the
        // back button closes it from the game's own interface update
        m_closePipBoy = true;

        if (!m_loggedPipBoyGazeAway) {
          Logger::info("VR: The left forearm is no longer looked at, the Pip-Boy closes");
          m_loggedPipBoyGazeAway = true;
        }
        break;

      case VrPipBoyRequest::None:
        break;
    }

    // The back button is pressed on the next frame and released on the
    // one after, and the game sees each change on its following update
    if (m_closePipBoy)
      m_pipBoyBackFrames = PipBoyBackReleaseFrames;
    else if (m_pipBoyBackFrames && gaze.state == VrPipBoyState::Closed)
      m_pipBoyBackFrames -= 1;

    m_pipBoyControl.blockGameOpen(m_pipBoyBackFrames != 0);

    // The Pip-Boy grows while it opens and is open, and goes back to its
    // size once the player looks away and it closes
    bool grown = gaze.state == VrPipBoyState::Open
      || (gaze.state == VrPipBoyState::Changing && m_pipBoyGaze.isLooking());

    m_weaponHand->setPipBoyScale(m_pipBoyGrowth.update(grown, m_pipBoyScale, dt));
  }


  bool VrStereoRenderer::isRightHandAtPipBoy(
    const VrInputState&         input,
          VrGameStateKind       state) {
    if (!m_pipBoyGazeAvailable || state != VrGameStateKind::InGame
     || m_pipBoyControl.state() != VrPipBoyState::Closed)
      return false;

    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];
    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrVector3* offset = m_weaponHand->pipBoyScreen();

    // With both hands on the weapon, the right hand is close to the left
    // one and a squeeze of its grip does not reach for the light
    bool gripped = input.hasActions && input.actions.isPressed(VrAction::TwoHandGrip);

    return left.isActive && right.isActive && offset && !gripped
        && vrIsHandAtPipBoy(right, vrComputeControllerPoint(left, *offset));
  }


  void VrStereoRenderer::updateHandPick(
    const VrInputState&         input,
          VrGameStateKind       state) {
    if (!m_handPick)
      return;

    // The game picks after this frame is drawn, from the same camera
    std::array<VrHandRay, VrHandCount> rays;
    std::array<const VrHandRay*, VrHandCount> tracked = { };

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrControllerState& controller = input.controllers[i];

      if (state != VrGameStateKind::InGame || !m_hasReference || !controller.isActive)
        continue;

      VrGameCameraPose aim = vrComputeEyeCameraPose(m_gameCameraPose,
        vrComputeEyeInReference(m_reference, controller.aimPose), VrGameUnitsPerMetre);

      rays[i]    = { aim.position, aim.forward };
      tracked[i] = &rays[i];
    }

    m_handPick->setRays(tracked);
  }


  VrGameCameraPose VrStereoRenderer::handInGame(const VrControllerState& controller) const {
    // Points where the controller points, held where the controller is
    VrGameCameraPose hand = vrComputeEyeCameraPose(m_gameCameraPose,
      vrComputeEyeInReference(m_reference, controller.aimPose), VrGameUnitsPerMetre);
    hand.position = vrComputeEyeCameraPose(m_gameCameraPose,
      vrComputeEyeInReference(m_reference, controller.gripPose), VrGameUnitsPerMetre).position;
    return hand;
  }


  void VrStereoRenderer::updateNpcGaze() {
    if (!m_npcGaze)
      return;

    if (!m_hasReference || !m_hasHeadPose) {
      m_npcGaze->setHead(nullptr);
      return;
    }

    VrVector3 head = vrComputeEyeCameraPose(m_gameCameraPose,
      vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre).position;
    m_npcGaze->setHead(&head);
  }


  bool VrStereoRenderer::cullingView(VrGameCameraPose& pose, VrGameFrustum& frustum) {
    // The eyes are drawn on the same camera, see renderFrame
    if (!m_hasReference || !m_hasHeadPose || !m_hasCullingFov)
      return false;

    m_controls.adjustCamera(pose);

    pose = vrComputeEyeCameraPose(pose,
      vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre);
    frustum = vrComputeGameFrustum(m_cullingFov);
    return true;
  }


  bool VrStereoRenderer::sharedPassView(VrGameCameraPose& pose, VrGameFrustum& frustum) {
    // The head on the eyes' base camera, with the view of both eyes
    if (!m_hasReference || !m_hasHeadPose || !m_hasCullingFov)
      return false;

    pose = vrComputeEyeCameraPose(m_gameCameraPose,
      vrComputeEyeInReference(m_reference, m_headPose), VrGameUnitsPerMetre);
    frustum = vrComputeGameFrustum(m_cullingFov);
    return true;
  }


  void VrStereoRenderer::captureEye() {
    if (m_eye == NoEye || m_eyeCopied[m_eye])
      return;

    // The console's terminal belongs to the 3D image, drawn with the eye
    if (m_terminal)
      m_terminal->drawEye(*m_eyeView);

    logEyeCapture();

    m_eyeCopied[m_eye] = copyRenderTarget(m_eye);

    if (!m_eyeCopied[m_eye] && !m_loggedFailure) {
      Logger::err("VR: Failed to copy the rendered eye image");
      m_loggedFailure = true;
    }
  }


  void VrStereoRenderer::logGameFrustum() {
    constexpr float DegreesPerRadian = 57.2957795f;
    constexpr float Tolerance = 1e-4f;

    const float* frustum = m_gameCamera.frustum;
    bool changed = false;

    for (uint32_t i = 0; i < 4; i++)
      changed |= std::abs(frustum[i] - m_loggedGameFrustum[i]) > Tolerance;

    if (!changed || m_gameFrustumLogs > MaxGameFrustumLogs)
      return;

    std::copy(frustum, frustum + 4, m_loggedGameFrustum);

    if (m_gameFrustumLogs++ == MaxGameFrustumLogs) {
      Logger::info("VR: The game's camera frustum keeps changing, further changes are not logged");
      return;
    }

    // Left, right, top and bottom are tangents of the half angles
    Logger::info(str::format("VR: The game's camera frustum is left ", frustum[0], " right ", frustum[1],
      " top ", frustum[2], " bottom ", frustum[3], " (",
      (std::atan(frustum[1]) - std::atan(frustum[0])) * DegreesPerRadian, "x",
      (std::atan(frustum[2]) - std::atan(frustum[3])) * DegreesPerRadian, " degrees), near ",
      frustum[4], " far ", frustum[5]));
  }


  void VrStereoRenderer::logEyeCapture() {
    D3DSURFACE_DESC target = { };
    m_gameTarget->GetDesc(&target);

    if (target.Width != m_loggedTargetWidth || target.Height != m_loggedTargetHeight) {
      D3DVIEWPORT9 viewport = { };
      m_device->GetViewport(&viewport);

      D3DSURFACE_DESC eye = { };
      m_eyeTextures[m_eye]->GetLevelDesc(0, &eye);

      Logger::info(str::format("VR: Eye image copied from a ", target.Width, "x", target.Height,
        " game target (viewport ", viewport.X, ",", viewport.Y, " ", viewport.Width, "x", viewport.Height,
        ") into a ", eye.Width, "x", eye.Height, " eye texture"));

      m_loggedTargetWidth  = target.Width;
      m_loggedTargetHeight = target.Height;
    }

    if (m_warnedEyeFrustum || m_screenPass)
      return;

    // The game may set its own field of view while it draws the frame
    VrGameFrustum written = vrComputeGameFrustum(m_eyeView->fov);
    const float expected[4] = { written.left, written.right, written.top, written.bottom };
    VrGameCameraState camera = m_renderCamera.save();
    const float* used = camera.frustum;

    for (uint32_t i = 0; i < 4; i++) {
      if (std::abs(used[i] - expected[i]) > 1e-4f) {
        Logger::warn(str::format("VR: The eye was drawn with the frustum left ", used[0], " right ", used[1],
          " top ", used[2], " bottom ", used[3], " instead of the headset's left ", expected[0],
          " right ", expected[1], " top ", expected[2], " bottom ", expected[3]));
        m_warnedEyeFrustum = true;
        return;
      }
    }

    if (!m_loggedEyeFrustum) {
      Logger::info("VR: The eye is drawn with the headset's frustum");
      m_loggedEyeFrustum = true;
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
      case VrGameStateKind::PipBoy:
        return m_hudLayers.menusFound() ? InterfaceLayer::Menu : InterfaceLayer::None;
      default:
        return InterfaceLayer::None;
    }
  }


  VrStereoRenderer::InterfaceLayer VrStereoRenderer::eyeLayer() const {
    if (m_eye == 0)
      return m_layer;

    // The HUD that the game keeps over a menu, drawn without the menu
    if (m_eye == 1 && m_layer == InterfaceLayer::Menu && m_hudAvailable && m_hudLayers.menusFound())
      return InterfaceLayer::Hud;

    if (m_eye == 1 && m_layer == InterfaceLayer::Hud && m_handHudAvailable)
      return InterfaceLayer::HandHud;

    return InterfaceLayer::None;
  }


  bool VrStereoRenderer::renderLayer(
          void*                 interfaceManager,
          void*                 arg0,
          uint32_t              arg1) {
    InterfaceLayer layer = eyeLayer();

    if (layer == InterfaceLayer::None || !createInterfaceTextures())
      return false;

    IDirect3DTexture9* texture = m_menuTexture.ptr();

    if (layer == InterfaceLayer::Hud)
      texture = m_hudTexture.ptr();
    else if (layer == InterfaceLayer::HandHud)
      texture = m_handHudTexture.ptr();

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

    if (layer == InterfaceLayer::Menu && m_menuScenes.isAnyOpen())
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
    bool rendered = kept && (m_hudIsolated || layer == InterfaceLayer::Menu);

    if (layer == InterfaceLayer::Hud)
      m_hudRendered = rendered;
    else if (layer == InterfaceLayer::HandHud)
      m_handHudRendered = rendered;
    else
      m_menuRendered = rendered && m_menuDrawn;

    m_device->SetRenderTarget(0, renderTarget.ptr());
    m_device->SetDepthStencilSurface(depthStencil.ptr());

    if (!rendered && !m_loggedHudFailure) {
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
    // The flags stay set until the eye's interface pass is drawn, see
    // renderLayer. Menus are drawn without any HUD group, and the HUD
    // without the menus that are open.
    InterfaceLayer layer = eyeLayer();

    // The game culled the menus drawn on the Pip-Boy's screen before this
    if (layer == InterfaceLayer::Menu && m_overPipBoy)
      m_menuDrawn = m_hudLayers.isMenuDrawn();

    if (layer == InterfaceLayer::None || !m_hudAvailable || m_hudIsolated)
      return;

    if (layer == InterfaceLayer::Menu)
      m_hudIsolated = m_hudLayers.isolate(nullptr, 0);
    else if (layer == InterfaceLayer::HandHud)
      m_hudIsolated = m_hudLayers.isolate(HandHudGroups, std::size(HandHudGroups));
    else if (m_layer == InterfaceLayer::Menu)
      m_hudIsolated = m_hudLayers.isolateFromMenus(HeadHudGroups, std::size(HeadHudGroups));
    else
      m_hudIsolated = m_hudLayers.isolate(HeadHudGroups, std::size(HeadHudGroups));
  }


  bool VrStereoRenderer::createInterfaceTextures() {
    if (m_hudTexture != nullptr)
      return true;

    D3DSURFACE_DESC desc = { };
    m_gameTarget->GetDesc(&desc);

    // One texture per panel, since a panel keeps showing its last image.
    // The HUD is drawn into one texture and rearranged into the panel's,
    // and the hand HUD into another, which is cut out into the hand panels'.
    bool created = SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_hudTexture, nullptr))
      && SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_hudPanelTexture, nullptr))
      && SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_handHudTexture, nullptr))
      && SUCCEEDED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_menuTexture, nullptr));

    if (!created) {
      Logger::err(str::format("VR: Failed to create the ", desc.Width, "x", desc.Height,
        " interface textures, the HUD and menus are not shown in the headset"));
      m_hudTexture  = nullptr;
      m_hudPanelTexture = nullptr;
      m_handHudTexture = nullptr;
      m_menuTexture = nullptr;
      m_interfaceFailed = true;
    }

    return created;
  }


  bool VrStereoRenderer::composeHud() {
    Com<IDirect3DSurface9> source;
    Com<IDirect3DSurface9> destination;

    if (FAILED(m_hudTexture->GetSurfaceLevel(0, &source))
     || FAILED(m_hudPanelTexture->GetSurfaceLevel(0, &destination)))
      return false;

    D3DSURFACE_DESC desc = { };
    source->GetDesc(&desc);

    VrVector2 imageSize = { float(desc.Width), float(desc.Height) };

    float shiftScale = m_panelConfig.hudWidth > 0.0f
      ? m_panelConfig.hudMessagesOffset / m_panelConfig.hudWidth : 0.0f;
    float shift = std::round(shiftScale * imageSize.x);

    // The crop of the meters placed in the top right corner
    VrInterfaceRect radiationGroup;
    VrInterfaceRect xpGroup;
    bool hasRadiation = m_hudMetersPlaced.radiation && m_hudLayers.groupRect(VrGame::HudRadiationMeter, radiationGroup);
    bool hasXp        = m_hudMetersPlaced.xp && m_hudLayers.groupRect(VrGame::HudXpMeter, xpGroup);

    VrVector2       size = vrComputeInterfaceSize(desc.Width, desc.Height);
    VrInterfaceRect metersGroup;
    VrInterfaceRect meters;
    bool hasMeters = vrComputeHudMetersCrop(hasRadiation ? &radiationGroup : nullptr,
      hasXp ? &xpGroup : nullptr, size, metersGroup);

    if (hasMeters)
      meters = pixelsToRect(interfaceToPixels(metersGroup, imageSize.x / size.x, desc));

    // The messages and objectives in the top left corner, which the meters
    // must not cover
    VrInterfaceRect messagesGroup;
    VrInterfaceRect objectivesGroup;
    bool hasMessages   = m_hudLayers.groupRect(VrGame::HudMessages, messagesGroup);
    bool hasObjectives = m_hudLayers.groupRect(VrGame::HudQuestReminder, objectivesGroup);

    float cornerRight = vrComputeHudCornerRight(hasMessages ? &messagesGroup : nullptr,
      hasObjectives ? &objectivesGroup : nullptr) * imageSize.x / size.x;

    VrHeadHudLayout layout = vrComputeHeadHudLayout(imageSize, shift,
      hasMeters ? &meters : nullptr, std::round(cornerRight));

    if (FAILED(m_device->ColorFill(destination.ptr(), nullptr, D3DCOLOR_ARGB(0, 0, 0, 0))))
      return false;

    for (uint32_t i = 0; i < layout.count; i++) {
      RECT from = rectToPixels(layout.copies[i].source);
      RECT to   = rectToPixels(layout.copies[i].target);

      if (from.right <= from.left || from.bottom <= from.top)
        continue;

      if (FAILED(m_device->StretchRect(source.ptr(), &from, destination.ptr(), &to, D3DTEXF_NONE)))
        return false;
    }

    return true;
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
    // Placed once when it appears, again once the head is known, and again
    // when a dialogue's speaker is first known or changes
    if (!m_panelShown || (!m_panelPlacedWithHead && m_hasHeadPose)
     || (m_dialogueSpeaker && m_dialogueSpeaker != m_panelSpeaker))
      placeMenuPanel();

    m_panelShowsMenu = true;

    m_panelShown = VrD3D9Bridge::submitPanel(m_device, m_backend, VrPanelId::Menu,
      m_menuTexture.ptr(), m_panelPose, m_panelConfig.hudWidth, VrPanelAnchor::Room);
    m_panelWidth = m_panelConfig.hudWidth;

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


  void VrStereoRenderer::submitHandHud(const VrInputState& input, float dt) {
    VrInterfaceRect hitPoints;
    VrInterfaceRect actionPoints;
    VrInterfaceRect info;

    if (!m_hudLayers.groupRect(VrGame::HudHitPoints, hitPoints)
     || !m_hudLayers.groupRect(VrGame::HudActionPoints, actionPoints)
     || !m_hudLayers.groupRect(VrGame::HudInfo, info)) {
      hideHandHud();
      return;
    }

    D3DSURFACE_DESC desc = { };
    m_handHudTexture->GetLevelDesc(0, &desc);

    VrVector2 size  = vrComputeInterfaceSize(desc.Width, desc.Height);
    float     scale = float(desc.Width) / size.x;

    VrHandHudCrops crops = vrComputeHandHudCrops(hitPoints, actionPoints, info, size);

    if (!m_loggedHandHudCrops) {
      Logger::info(str::format("VR: Hand HUD crops in interface units: health bar and compass ",
        crops.wrist.left, ",", crops.wrist.top, " to ", crops.wrist.right, ",", crops.wrist.bottom,
        ", action points and ammunition ", crops.actionPoints.left, ",", crops.actionPoints.top,
        " to ", crops.actionPoints.right, ",", crops.actionPoints.bottom,
        ", interaction prompt ", crops.prompt.left, ",", crops.prompt.top, " to ",
        crops.prompt.right, ",", crops.prompt.bottom));
      m_loggedHandHudCrops = true;
    }

    const VrPose* head = m_hasHeadPose ? &m_headPose : nullptr;

    // The health bar and compass, with the action points and ammunition
    // below them, while the back of the right hand is looked at. They are
    // stacked in pixels, so that each part is copied without scaling.
    RECT health = interfaceToPixels(crops.wrist, scale, desc);
    RECT weapon = interfaceToPixels(crops.actionPoints, scale, desc);
    VrWristStack stack = vrComputeWristStack(pixelsToRect(health), pixelsToRect(weapon));

    const HandPanelPart wristParts[] = {
      { health, rectToPixels(stack.health) },
      { weapon, rectToPixels(stack.actionPoints) } };

    float wristMetresPerPixel = VrHandHudLayout::WristMetresPerUnit / scale;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    VrPose wristPose;

    if (right.isActive)
      wristPose = vrComputeWristPanelPose(right, stack.healthDrop() * wristMetresPerPixel);

    if (m_wristGaze.update(head, right.isActive ? &wristPose : nullptr, dt)) {
      m_wristShown = showHandPanel(VrPanelId::HudWrist, wristParts, std::size(wristParts),
        wristMetresPerPixel, wristPose, m_wristTexture);

      if (m_wristShown && !m_loggedWrist) {
        Logger::info("VR: The back of the right hand is looked at, the health bar, compass, action points and ammunition are shown on it");
        m_loggedWrist = true;
      }
    } else {
      hideHandPanel(VrPanelId::HudWrist, m_wristShown);
    }

    // The interaction prompt, at the hand nearest what the player would activate
    VrVector3 target;
    VrHand hand = VrHand::Right;

    if (!m_promptAvailable || !head || !m_hasReference || !m_activateTarget.find(target)) {
      hideHandPanel(VrPanelId::HudPrompt, m_promptShown);
      return;
    }

    target = vrTransformPoint(m_reference,
      vrComputeGamePointInReference(m_gameCameraPose, target, VrGameUnitsPerMetre));

    // The hand that pointed at the target, or the nearest one when the
    // game picked along the view
    if (m_handPick && m_handPick->pickedWithHands()) {
      hand = m_handPick->hand();

      if (!input.controllers[uint32_t(hand)].isActive) {
        hideHandPanel(VrPanelId::HudPrompt, m_promptShown);
        return;
      }
    } else if (!m_promptHand.update(input.controllers, target, hand)) {
      hideHandPanel(VrPanelId::HudPrompt, m_promptShown);
      return;
    }

    m_promptAt = hand;

    // The button that activates with this hand, from the next frame on
    m_promptIcon.show(hand);

    RECT prompt = interfaceToPixels(crops.prompt, scale, desc);
    const HandPanelPart promptPart = { prompt, { 0, 0, prompt.right - prompt.left, prompt.bottom - prompt.top } };

    m_promptShown = showHandPanel(VrPanelId::HudPrompt, &promptPart, 1,
      VrHandHudLayout::PromptMetresPerUnit / scale,
      vrComputePromptPanelPose(input.controllers[uint32_t(hand)], *head), m_promptTexture);

    if (m_promptShown && hand == VrHand::Left && !m_loggedLeftPrompt) {
      Logger::info("VR: The interaction prompt is at the left hand, the left hand's reload button activates");
      m_loggedLeftPrompt = true;
    }

    if (m_promptShown && !m_loggedPrompt) {
      Logger::info(str::format("VR: The interaction prompt is shown at the fingertip of the ",
        hand == VrHand::Left ? "left" : "right", " hand"));
      m_loggedPrompt = true;
    }
  }


  bool VrStereoRenderer::showHandPanel(
          VrPanelId             id,
    const HandPanelPart*        parts,
          size_t                count,
          float                 metresPerPixel,
    const VrPose&               pose,
          Com<IDirect3DTexture9>& texture) {
    UINT width  = 0;
    UINT height = 0;

    for (size_t i = 0; i < count; i++) {
      width  = std::max(width,  UINT(std::max(parts[i].target.right,  LONG(0))));
      height = std::max(height, UINT(std::max(parts[i].target.bottom, LONG(0))));
    }

    if (!width || !height)
      return false;

    // The crops only change with the game's resolution
    if (texture != nullptr) {
      D3DSURFACE_DESC current = { };
      texture->GetLevelDesc(0, &current);

      if (current.Width != width || current.Height != height)
        texture = nullptr;
    }

    if (texture == nullptr && FAILED(m_device->CreateTexture(width, height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr))) {
      texture = nullptr;

      if (!m_loggedHandHudFailure) {
        Logger::err(str::format("VR: Failed to create a ", width, "x", height, " hand HUD texture"));
        m_loggedHandHudFailure = true;
      }

      return false;
    }

    Com<IDirect3DSurface9> sourceSurface;
    Com<IDirect3DSurface9> panelSurface;

    // Parts narrower than the panel leave transparent space around them
    bool copied = SUCCEEDED(m_handHudTexture->GetSurfaceLevel(0, &sourceSurface))
      && SUCCEEDED(texture->GetSurfaceLevel(0, &panelSurface))
      && SUCCEEDED(m_device->ColorFill(panelSurface.ptr(), nullptr, D3DCOLOR_ARGB(0, 0, 0, 0)));

    for (size_t i = 0; i < count && copied; i++) {
      const HandPanelPart& part = parts[i];

      if (part.source.right <= part.source.left || part.source.bottom <= part.source.top)
        continue;

      copied = SUCCEEDED(m_device->StretchRect(sourceSurface.ptr(), &part.source,
        panelSurface.ptr(), &part.target, D3DTEXF_NONE));
    }

    bool shown = copied && VrD3D9Bridge::submitPanel(m_device, m_backend, id,
      texture.ptr(), pose, float(width) * metresPerPixel, VrPanelAnchor::Room);

    if (!shown && !m_loggedHandHudFailure) {
      Logger::err("VR: Failed to show a hand HUD panel");
      m_loggedHandHudFailure = true;
    }

    return shown;
  }


  void VrStereoRenderer::hideHandPanel(VrPanelId id, bool& shown) {
    if (!shown)
      return;

    m_backend.hidePanel(id);
    shown = false;
  }


  void VrStereoRenderer::hideHandHud() {
    // The wrist display waits to be looked at again before it reappears
    m_wristGaze.reset();
    hideHandPanel(VrPanelId::HudWrist,  m_wristShown);
    hideHandPanel(VrPanelId::HudPrompt, m_promptShown);
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
    m_panelWidth = m_panelConfig.width;

    if (!m_panelShown && !m_loggedPanelFailure) {
      Logger::err("VR: The backend rejected the panel");
      m_loggedPanelFailure = true;
    }
  }


  const VrPanelPlacement* VrStereoRenderer::menuPanel() {
    Com<IDirect3DSurface9> backBuffer;
    D3DSURFACE_DESC desc = { };

    if (!m_panelShown
     || FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))
     || FAILED(backBuffer->GetDesc(&desc)) || !desc.Width)
      return nullptr;

    // Both images of the panel are the size of the game's frame
    m_menuPanel.pose   = m_panelPose;
    m_menuPanel.width  = m_panelWidth;
    m_menuPanel.height = m_panelWidth * float(desc.Height) / float(desc.Width);
    return &m_menuPanel;
  }


  void VrStereoRenderer::placePanel(float distance) {
    VrPose head;
    head.position.y = DefaultHeadHeight;

    if (m_hasHeadPose)
      head = m_headPose;

    m_panelPose = vrComputePanelPose(head, distance);
    m_panelPlacedWithHead = m_hasHeadPose;
  }


  void VrStereoRenderer::placeMenuPanel() {
    m_panelSpeaker = m_dialogueSpeaker;

    // The speaker can stand anywhere around the player, even behind, and
    // sit lower than the eyes. The middle of the dialogue menu is left
    // free for the speaker's face, so it is put on the line to the head.
    VrVector3 head;

    if (!m_dialogueSpeaker || !m_hasHeadPose || !m_hasReference
     || !vrFindActorHead(reinterpret_cast<uintptr_t>(m_dialogueSpeaker), head)) {
      placePanel(m_panelConfig.hudDistance);
      return;
    }

    VrVector3 target = vrTransformPoint(m_reference,
      vrComputeGamePointInReference(m_gameCameraPose, head, VrGameUnitsPerMetre));

    m_panelPose = vrComputePanelPoseTowards(m_headPose, target,
      m_panelConfig.hudDistance, MaxSpeakerPanelPitch);
    m_panelPlacedWithHead = true;

    if (!m_loggedSpeakerPanel) {
      Logger::info("VR: The dialogue panel is placed between the head and the speaker's head");
      m_loggedSpeakerPanel = true;
    }
  }


  void VrStereoRenderer::hidePanel() {
    m_panelSpeaker = nullptr;

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
