#include <algorithm>
#include <cstring>
#include <iterator>

#include "../d3d9_include.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_sleeve_cut.h"

namespace dxvk {

  namespace {

    using RenderSkinnedFn = void (__cdecl*)(uint8_t*, int, int, int);
    using DrawPartitionFn = void (__fastcall*)(uint8_t*, void*, void*, void*, void*);

    /** Clip plane slot taken for the cut, the last one Direct3D 9 has */
    constexpr DWORD CutPlaneIndex = 5;

    /**
     * The cut stays this far from the elbow, where the other bones gather:
     * beyond the vertices the upper arm also weighs
     */
    constexpr float ElbowMargin = 0.08f * VrGameUnitsPerMetre;

    /** Flipping this bit of a skin's frame id makes its palette computed again */
    constexpr uint32_t FrameIdFlip = 0x80000000u;

    VrSleeveCut* g_sleeveCut = nullptr;
    uintptr_t g_previousRenderSkinned = 0;
    uintptr_t g_previousDrawPartition = 0;

    template<typename T>
    T& field(uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<T*>(base + offset);
    }

    VrVector3 readVector(const uint8_t* object, uintptr_t offset) {
      VrVector3 v;
      std::memcpy(&v, object + offset, sizeof(v));
      return v;
    }

    void writeVector(uint8_t* object, uintptr_t offset, const VrVector3& v) {
      std::memcpy(object + offset, &v, sizeof(v));
    }

    bool isAncestor(const uint8_t* ancestor, uint8_t* object) {
      for (; object; object = field<uint8_t*>(object, VrGame::ObjectParent)) {
        if (object == ancestor)
          return true;
      }

      return false;
    }

    /** The partitions of a dismemberable skin, null for another skin */
    const uint8_t* dismemberPartitions(uint8_t* skin) {
      using GetRttiFn = const void* (__fastcall*)(uint8_t*, void*);

      auto vtable  = field<const uint8_t*>(skin, 0);
      auto getRtti = *reinterpret_cast<const GetRttiFn*>(vtable + VrGame::ObjectGetRttiSlot);

      if (getRtti(skin, nullptr) != reinterpret_cast<const void*>(VrGame::DismemberSkinInstanceRtti))
        return nullptr;

      return field<const uint8_t*>(skin, VrGame::DismemberPartitions);
    }

    void renderSkinned(uint8_t* pass, int testAlpha, int blendAlpha, int setupDrawMode) {
      reinterpret_cast<RenderSkinnedFn>(g_previousRenderSkinned)(pass, testAlpha, blendAlpha, setupDrawMode);
    }

    void drawPartition(uint8_t* renderer, void* buffer, void* partition, void* data) {
      reinterpret_cast<DrawPartitionFn>(g_previousDrawPartition)(renderer, nullptr, buffer, partition, data);
    }

  }


  VrSleeveCut::~VrSleeveCut() {
    // The patched calls stay in place and fall through to the game's functions
    g_sleeveCut = nullptr;
  }


  std::unique_ptr<VrSleeveCut> VrSleeveCut::install() {
    if (g_sleeveCut)
      return nullptr;

    // Both are needed: drawing a sleeve once per arm without its cut would
    // show the gathered bones, and the cut alone would not gather them
    bool found = VrGameMemory::canChainCall(VrGame::RenderSkinnedPassCallSite, VrGame::RenderSkinnedPass)
      && VrGameMemory::chainCalls(VrGame::DrawPartitionCallSites, std::size(VrGame::DrawPartitionCallSites),
        VrGame::DrawPartition, reinterpret_cast<const void*>(&VrSleeveCut::drawPartitionHook), &g_previousDrawPartition)
      && VrGameMemory::chainCall(VrGame::RenderSkinnedPassCallSite, VrGame::RenderSkinnedPass,
        reinterpret_cast<const void*>(&VrSleeveCut::renderSkinnedHook), &g_previousRenderSkinned);

    // A partition hook left in place only passes through without a sleeve
    if (!found) {
      Logger::warn("VR: The skinned draw calls were not found, sleeves that reach above the forearms are hidden");
      return nullptr;
    }

    std::unique_ptr<VrSleeveCut> sleeveCut(new VrSleeveCut());
    g_sleeveCut = sleeveCut.get();
    return sleeveCut;
  }


  void VrSleeveCut::clear() {
    m_sleeves.clear();
    m_arms[0] = Arm();
    m_arms[1] = Arm();
  }


  void VrSleeveCut::setArm(
          uint32_t              arm,
          uint8_t*              forearm,
          uint8_t*              hand,
          float                 distance) {
    if (arm < 2)
      m_arms[arm] = { forearm, hand, distance };
  }


  void VrSleeveCut::addSleeve(
          uint8_t*              geometry,
          uint32_t              arms) {
    auto known = std::find_if(m_sleeves.begin(), m_sleeves.end(),
      [geometry] (const Sleeve& sleeve) { return sleeve.geometry == geometry; });

    if (known != m_sleeves.end())
      known->arms = arms;
    else
      m_sleeves.push_back({ geometry, arms });
  }


  void VrSleeveCut::drawSleeve(
          uint8_t*              pass,
    const Sleeve&               sleeve,
          int                   testAlpha,
          int                   blendAlpha,
          int                   setupDrawMode) {
    uint8_t* skin = field<uint8_t*>(sleeve.geometry, VrGame::GeometrySkinInstance);

    if (!skin)
      return;

    // NiDX9Renderer::CalculateBoneMatrices computes the palette again only
    // when the skin's frame id differs from the renderer's, and then writes
    // the renderer's id into it. Each cut draw writes an id the palette did
    // not come from: flipping the original id twice would give it back to
    // the second arm, which would reuse the first arm's palette.
    uint32_t& frameId = field<uint32_t>(skin, VrGame::SkinInstanceFrameId);
    uint32_t gameId   = frameId;
    uint32_t forcedId = gameId ^ FrameIdFlip;
    bool computed     = false;

    for (uint32_t i = 0; i < 2; i++) {
      const Arm& arm = m_arms[i];

      if (!(sleeve.arms & (1u << i)) || !arm.forearm || !arm.hand)
        continue;

      VrForearmCut cut = vrComputeForearmCut(
        readVector(arm.forearm, VrGame::ObjectWorldTranslation),
        readVector(arm.hand, VrGame::ObjectWorldTranslation),
        arm.distance, ElbowMargin);

      if (!cut.valid || !gatherBones(skin, arm, cut.gather)) {
        restoreBones();
        continue;
      }

      // The palette is computed from the gathered bones for this draw
      if (frameId != forcedId)
        forcedId = frameId ^ FrameIdFlip;

      frameId = forcedId;

      m_cut = cut;
      m_partitions = setupPartitions(skin);
      renderSkinned(pass, testAlpha, blendAlpha, setupDrawMode);
      m_partitions = Partitions();
      m_cut.valid = false;

      computed |= frameId != forcedId;
      restoreBones();
    }

    // And from the game's bones again for the next draw
    if (!computed)
      frameId = gameId;
    else if (frameId != forcedId)
      frameId ^= FrameIdFlip;
  }


  bool VrSleeveCut::gatherBones(
          uint8_t*              skin,
    const Arm&                  arm,
    const VrVector3&            gather) {
    auto data  = field<uint8_t*>(skin, VrGame::SkinInstanceData);
    auto bones = field<uint8_t**>(skin, VrGame::SkinInstanceBones);

    if (!data || !bones)
      return false;

    uint32_t count = field<uint32_t>(data, VrGame::SkinDataBoneCount);
    m_saved.clear();

    // A bone at zero scale draws its vertices at its translation: the
    // vertices weighted to it move towards the elbow, behind the cut
    for (uint32_t i = 0; i < count; i++) {
      uint8_t* bone = bones[i];

      if (!bone || isAncestor(arm.forearm, bone))
        continue;

      m_saved.push_back({ bone,
        readVector(bone, VrGame::ObjectWorldTranslation),
        field<float>(bone, VrGame::ObjectWorldScale) });
    }

    for (const SavedBone& saved : m_saved) {
      writeVector(saved.bone, VrGame::ObjectWorldTranslation, gather);
      field<float>(saved.bone, VrGame::ObjectWorldScale) = 0.0f;
    }

    return true;
  }


  void VrSleeveCut::restoreBones() {
    // In reverse, so that a bone listed twice gets its first value back
    for (auto i = m_saved.rbegin(); i != m_saved.rend(); i++) {
      writeVector(i->bone, VrGame::ObjectWorldTranslation, i->translate);
      field<float>(i->bone, VrGame::ObjectWorldScale) = i->scale;
    }

    m_saved.clear();
  }


  VrSleeveCut::Partitions VrSleeveCut::setupPartitions(uint8_t* skin) {
    Partitions partitions;
    auto skinPartition = field<uint8_t*>(skin, VrGame::SkinInstancePartition);

    if (!skinPartition)
      return partitions;

    partitions.skin      = field<const uint8_t*>(skinPartition, VrGame::SkinPartitionArray);
    partitions.count     = field<uint32_t>(skinPartition, VrGame::SkinPartitionCount);
    partitions.dismember = dismemberPartitions(skin);
    return partitions;
  }


  bool VrSleeveCut::isCap(const void* partition) const {
    if (!m_partitions.skin || !m_partitions.dismember)
      return false;

    auto offset = uintptr_t(partition) - uintptr_t(m_partitions.skin);
    uintptr_t index = offset / VrGame::SkinPartitionSize;

    if (uintptr_t(partition) < uintptr_t(m_partitions.skin) || index >= m_partitions.count)
      return false;

    uint16_t bodyPart = 0;
    std::memcpy(&bodyPart, m_partitions.dismember + index * VrGame::DismemberPartitionSize
      + VrGame::DismemberPartitionBodyPart, sizeof(bodyPart));
    return bodyPart >= VrGame::FirstCapBodyPart;
  }


  void VrSleeveCut::drawCutPartition(
          uint8_t*              renderer,
          void*                 buffer,
          void*                 partition,
          void*                 data) {
    auto device = field<IDirect3DDevice9*>(renderer, VrGame::RendererDevice);

    float viewProj[4][4];
    std::memcpy(viewProj, renderer + VrGame::RendererViewProj, sizeof(viewProj));

    VrVector3 origin;
    std::memcpy(&origin, reinterpret_cast<const void*>(VrGame::RendererPosAdjust), sizeof(origin));

    std::array<float, 4> plane = { };

    if (!device || !vrComputeClipPlane(viewProj, origin, m_cut.point, m_cut.normal, plane)) {
      if (!m_loggedNoPlane) {
        Logger::warn("VR: The camera gives no clip plane, a sleeve is drawn uncut");
        m_loggedNoPlane = true;
      }

      drawPartition(renderer, buffer, partition, data);
      return;
    }

    DWORD enabled = 0;
    DWORD cull    = D3DCULL_CCW;
    float saved[4] = { };

    device->GetRenderState(D3DRS_CLIPPLANEENABLE, &enabled);
    device->GetRenderState(D3DRS_CULLMODE, &cull);
    device->GetClipPlane(CutPlaneIndex, saved);

    // The inside of a cut sleeve shows through its open end
    device->SetClipPlane(CutPlaneIndex, plane.data());
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, enabled | (1u << CutPlaneIndex));
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    drawPartition(renderer, buffer, partition, data);

    device->SetRenderState(D3DRS_CULLMODE, cull);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, enabled);
    device->SetClipPlane(CutPlaneIndex, saved);
  }


  void __cdecl VrSleeveCut::renderSkinnedHook(
          uint8_t*              pass,
          int                   testAlpha,
          int                   blendAlpha,
          int                   setupDrawMode) {
    VrSleeveCut* self = g_sleeveCut;
    uint8_t* geometry = pass ? field<uint8_t*>(pass, VrGame::RenderPassGeometry) : nullptr;

    if (self && geometry && !self->m_sleeves.empty()) {
      auto sleeve = std::find_if(self->m_sleeves.begin(), self->m_sleeves.end(),
        [geometry] (const Sleeve& s) { return s.geometry == geometry; });

      if (sleeve != self->m_sleeves.end()) {
        self->drawSleeve(pass, *sleeve, testAlpha, blendAlpha, setupDrawMode);
        return;
      }
    }

    renderSkinned(pass, testAlpha, blendAlpha, setupDrawMode);
  }


  void __fastcall VrSleeveCut::drawPartitionHook(
          uint8_t*              renderer,
          void*                 unused,
          void*                 buffer,
          void*                 partition,
          void*                 data) {
    VrSleeveCut* self = g_sleeveCut;

    // The meat caps inside a sleeve would show through its cut
    if (self && self->m_cut.valid && self->isCap(partition)) {
      if (!self->m_loggedCaps) {
        Logger::info("VR: The meat caps inside the sleeves are left out of their cut draws");
        self->m_loggedCaps = true;
      }

      return;
    }

    if (self && self->m_cut.valid)
      self->drawCutPartition(renderer, buffer, partition, data);
    else
      drawPartition(renderer, buffer, partition, data);
  }

}
