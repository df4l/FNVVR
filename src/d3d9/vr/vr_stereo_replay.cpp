#include <algorithm>
#include <cstring>
#include <iterator>

#include "../d3d9_device.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_math.h"
#include "vr_stereo_replay.h"

namespace dxvk {

  namespace {

    using DrawWorldFn  = void (__fastcall*)(void* main, void* unused, void* destination, uint32_t pipBoyMode);
    using AccumulateFn = void (__cdecl*)(void* camera, void* scene, void* culler);
    using StartFn      = void (__cdecl*)(uint32_t clear, void* group);
    using IsOutsideFn  = bool (__cdecl*)();
    using ScreenEffectFn = void (__fastcall*)(void* manager, void* unused, uint32_t effect,
      void* renderer, void* source, void* destination, void* parameters, uint32_t endFrame);

    VrStereoReplay* g_stereoReplay      = nullptr;

    // The functions the redirected calls reached: the game's, or another
    // mod's hook (or the mod's occlusion culling)
    uintptr_t       g_previousDrawWorld    = VrGame::DrawWorldStandard;
    uintptr_t       g_previousAccumulate   = VrGame::AccumulateScene;
    uintptr_t       g_previousScreenEffect = VrGame::ImageSpaceRenderEffect;

    /// Seconds between two summary lines
    constexpr double WindowSeconds = 60.0;

    /// Distance, in game units, between the camera the game culls with and
    /// the left eye's camera for it to be the left eye's
    constexpr float SameCameraDistance = 0.5f;

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

  }


  VrStereoReplay::VrStereoReplay(D3D9DeviceEx* device, VrStereoReplayTest test)
  : m_device(device), m_test(test), m_windowStart(high_resolution_clock::now()) { }


  VrStereoReplay::~VrStereoReplay() {
    // The redirected calls stay in place and fall through to the game's functions
    if (m_device->HasVrStereoCapture())
      m_device->DiscardVrStereoCapture();

    if (g_stereoReplay == this)
      g_stereoReplay = nullptr;
  }


  std::unique_ptr<VrStereoReplay> VrStereoReplay::install(IDirect3DDevice9* device, VrStereoReplayTest test) {
    if (g_stereoReplay)
      return nullptr;

    bool found = VrGameMemory::canChainCall(VrGame::DrawWorldStandardCallSite, VrGame::DrawWorldStandard)
      && VrGameMemory::canChainCall(VrGame::SceneAccumulateCallSite, VrGame::AccumulateScene)
      && std::all_of(std::begin(VrGame::ScreenEffectCallSites), std::end(VrGame::ScreenEffectCallSites),
        [] (uintptr_t site) { return VrGameMemory::canChainCall(site, VrGame::ImageSpaceRenderEffect); })
      && VrGameMemory::matches(VrGame::RenderedTextureStart,
        VrGame::RenderedTextureStartCode, sizeof(VrGame::RenderedTextureStartCode))
      && VrGameMemory::matches(VrGame::RenderedTextureIsOutsideFrame,
        VrGame::RenderedTextureIsOutsideFrameCode, sizeof(VrGame::RenderedTextureIsOutsideFrameCode));

    if (!found
     || !VrGameMemory::chainCall(VrGame::DrawWorldStandardCallSite, VrGame::DrawWorldStandard,
          reinterpret_cast<const void*>(&VrStereoReplay::drawWorldHook), &g_previousDrawWorld)
     || !VrGameMemory::chainCall(VrGame::SceneAccumulateCallSite, VrGame::AccumulateScene,
          reinterpret_cast<const void*>(&VrStereoReplay::accumulateHook), &g_previousAccumulate)
     || !VrGameMemory::chainCalls(VrGame::ScreenEffectCallSites, std::size(VrGame::ScreenEffectCallSites),
          VrGame::ImageSpaceRenderEffect, reinterpret_cast<const void*>(&VrStereoReplay::screenEffectHook),
          &g_previousScreenEffect)) {
      Logger::warn("VR: The game's world render calls were not found, the performance mode does not replay the left eye");
      return nullptr;
    }

    std::unique_ptr<VrStereoReplay> replay(new VrStereoReplay(static_cast<D3D9DeviceEx*>(device), test));
    g_stereoReplay = replay.get();

    Logger::info("VR: The performance mode can draw the right eye from the left eye's commands");

    if (test == VrStereoReplayTest::Force)
      Logger::warn("VR: d3d9.vrStereoReplayTest = force: the performance mode replays every frame");
    else if (test == VrStereoReplayTest::Unmoved)
      Logger::warn("VR: d3d9.vrStereoReplayTest = unmoved: the performance mode shows the left eye's world unmoved in the right eye");

    return replay;
  }


  void VrStereoReplay::beginFrame(bool active, const std::array<VrStereoEyeCamera, 2>& eyes) {
    if (!active && m_device->HasVrStereoCapture())
      m_device->DiscardVrStereoCapture();

    // A right eye that changes between replayed and drawn from one frame
    // to the next shows every difference between the two as a flicker
    m_window.notActive += active ? 0u : 1u;
    m_window.switches  += active != m_wasActive ? 1u : 0u;
    m_wasActive = active;

    m_active    = active;
    m_eyes      = eyes;
    m_captured  = false;
    m_hasMatrix = false;

    m_hasCullingView = active
      && vrComputeStereoCullingView(eyes[0], eyes[1], m_cullingPose, m_cullingFrustum);

    if (active && !m_hasCullingView && !m_loggedCulling) {
      Logger::warn("VR: The eyes' frustums cannot be joined, the left eye culls for itself only");
      m_loggedCulling = true;
    }
  }


  void VrStereoReplay::beginEye(uint32_t eye) {
    m_eye = eye;
  }


  void VrStereoReplay::endEye() {
    // A capture the world render did not end, which no replay can trust
    if (m_capturing) {
      m_device->EndVrStereoCapture();
      m_device->DiscardVrStereoCapture();
      m_capturing = false;
      m_captured  = false;
    }

    m_worldMatrix = false;

    m_eye = NoEye;
  }


  void VrStereoReplay::afterShadows() {
    if (!m_active || m_eye != 0 || m_capturing)
      return;

    m_device->BeginVrStereoCapture();
    m_device->SetVrStereoWorld(true);

    m_capturing   = true;
    m_worldMatrix = true;
    m_captured  = false;
    m_hasMatrix = false;
  }


  void VrStereoReplay::beforeScreenPasses() {
    if (!m_capturing)
      return;

    // The renderer still holds the camera the left eye's world was drawn with
    const auto* renderer = *reinterpret_cast<const uint8_t* const*>(VrGame::Renderer);

    if (renderer) {
      VrRendererCamera camera;
      std::memcpy(camera.view.data(), renderer + VrGame::RendererD3DView, sizeof(camera.view));
      std::memcpy(camera.projection.data(), renderer + VrGame::RendererD3DProj, sizeof(camera.projection));

      std::string mismatch;
      m_hasMatrix = vrComputeStereoReplayMatrix(camera, m_eyes[0], m_eyes[1], m_matrix, mismatch);

      if (!m_hasMatrix && !m_loggedCamera) {
        Logger::warn(str::format("VR: A frame is drawn for both eyes: ", mismatch));
        m_loggedCamera = true;
      }

      if (m_hasMatrix && !m_loggedReplay)
        m_cameraReport = describeCamera(camera);
    }

    m_device->SetVrStereoWorld(false);
    m_worldMatrix = false;
  }


  void VrStereoReplay::endFrame() {
    if (m_active)
      m_window.frames += 1;

    if (m_device->HasVrStereoCapture())
      m_device->DiscardVrStereoCapture();

    m_captured = false;
    m_active   = false;

    double seconds = std::chrono::duration<double>(high_resolution_clock::now() - m_windowStart).count();

    if (seconds >= WindowSeconds) {
      if (m_window.frames)
        logWindow();

      m_window = Window();
      m_windowStart = high_resolution_clock::now();
    }
  }


  void VrStereoReplay::endCapture(void* destination, uint32_t pipBoyMode) {
    if (!m_capturing)
      return;

    m_device->EndVrStereoCapture();
    m_capturing   = false;
    m_worldMatrix = false;
    m_captured    = m_device->HasVrStereoCapture();
    m_capturedDestination = destination;
    m_capturedPipBoyMode  = pipBoyMode & 0xFFu;
  }


  bool VrStereoReplay::replay(void* destination, uint32_t pipBoyMode) {
    if (!m_active)
      return false;

    if (!m_captured) {
      refuse(Refusal::NoCapture);
      return false;
    }

    m_captured = false;

    if (!m_hasMatrix) {
      refuse(Refusal::Camera);
      return false;
    }

    // The right eye's world render is not the one the left eye drew
    if (destination != m_capturedDestination || (pipBoyMode & 0xFFu) != m_capturedPipBoyMode) {
      refuse(Refusal::Arguments);
      return false;
    }

    static const std::array<float, 16> Identity = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };

    if (!m_loggedReplay) {
      logFirstReplay();
      m_loggedReplay = true;
    }

    bool unmoved = m_test == VrStereoReplayTest::Unmoved;

    if (!m_device->ReplayVrStereoCapture(unmoved ? Identity : m_matrix)) {
      refuse(Refusal::NoCapture);
      return false;
    }

    m_window.replayed += 1;
    startScreenFrame();

    if (m_onReplay)
      m_onReplay();

    return true;
  }


  void VrStereoReplay::startScreenFrame() {
    // The skipped world render's last image space effect starts the
    // screen's frame (BSRenderedTexture::Start without a clear). Outside a
    // frame, the interface would start it with every buffer cleared, over
    // the replayed world.
    if (game<IsOutsideFn>(VrGame::RenderedTextureIsOutsideFrame)())
      game<StartFn>(VrGame::RenderedTextureStart)(VrGame::ClearNone, nullptr);
  }


  void VrStereoReplay::refuse(Refusal reason) {
    m_window.refused[uint32_t(reason)] += 1;

    if (m_device->HasVrStereoCapture())
      m_device->DiscardVrStereoCapture();
  }


  std::string VrStereoReplay::describeCamera(const VrRendererCamera& camera) const {
    const auto& v = camera.view;
    const auto& p = camera.projection;

    // Direct3D's view: the axes are its columns, its last row is minus
    // the camera's position along each axis
    VrVector3 position = { };

    for (uint32_t j = 0u; j < 3u; j++) {
      VrVector3 axis = { v[0 * 4 + j], v[1 * 4 + j], v[2 * 4 + j] };
      position = position - axis * v[3 * 4 + j];
    }

    auto point = [] (const VrVector3& x) {
      return str::format("(", x.x, ", ", x.y, ", ", x.z, ")");
    };

    return str::format("renderer camera at ", point(position),
      ", left eye at ", point(m_eyes[0].pose.position), ", right eye at ", point(m_eyes[1].pose.position),
      ", culled from ", m_hasCullingView ? point(m_cullingPose.position) : std::string("the left eye"),
      ", projection _11 ", p[0], " _22 ", p[5], " _31 ", p[8], " _32 ", p[9],
      " _33 ", p[10], " _34 ", p[11], " _43 ", p[14], " _44 ", p[15]);
  }


  void VrStereoReplay::logFirstReplay() const {
    std::string rows;

    for (uint32_t i = 0u; i < 4u; i++) {
      rows += str::format(i ? " | " : "", m_matrix[i * 4 + 0], " ", m_matrix[i * 4 + 1], " ",
        m_matrix[i * 4 + 2], " ", m_matrix[i * 4 + 3]);
    }

    Logger::info(str::format("VR: Performance mode, first replay: ", m_cameraReport,
      "; right eye's clip matrix rows ", rows,
      m_test == VrStereoReplayTest::Unmoved ? " (not used: d3d9.vrStereoReplayTest = unmoved)" : ""));
  }


  void VrStereoReplay::logWindow() {
    const auto& refused = m_window.refused;

    Logger::info(str::format("VR: Stereo replay over ", uint32_t(WindowSeconds), " s: ",
      m_window.replayed, " of ", m_window.frames, " frames drew the right eye from the left eye's commands (",
      m_window.widened, " culled for both eyes); drawn for both eyes: ",
      refused[uint32_t(Refusal::NoCapture)], " without the left eye's world, ",
      refused[uint32_t(Refusal::Camera)], " with another camera, ",
      refused[uint32_t(Refusal::Arguments)], " with another world render; ",
      m_window.notActive, " frames in the world without the replay, ",
      m_window.switches, " switches between the two"));
  }


  bool VrStereoReplay::widensCulling(void* camera) {
    if (!m_capturing || !m_hasCullingView || !camera)
      return false;

    // Only the left eye's own camera: the world camera placed at that eye
    m_cullingCamera.attach(camera);
    VrVector3 offset = m_cullingCamera.readPose().position - m_eyes[0].pose.position;
    return vrDot(offset, offset) <= SameCameraDistance * SameCameraDistance;
  }


  void __fastcall VrStereoReplay::drawWorldHook(
          void*                 main,
          void*                 unused,
          void*                 destination,
          uint32_t              pipBoyMode) {
    auto original = game<DrawWorldFn>(g_previousDrawWorld);
    VrStereoReplay* self = g_stereoReplay;

    if (self && self->m_eye == 1 && self->replay(destination, pipBoyMode))
      return;

    original(main, nullptr, destination, pipBoyMode);

    if (self && self->m_eye == 0)
      self->endCapture(destination, pipBoyMode);
  }


  void __cdecl VrStereoReplay::accumulateHook(void* camera, void* scene, void* culler) {
    VrStereoReplay* self = g_stereoReplay;
    bool widen = self && self->widensCulling(camera);

    VrGameCameraState saved = { };

    if (widen) {
      saved = self->m_cullingCamera.save();
      self->m_cullingCamera.apply(self->m_cullingPose, self->m_cullingFrustum);
      self->m_window.widened += 1;
    }

    game<AccumulateFn>(g_previousAccumulate)(camera, scene, culler);

    if (widen)
      self->m_cullingCamera.restore(saved);
  }


  void __fastcall VrStereoReplay::screenEffectHook(
          void*                 manager,
          void*                 unused,
          uint32_t              effect,
          void*                 renderer,
          void*                 source,
          void*                 destination,
          void*                 parameters,
          uint32_t              endFrame) {
    VrStereoReplay* self = g_stereoReplay;

    // A screen quad: the replayed eye draws it where the left eye did
    bool world = self && self->m_worldMatrix;

    if (world)
      self->m_device->SetVrStereoWorld(false);

    game<ScreenEffectFn>(g_previousScreenEffect)(manager, nullptr, effect,
      renderer, source, destination, parameters, endFrame);

    if (world)
      self->m_device->SetVrStereoWorld(true);
  }

}
