#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"
#include "../../util/util_time.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_occlusion_culling.h"

namespace dxvk {

  namespace {

    /// Smaller objects are never tested, in units of radius
    constexpr float    MinRadius       = 100.0f;
    /// Boxes nearer to the eye are never tested, in units
    constexpr float    NearDistance    = 400.0f;
    /// Tests per eye and frame
    constexpr uint32_t MaxTests        = 384;
    /// Oldest answer that may leave an object out, in frames
    constexpr uint32_t MaxAnswerAge    = 4;
    /// Frames between two tests of a visible candidate
    constexpr uint32_t VisibleRetest   = 4;
    /// Turn and move of the eye per frame of an answer's age beyond which
    /// the answer is not used
    constexpr float    GuardDegrees    = 2.5f;
    constexpr float    GuardUnits      = 32.0f;
    /// Covered boxes grow by how far the eye's move slides an edge this far
    constexpr float    RevealDepth     = 150.0f;
    /// Smaller moves of the eye per frame grow nothing (the head's sway)
    constexpr float    RevealMinMove   = 0.5f;
    /// Every box reaches this far past its object on screen
    constexpr float    ScreenPad       = 1.5f;
    /// Margins of the boxes rebuilt every frame: share of the radius and units
    constexpr float    ActorShare      = 0.10f;
    constexpr float    ActorMargin     = 24.0f;
    constexpr float    ActivatorShare  = 0.05f;
    constexpr float    ActivatorMargin = 16.0f;
    /// Leaf cards are spread out by the vertex shader, outside the bound
    constexpr float    TreeShare       = 1.20f;
    constexpr float    TreeMargin      = 32.0f;
    /// Tessellated land and swaying grass reach past their boxes
    constexpr float    LandMargin      = 32.0f;
    constexpr float    GrassMargin     = 32.0f;
    /// A land quadrant's half side
    constexpr float    LandHalfSide    = 1024.0f;
    /// Shapes visited per object at most
    constexpr uint32_t MaxShapeVisits  = 4096;
    constexpr uint32_t MaxShapeDepth   = 32;
    /// Blinking guard: a hide that lasted at most FlapSpell frames, again at
    /// most FlapGap frames after the box showed pixels, FlapLimit times
    /// within FlapWindow frames, and the object is never tested again
    constexpr uint32_t FlapGap         = 4;
    constexpr uint32_t FlapSpell       = 2;
    constexpr uint32_t FlapWindow      = 120;
    constexpr uint8_t  FlapLimit       = 8;
    /// Records not seen for this long are forgotten, checked this often
    constexpr uint32_t ForgetFrames    = 600;
    constexpr uint32_t PruneFrames     = 64;
    constexpr double   LogWindowMs     = 60000.0;

    constexpr DWORD BoxFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

    constexpr const char* KindNames[] = { "objects", "trees", "actors", "doors and activators", "land, terrain LOD and grass" };

    using AccumulateFn    = void  (__cdecl*)(void* camera, void* scene, void* culler);
    using OpaqueFn        = void  (__cdecl*)(void* camera, void* accumulator, uint32_t flag);
    using OnVisibleFn     = void  (__fastcall*)(void* node, void* unused, void* culler);
    using GetRttiFn       = const void* (__fastcall*)(void* object, void* unused);
    using WithinFrustumFn = bool  (__fastcall*)(void* shape, void* unused, void* planes);

    VrOcclusionCulling* g_occlusion = nullptr;

    // The functions the redirected calls and slots reached: the game's, or
    // another mod's hook
    uintptr_t g_accumulate          = VrGame::AccumulateScene;
    uintptr_t g_opaque              = VrGame::RenderScenePreResolveDepth;
    uintptr_t g_fadeOnVisible       = VrGame::FadeNodeOnVisible;
    uintptr_t g_treeOnVisible       = VrGame::TreeNodeOnVisible;
    uintptr_t g_multiBoundOnVisible = VrGame::MultiBoundNodeOnVisible;

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
    }

    template<typename T>
    T gameValue(uintptr_t address) {
      return *reinterpret_cast<const T*>(address);
    }

    const void* rttiOf(void* object) {
      auto getRtti = field<GetRttiFn*>(object, 0)[VrGame::ObjectGetRttiSlot / sizeof(void*)];
      return getRtti(object, nullptr);
    }

    bool isKindOf(void* object, uintptr_t rtti) {
      const void* r = rttiOf(object);

      for (uint32_t i = 0; r && i < 16; i++) {
        if (reinterpret_cast<uintptr_t>(r) == rtti)
          return true;

        r = field<const void*>(const_cast<void*>(r), VrGame::RttiBase);
      }

      return false;
    }

    bool rttiNamed(void* object, const char* name) {
      const void* r = rttiOf(object);
      const char* n = r ? field<const char*>(const_cast<void*>(r), VrGame::RttiName) : nullptr;
      return n && !std::strcmp(n, name);
    }

    bool nodeNamed(void* node, const char* name) {
      const char* n = node ? field<const char*>(node, VrGame::ObjectName) : nullptr;
      return n && !std::strcmp(n, name);
    }

    bool appCulled(void* object) {
      return field<uint32_t>(object, VrGame::ObjectFlags) & VrGame::ObjectAppCulled;
    }

    VrOcclusionBox boundCube(const float* bound, float reach) {
      VrOcclusionBox box;

      for (uint32_t i = 0; i < 3; i++) {
        box.min[i] = bound[i] - reach;
        box.max[i] = bound[i] + reach;
      }

      return box;
    }

    struct ShapeWalk {
      VrOcclusionBox box;
      bool           any     = false;
      uint32_t       shapes  = 0;
      uint32_t       visited = 0;
    };

    // Union of the shapes' world bound cubes
    void walkShapes(void* object, uint32_t depth, ShapeWalk& walk) {
      if (!object || depth > MaxShapeDepth || ++walk.visited > MaxShapeVisits || appCulled(object))
        return;

      if (isKindOf(object, VrGame::NiNodeRtti)) {
        void**   children = field<void**>(object, VrGame::NodeChildren);
        uint16_t count    = field<uint16_t>(object, VrGame::NodeChildCount);

        for (uint16_t i = 0; children && i < count; i++)
          walkShapes(children[i], depth + 1, walk);
      } else if (isKindOf(object, VrGame::NiGeometryRtti)) {
        walk.shapes += 1;

        const float* bound = field<const float*>(object, VrGame::ObjectWorldBound);

        if (!bound || !(bound[3] > 0.0f))
          return;

        VrOcclusionBox cube = boundCube(bound, bound[3]);

        for (uint32_t i = 0; i < 3; i++) {
          walk.box.min[i] = walk.any ? std::min(walk.box.min[i], cube.min[i]) : cube.min[i];
          walk.box.max[i] = walk.any ? std::max(walk.box.max[i], cube.max[i]) : cube.max[i];
        }

        walk.any = true;
      }
    }

    /**
     * \brief The device states the box tests change, put back after
     *
     * Each is read first and set back to exactly that value. DrawPrimitiveUP
     * also unbinds stream 0, which is put back too.
     */
    class TestStates {

    public:

      explicit TestStates(IDirect3DDevice9* device)
      : m_device(device) {
        for (uint32_t i = 0; i < StateCount; i++)
          m_device->GetRenderState(States[i], &m_states[i]);

        m_device->GetTextureStageState(0, D3DTSS_COLOROP,   &m_stage[0]);
        m_device->GetTextureStageState(0, D3DTSS_COLORARG1, &m_stage[1]);
        m_device->GetTextureStageState(0, D3DTSS_ALPHAOP,   &m_stage[2]);
        m_device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &m_stage[3]);
        m_device->GetTextureStageState(1, D3DTSS_COLOROP,   &m_stage[4]);
        m_device->GetVertexShader(&m_vs);
        m_device->GetPixelShader(&m_ps);
        m_device->GetVertexDeclaration(&m_declaration);
        m_device->GetFVF(&m_fvf);
        m_device->GetStreamSource(0, &m_stream, &m_offset, &m_stride);
      }

      ~TestStates() {
        for (uint32_t i = 0; i < StateCount; i++)
          m_device->SetRenderState(States[i], m_states[i]);

        m_device->SetTextureStageState(0, D3DTSS_COLOROP,   m_stage[0]);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG1, m_stage[1]);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   m_stage[2]);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, m_stage[3]);
        m_device->SetTextureStageState(1, D3DTSS_COLOROP,   m_stage[4]);

        if (m_declaration)
          m_device->SetVertexDeclaration(m_declaration);
        else
          m_device->SetFVF(m_fvf);

        m_device->SetStreamSource(0, m_stream, m_offset, m_stride);
        m_device->SetVertexShader(m_vs);
        m_device->SetPixelShader(m_ps);

        for (IUnknown* object : { static_cast<IUnknown*>(m_vs), static_cast<IUnknown*>(m_ps),
                                  static_cast<IUnknown*>(m_declaration), static_cast<IUnknown*>(m_stream) }) {
          if (object)
            object->Release();
        }
      }

      void set(bool depthGrows) {
        const DWORD values[StateCount] = {
          0, 0, 0, 0,                                                   // colour writes
          D3DZB_TRUE, FALSE,                                            // depth test, no writes
          DWORD(depthGrows ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL),
          FALSE, FALSE, FALSE, FALSE,                                   // stencil, scissor, alpha test, blend
          D3DCULL_NONE, 0, 0, 0, FALSE,                                 // cull, clip planes, depth bias, fog
        };

        for (uint32_t i = 0; i < StateCount; i++)
          m_device->SetRenderState(States[i], values[i]);

        // Fully opaque, whatever texture is bound
        m_device->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        m_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        m_device->SetVertexShader(nullptr);
        m_device->SetPixelShader(nullptr);
        m_device->SetFVF(BoxFvf);
      }

    private:

      static constexpr uint32_t StateCount = 16;
      static constexpr D3DRENDERSTATETYPE States[StateCount] = {
        D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2, D3DRS_COLORWRITEENABLE3,
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
        D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
        D3DRS_CULLMODE, D3DRS_CLIPPLANEENABLE, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_FOGENABLE,
      };

      IDirect3DDevice9*            m_device;
      DWORD                        m_states[StateCount] = { };
      DWORD                        m_stage[5]     = { };
      IDirect3DVertexShader9*      m_vs           = nullptr;
      IDirect3DPixelShader9*       m_ps           = nullptr;
      IDirect3DVertexDeclaration9* m_declaration  = nullptr;
      DWORD                        m_fvf          = 0;
      IDirect3DVertexBuffer9*      m_stream       = nullptr;
      UINT                         m_offset       = 0;
      UINT                         m_stride       = 0;

    };

    double microsecondsSince(high_resolution_clock::time_point start) {
      return std::chrono::duration<double, std::micro>(high_resolution_clock::now() - start).count();
    }

  }


  VrOcclusionCulling::VrOcclusionCulling(IDirect3DDevice9* device)
  : m_device(device) { }


  VrOcclusionCulling::~VrOcclusionCulling() {
    // The redirected calls stay in place and fall through to the game's functions
    g_occlusion = nullptr;
    releaseAll();
  }


  std::unique_ptr<VrOcclusionCulling> VrOcclusionCulling::install(IDirect3DDevice9* device) {
    if (g_occlusion)
      return nullptr;

    if (pluginLoaded()) {
      Logger::warn("VR: The Occlusion Culling plugin is loaded, the mod's own occlusion culling stays off");
      return nullptr;
    }

    auto slotUsable = [] (uintptr_t slot, uintptr_t expected) {
      if (!VrGameMemory::readable(slot, sizeof(uintptr_t)))
        return false;

      uintptr_t current = *reinterpret_cast<const uintptr_t*>(slot);
      return current == expected || VrGameMemory::isOtherModule(current);
    };

    bool found = VrGameMemory::canChainCall(VrGame::SceneAccumulateCallSite, VrGame::AccumulateScene)
      && VrGameMemory::canChainCall(VrGame::SceneOpaqueCallSite, VrGame::RenderScenePreResolveDepth)
      && slotUsable(VrGame::FadeNodeOnVisibleSlot, VrGame::FadeNodeOnVisible)
      && slotUsable(VrGame::TreeNodeOnVisibleSlot, VrGame::TreeNodeOnVisible)
      && slotUsable(VrGame::MultiBoundNodeOnVisibleSlot, VrGame::MultiBoundNodeOnVisible)
      && VrGameMemory::readable(VrGame::MultiBoundAabbVtable, sizeof(uintptr_t));

    if (!found
     || !VrGameMemory::chainCall(VrGame::SceneAccumulateCallSite, VrGame::AccumulateScene,
          reinterpret_cast<const void*>(&VrOcclusionCulling::accumulateHook), &g_accumulate)
     || !VrGameMemory::chainCall(VrGame::SceneOpaqueCallSite, VrGame::RenderScenePreResolveDepth,
          reinterpret_cast<const void*>(&VrOcclusionCulling::opaqueHook), &g_opaque)
     || !VrGameMemory::chainVirtual(VrGame::FadeNodeOnVisibleSlot, VrGame::FadeNodeOnVisible,
          reinterpret_cast<const void*>(&VrOcclusionCulling::fadeOnVisibleHook), &g_fadeOnVisible)
     || !VrGameMemory::chainVirtual(VrGame::TreeNodeOnVisibleSlot, VrGame::TreeNodeOnVisible,
          reinterpret_cast<const void*>(&VrOcclusionCulling::treeOnVisibleHook), &g_treeOnVisible)
     || !VrGameMemory::chainVirtual(VrGame::MultiBoundNodeOnVisibleSlot, VrGame::MultiBoundNodeOnVisible,
          reinterpret_cast<const void*>(&VrOcclusionCulling::multiBoundOnVisibleHook), &g_multiBoundOnVisible)) {
      Logger::warn("VR: The scene's culling was not found, occlusion culling is off");
      return nullptr;
    }

    std::unique_ptr<VrOcclusionCulling> culling(new VrOcclusionCulling(device));
    g_occlusion = culling.get();
    return culling;
  }


  bool VrOcclusionCulling::pluginLoaded() {
    return GetModuleHandleW(L"OcclusionCulling.dll") != nullptr;
  }


  void VrOcclusionCulling::setEnabled(bool enabled) {
    if (enabled == m_enabled)
      return;

    m_enabled = enabled;
    m_eye     = ~0u;
    m_gathering = false;
    releaseAll();
    m_stats   = Stats();
    m_statsMs = 0.0;
    m_lastFrameTicks = 0;

    Logger::info(str::format("VR: Occlusion culling ", enabled ? "on" : "off"));
  }


  void VrOcclusionCulling::beginWorld(uint32_t eye) {
    if (!m_enabled || eye >= EyeCount)
      return;

    m_eye    = eye;
    m_thread = GetCurrentThreadId();
  }


  void VrOcclusionCulling::endWorld() {
    m_eye       = ~0u;
    m_gathering = false;
  }


  void VrOcclusionCulling::endFrame() {
    if (!m_enabled)
      return;

    // Only frames with the world count in the log's window
    uint64_t ticks = uint64_t(high_resolution_clock::now().time_since_epoch().count());

    if (m_lastFrameTicks && m_stats.frames) {
      double ms = double(ticks - m_lastFrameTicks) * 1e-6;

      if (ms > 0.0 && ms < 250.0)
        m_statsMs += ms;
    }

    m_lastFrameTicks = ticks;

    if (m_statsMs >= LogWindowMs)
      logStats();

    if (m_frame % PruneFrames == 0) {
      // A reset record stays in its eye's list until that eye's next
      // readAnswers, which may come long after when the world is not drawn
      // (menus, loading screens): drop it now, before it can be freed
      for (uint32_t eye = 0; eye < EyeCount; eye++) {
        std::vector<Record*>& pending = m_pending[eye];

        pending.erase(std::remove_if(pending.begin(), pending.end(),
          [eye] (const Record* r) { return !r->eyes[eye].pending; }), pending.end());
      }

      for (auto i = m_records.begin(); i != m_records.end(); ) {
        Record& r = i->second;
        bool pending = r.eyes[0].pending || r.eyes[1].pending;

        if (!pending && r.lastSeen + ForgetFrames < m_frame) {
          for (EyeState& state : r.eyes) {
            if (state.query)
              m_freeQueries.push_back(state.query);
          }

          i = m_records.erase(i);
        } else {
          ++i;
        }
      }
    }

    m_frame += 1;
  }


  bool VrOcclusionCulling::active() const {
    return m_enabled && m_supported && m_eye < EyeCount && GetCurrentThreadId() == m_thread;
  }


  void VrOcclusionCulling::gatherBegin(void* camera, void* culler) {
    auto start = high_resolution_clock::now();

    const float* position = &field<float>(camera, VrGame::ObjectWorldTranslation);
    const float* rotation = &field<float>(camera, VrGame::ObjectWorldRotation);

    CameraRecord& now = m_cameras[m_eye][m_frame % CameraRing];
    now.frame = m_frame;
    std::memcpy(now.position, position, sizeof(now.position));
    std::memcpy(now.rotation, rotation, sizeof(now.rotation));
    std::memcpy(m_eyePosition, position, sizeof(m_eyePosition));

    // Whether the eye stayed close enough to where it was each answer age ago
    for (uint32_t age = 1; age <= MaxAnswerAge; age++) {
      const CameraRecord& then = m_cameras[m_eye][(m_frame - age) % CameraRing];

      float dx = position[0] - then.position[0];
      float dy = position[1] - then.position[1];
      float dz = position[2] - then.position[2];

      m_guard[age] = then.frame == m_frame - age
        && std::sqrt(dx * dx + dy * dy + dz * dz) <= GuardUnits * float(age)
        && vrRotationDegrees(rotation, then.rotation) <= GuardDegrees * float(age);
    }

    readAnswers(m_eye);

    m_candidates.clear();
    m_culler    = culler;
    m_camera    = camera;
    m_depth     = 0;
    m_gathering = true;

    m_stats.costUs += microsecondsSince(start);
  }


  void VrOcclusionCulling::gatherEnd() {
    m_gathering = false;
  }


  void VrOcclusionCulling::readAnswers(uint32_t eye) {
    std::vector<Record*>& pending = m_pending[eye];
    size_t kept = 0;
    bool waiting = false;

    for (size_t i = 0; i < pending.size(); i++) {
      Record* r = pending[i];
      EyeState& s = r->eyes[eye];

      // Reset meanwhile: another object at that node's address
      if (!s.pending)
        continue;

      // The GPU answers in order: after one that is not ready, the rest wait
      if (waiting) {
        pending[kept++] = r;
        continue;
      }

      DWORD pixels = 0;
      HRESULT hr = s.query->GetData(&pixels, sizeof(pixels), 0);

      if (hr == S_FALSE) {
        m_stats.notReady += 1;
        pending[kept++] = r;
        waiting = true;
        continue;
      }

      s.pending = false;

      if (hr != S_OK) {
        s.query->Release();
        s.query = nullptr;

        if (hr == D3DERR_DEVICELOST) {
          releaseAll();
          return;
        }

        continue;
      }

      s.answered    = true;
      s.answerFrame = s.tested;
      s.answeredBox = s.testedBox;

      if (pixels) {
        if (!s.testedGrown)
          s.sawPixels = true;

        // A grown box's pixels may only be its margin's: drawn now and
        // tested again next frame
        if (s.testedGrown && (s.zeroStreak > 0 || s.afterGrown)) {
          if (!s.afterGrown)
            s.streakBefore = s.zeroStreak;

          s.afterGrown = true;
        } else {
          s.afterGrown = false;
        }

        s.zeroStreak = 0;
      } else if (s.afterGrown) {
        s.zeroStreak = uint8_t(std::min(s.streakBefore + 1, 255));
        s.afterGrown = false;
      } else {
        s.zeroStreak = uint8_t(std::min(s.zeroStreak + 1, 255));
      }
    }

    pending.resize(kept);
  }


  bool VrOcclusionCulling::readView(void* renderer, void* camera, VrOcclusionView& view) const {
    const float* posAdjust = reinterpret_cast<const float*>(VrGame::RendererPosAdjust);
    const float* invView   = &field<float>(renderer, VrGame::RendererInvView);
    const float* position  = &field<float>(camera, VrGame::ObjectWorldTranslation);

    // The renderer's camera must be the eye's
    float dx = posAdjust[0] + invView[12] - position[0];
    float dy = posAdjust[1] + invView[13] - position[1];
    float dz = posAdjust[2] + invView[14] - position[2];

    if (dx * dx + dy * dy + dz * dz >= 4.0f)
      return false;

    D3DVIEWPORT9 viewport = { };

    if (FAILED(m_device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height)
      return false;

    std::memcpy(view.posAdjust, posAdjust, sizeof(view.posAdjust));
    std::memcpy(view.view, &field<float>(renderer, VrGame::RendererD3DView), sizeof(view.view));
    std::memcpy(view.proj, &field<float>(renderer, VrGame::RendererD3DProj), sizeof(view.proj));
    view.x      = float(viewport.X);
    view.y      = float(viewport.Y);
    view.width  = float(viewport.Width);
    view.height = float(viewport.Height);
    view.minZ   = viewport.MinZ;
    view.maxZ   = viewport.MaxZ;
    return true;
  }


  void VrOcclusionCulling::testCandidates(void* camera) {
    auto start = high_resolution_clock::now();
    uint32_t eye = m_eye;

    void* renderer = gameValue<void*>(VrGame::Renderer);

    if (!renderer || camera != m_camera)
      return;

    if (field<bool>(renderer, VrGame::RendererDeviceLost)) {
      releaseAll();
      return;
    }

    if (!m_checked) {
      m_checked = true;
      m_supported = SUCCEEDED(m_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr));

      if (!m_supported) {
        Logger::warn("VR: The device has no occlusion queries, occlusion culling is off");
        return;
      }
    }

    VrOcclusionView view;
    IDirect3DSurface9* depth = nullptr;

    if (!readView(renderer, camera, view) || FAILED(m_device->GetDepthStencilSurface(&depth)) || !depth)
      return;

    depth->Release();

    m_turns.clear();

    for (uint32_t i = 0; i < m_candidates.size(); i++) {
      const EyeState& s = m_candidates[i].record->eyes[eye];

      if (s.pending)
        continue;

      uint32_t since = m_frame - s.tested;
      VrOcclusionUrgency urgency = vrOcclusionUrgency(s.answered,
        s.zeroStreak > 0 || s.afterGrown, since, VisibleRetest);

      if (urgency != VrOcclusionUrgency::NotDue)
        m_turns.push_back({ urgency, since, i });
    }

    vrSelectOcclusionTurns(m_turns, MaxTests);

    if (m_turns.empty()) {
      m_stats.costUs += microsecondsSince(start);
      return;
    }

    float focal = vrOcclusionFocal(view);

    // How far this frame's move of the eye can slide a near edge
    float reveal = 0.0f;
    const CameraRecord& last = m_cameras[eye][(m_frame - 1) % CameraRing];

    if (last.frame == m_frame - 1) {
      float dx = m_eyePosition[0] - last.position[0];
      float dy = m_eyePosition[1] - last.position[1];
      float dz = m_eyePosition[2] - last.position[2];
      float move = std::sqrt(dx * dx + dy * dy + dz * dz);

      if (move >= RevealMinMove)
        reveal = vrRevealPixels(focal, move, RevealDepth, 0.25f * view.width);
    }

    {
      TestStates states(m_device);
      states.set(vrDepthGrowsWithDistance(view));

      VrOcclusionVertex vertices[VrOcclusionBoxVertices];

      for (const VrOcclusionTurn& turn : m_turns) {
        Record& r = *m_candidates[turn.index].record;
        EyeState& s = r.eyes[eye];

        bool grown = reveal > 0.0f && s.answered && (s.zeroStreak > 0 || s.afterGrown);
        VrOcclusionBox box = vrGrowOcclusionBox(r.box, m_eyePosition, ScreenPad + (grown ? reveal : 0.0f), focal);

        // Reaches behind the eye: counts as seen
        if (!vrProjectOcclusionBox(box, view, vertices)) {
          s.tested     = m_frame;
          s.zeroStreak = 0;
          s.afterGrown = false;
          m_stats.untestable += 1;
          continue;
        }

        if (!s.query)
          s.query = acquireQuery();

        if (!s.query)
          break;

        s.query->Issue(D3DISSUE_BEGIN);
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, VrOcclusionBoxVertices / 3, vertices, sizeof(VrOcclusionVertex));
        s.query->Issue(D3DISSUE_END);

        s.pending     = true;
        s.tested      = m_frame;
        s.testedGrown = grown;
        s.testedBox   = r.box;
        m_pending[eye].push_back(&r);
        m_stats.tested += 1;
      }
    }

    m_stats.costUs += microsecondsSince(start);
  }


  VrOcclusionCulling::Record& VrOcclusionCulling::record(void* node) {
    return m_records[node];
  }


  void VrOcclusionCulling::resetEye(EyeState& state) {
    if (state.pending && state.query) {
      state.query->Release();
      state.query = nullptr;
    }

    IDirect3DQuery9* query = state.query;
    state = EyeState();
    state.query = query;
  }


  bool VrOcclusionCulling::collect(void* node, Kind hint) {
    void* ref = field<void*>(node, VrGame::FadeNodeLinkedRef);

    if (!ref || appCulled(node))
      return false;

    const float* bound = field<const float*>(node, VrGame::ObjectWorldBound);

    if (!bound || !(bound[3] > 0.0f))
      return false;

    uint8_t  refType  = field<uint8_t>(ref, VrGame::FormType);
    void*    base     = field<void*>(ref, VrGame::RefBaseForm);
    uint8_t  baseType = base ? field<uint8_t>(base, VrGame::FormType) : 0;
    uint32_t mult     = field<uint32_t>(node, VrGame::FadeNodeLodMultType);
    float    radius   = bound[3];

    bool isActor = refType == VrGame::FormTypeCharacter || refType == VrGame::FormTypeCreature;
    bool isActivator = baseType == VrGame::FormTypeActivator || baseType == VrGame::FormTypeTalkingActivator
      || baseType == VrGame::FormTypeTerminal || baseType == VrGame::FormTypeDoor;

    Kind kind = hint;

    if (isActor) {
      if (ref == gameValue<void*>(VrGame::Player) || mult == VrGame::LodMultActorBodyPart || radius >= 10000.0f)
        return false;

      kind = Kind::Actor;
    } else if (isActivator) {
      if (radius < MinRadius || radius >= 100000.0f)
        return false;

      kind = Kind::Activator;
    } else {
      bool projectile = (refType >= VrGame::FormTypeMissileProjectile && refType <= VrGame::FormTypeFlameProjectile)
        || refType == VrGame::FormTypeContinuousBeam;

      if (projectile || baseType == VrGame::FormTypeLight || baseType == VrGame::FormTypePlaceableWater
       || mult == VrGame::LodMultActors || mult == VrGame::LodMultActorBodyPart
       || mult == VrGame::LodMultRenderedMenu || mult == VrGame::LodMultInvisible
       || radius < MinRadius)
        return false;
    }

    Record& r = record(node);
    uint32_t formId = field<uint32_t>(ref, VrGame::FormId);
    bool moving = kind == Kind::Actor || kind == Kind::Activator;

    const float* position = &field<float>(node, VrGame::ObjectWorldTranslation);
    float scale = field<float>(node, VrGame::ObjectWorldScale);

    bool same = r.lastSeen && r.kind == kind && r.formId == formId
      && (moving || (r.scale == scale && !std::memcmp(r.key, position, sizeof(r.key))));

    if (!same) {
      for (EyeState& state : r.eyes)
        resetEye(state);

      r.kind     = kind;
      r.formId   = formId;
      r.scale    = scale;
      r.unstable = false;
      r.boxFrame = 0;
      std::memcpy(r.key, position, sizeof(r.key));

      ShapeWalk walk;
      walkShapes(node, 0, walk);
      r.shapes = uint16_t(std::min<uint32_t>(walk.shapes, 0xFFFF));

      if (kind == Kind::Tree) {
        r.box = boundCube(bound, radius * TreeShare + TreeMargin);
      } else if (!moving) {
        // The shapes' union, cut to the node's own bound: both hold the object
        r.box = boundCube(bound, radius);

        if (walk.any) {
          for (uint32_t i = 0; i < 3; i++) {
            float low  = std::max(walk.box.min[i], r.box.min[i]);
            float high = std::min(walk.box.max[i], r.box.max[i]);

            if (low <= high) {
              r.box.min[i] = low;
              r.box.max[i] = high;
            }
          }
        }
      }
    }

    // Things that move: the bound the game culls them with, every frame
    VrOcclusionBox current;

    if (moving) {
      current = boundCube(bound, radius);

      if (r.boxFrame != m_frame) {
        float margin = kind == Kind::Actor
          ? radius * ActorShare + ActorMargin
          : radius * ActivatorShare + ActivatorMargin;

        r.box = boundCube(bound, radius + margin);
        r.boxFrame = m_frame;
      }
    }

    r.lastSeen = m_frame;

    if (!r.shapes || r.unstable || vrDistanceToBox(m_eyePosition, r.box) < NearDistance) {
      m_stats.unstable += r.unstable ? 1 : 0;
      return false;
    }

    bool hide = decide(r, moving, moving ? &current : nullptr);
    m_candidates.push_back({ &r, hide });
    return hide;
  }


  bool VrOcclusionCulling::collectMultiBound(void* node, void* culler) {
    void*    multiBound = field<void*>(node, VrGame::MultiBoundNodeBound);
    void*    shape      = multiBound ? field<void*>(multiBound, VrGame::MultiBoundShape) : nullptr;
    uint16_t children   = field<uint16_t>(node, VrGame::NodeChildCount);
    void**   list       = field<void**>(node, VrGame::NodeChildren);

    if (!shape || !children || !list || field<uintptr_t>(shape, 0) != VrGame::MultiBoundAabbVtable)
      return false;

    const float* centre = &field<float>(shape, VrGame::MultiBoundAabbCentre);
    const float* half   = &field<float>(shape, VrGame::MultiBoundAabbHalfExtents);

    bool sane = half[0] > 0.0f && half[1] > 0.0f && half[2] >= 0.0f
      && half[0] < 1.0e6f && half[1] < 1.0e6f && half[2] < 1.0e6f;

    if (!sane)
      return false;

    void* child = list[0];
    float margin[3] = { 0.0f, 0.0f, LandMargin };

    bool land = children == 1 && half[0] == LandHalfSide && half[1] == LandHalfSide
      && child && isKindOf(child, VrGame::NiGeometryRtti);

    bool grass = !land && children == 1 && child
      && nodeNamed(field<void*>(node, VrGame::ObjectParent), "Grass")
      && rttiNamed(child, "TallGrassTriStrips");

    bool terrainLod = false;

    if (!land && !grass) {
      void* parent = field<void*>(node, VrGame::ObjectParent);

      for (uint32_t i = 0; parent && i < 4 && !terrainLod; i++) {
        terrainLod = nodeNamed(parent, "LandLOD");
        parent = field<void*>(parent, VrGame::ObjectParent);
      }
    }

    if (!land && !grass && !terrainLod)
      return false;

    if (grass)
      margin[0] = margin[1] = margin[2] = GrassMargin;

    // The game's own test against the view: what lies outside is its to cull
    auto withinFrustum = field<WithinFrustumFn*>(shape, 0)[VrGame::MultiBoundWithinFrustumSlot / sizeof(void*)];

    if (!withinFrustum(shape, nullptr, reinterpret_cast<uint8_t*>(culler) + VrGame::CullerPlanes))
      return false;

    VrOcclusionBox box;

    for (uint32_t i = 0; i < 3; i++) {
      box.min[i] = centre[i] - half[i] - margin[i];
      box.max[i] = centre[i] + half[i] + margin[i];
    }

    Record& r = record(node);

    bool same = r.lastSeen && r.kind == Kind::Land
      && !std::memcmp(r.box.min, box.min, sizeof(box.min))
      && !std::memcmp(r.box.max, box.max, sizeof(box.max));

    if (!same) {
      for (EyeState& state : r.eyes)
        resetEye(state);

      r.kind     = Kind::Land;
      r.formId   = 0;
      r.box      = box;
      r.shapes   = 1;
      r.unstable = false;
    }

    r.lastSeen = m_frame;

    if (r.unstable || vrDistanceToBox(m_eyePosition, r.box) < NearDistance) {
      m_stats.unstable += r.unstable ? 1 : 0;
      return false;
    }

    bool hide = decide(r, false, nullptr);
    m_candidates.push_back({ &r, hide });
    return hide;
  }


  bool VrOcclusionCulling::decide(Record& r, bool moving, const VrOcclusionBox* current) {
    EyeState& s = r.eyes[m_eye];
    bool hide = false;

    if (s.answered && s.zeroStreak >= 2) {
      uint32_t age = m_frame - s.answerFrame;

      m_stats.covered += 1;

      if (age >= 1 && age <= MaxAnswerAge) {
        if (!m_guard[age])
          m_stats.guarded += 1;
        else
          hide = !moving || s.answeredBox.contains(*current);
      }
    }

    hide = notBlinking(r, s, hide);

    m_stats.candidates += 1;

    if (hide) {
      m_stats.hidden += 1;
      m_stats.hiddenShapes += r.shapes;
      m_stats.kinds[uint32_t(r.kind)] += 1;
    }

    return hide;
  }


  bool VrOcclusionCulling::notBlinking(Record& r, EyeState& s, bool hide) {
    if (!hide)
      return false;

    // An object whose box lies behind its own surface hides its own box:
    // left out, shown at the next answer, left out again, every few frames
    bool newSpell = s.hiddenFrame + 1 < m_frame;

    if (newSpell && s.hiddenFrame && s.sawPixels && m_frame - s.hiddenFrame <= FlapGap
     && s.hiddenFrame - s.hideStart + 1 <= FlapSpell) {
      if (!s.flaps || m_frame - s.flapFrame > FlapWindow) {
        s.flaps     = 0;
        s.flapFrame = m_frame;
      }

      if (++s.flaps >= FlapLimit) {
        r.unstable = true;
        return false;
      }
    }

    if (newSpell)
      s.hideStart = m_frame;

    s.hiddenFrame = m_frame;
    s.sawPixels   = false;
    return true;
  }


  IDirect3DQuery9* VrOcclusionCulling::acquireQuery() {
    if (!m_freeQueries.empty()) {
      IDirect3DQuery9* query = m_freeQueries.back();
      m_freeQueries.pop_back();
      return query;
    }

    IDirect3DQuery9* query = nullptr;

    if (FAILED(m_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query)))
      return nullptr;

    return query;
  }


  void VrOcclusionCulling::releaseAll() {
    for (auto& entry : m_records) {
      for (EyeState& state : entry.second.eyes) {
        if (state.query)
          state.query->Release();
      }
    }

    for (IDirect3DQuery9* query : m_freeQueries)
      query->Release();

    m_records.clear();
    m_freeQueries.clear();
    m_candidates.clear();

    for (auto& pending : m_pending)
      pending.clear();
  }


  void VrOcclusionCulling::logStats() {
    const Stats& s = m_stats;
    double views = double(std::max<uint64_t>(s.frames, 1));

    auto per = [views] (uint64_t value) {
      return uint32_t(double(value) / views + 0.5);
    };

    std::string kinds;

    for (uint32_t i = 0; i < 5; i++)
      kinds += str::format(i ? ", " : "", per(s.kinds[i]), " ", KindNames[i]);

    Logger::info(str::format("VR: Occlusion culling over ", uint32_t(m_statsMs / 1000.0 + 0.5), " s, ",
      s.frames, " eye views: per view ", per(s.candidates), " candidates, ", per(s.hidden), " left out (",
      s.candidates ? uint32_t(100.0 * double(s.hidden) / double(s.candidates) + 0.5) : 0u, " %, ",
      per(s.hiddenShapes), " shapes: ", kinds, "), ", per(s.tested), " tests, ", per(s.covered),
      " found covered, ", per(s.guarded), " kept for the eye's move, ", per(s.untestable),
      " untestable, ", per(s.notReady), " answers late, ", per(s.unstable),
      " blinking ones drawn; ", uint32_t(s.costUs / views + 0.5), " us per view"));

    m_stats   = Stats();
    m_statsMs = 0.0;
  }


  void __cdecl VrOcclusionCulling::accumulateHook(void* camera, void* scene, void* culler) {
    VrOcclusionCulling* self = g_occlusion;
    bool mine = self && camera && culler && self->active();

    if (mine) {
      self->gatherBegin(camera, culler);
      self->m_stats.frames += 1;
    }

    game<AccumulateFn>(g_accumulate)(camera, scene, culler);

    if (mine)
      self->gatherEnd();
  }


  void __cdecl VrOcclusionCulling::opaqueHook(void* camera, void* accumulator, uint32_t flag) {
    game<OpaqueFn>(g_opaque)(camera, accumulator, flag);

    VrOcclusionCulling* self = g_occlusion;

    if (self && self->active() && self->m_camera) {
      self->testCandidates(camera);
      self->m_candidates.clear();
      self->m_camera = nullptr;
    }
  }


  void __fastcall VrOcclusionCulling::fadeOnVisibleHook(void* node, void* unused, void* culler) {
    VrOcclusionCulling* self = g_occlusion;

    if (self && self->m_gathering && culler == self->m_culler && GetCurrentThreadId() == self->m_thread) {
      if (self->m_depth == 0 && self->collect(node, Kind::Object))
        return;

      self->m_depth += 1;
      game<OnVisibleFn>(g_fadeOnVisible)(node, nullptr, culler);
      self->m_depth -= 1;
      return;
    }

    game<OnVisibleFn>(g_fadeOnVisible)(node, nullptr, culler);
  }


  void __fastcall VrOcclusionCulling::treeOnVisibleHook(void* node, void* unused, void* culler) {
    VrOcclusionCulling* self = g_occlusion;

    if (self && self->m_gathering && culler == self->m_culler && GetCurrentThreadId() == self->m_thread) {
      if (self->m_depth == 0 && self->collect(node, Kind::Tree))
        return;

      self->m_depth += 1;
      game<OnVisibleFn>(g_treeOnVisible)(node, nullptr, culler);
      self->m_depth -= 1;
      return;
    }

    game<OnVisibleFn>(g_treeOnVisible)(node, nullptr, culler);
  }


  void __fastcall VrOcclusionCulling::multiBoundOnVisibleHook(void* node, void* unused, void* culler) {
    VrOcclusionCulling* self = g_occlusion;

    if (self && self->m_gathering && culler == self->m_culler && self->m_depth == 0
     && GetCurrentThreadId() == self->m_thread && self->collectMultiBound(node, culler))
      return;

    game<OnVisibleFn>(g_multiBoundOnVisible)(node, nullptr, culler);
  }

}
