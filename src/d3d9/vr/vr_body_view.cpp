#include <cmath>
#include <cstring>

#include "../../util/log/log.h"

#include "vr_body_view.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"

namespace dxvk {

  namespace {

    using Get3DFn       = uintptr_t (__thiscall*)(uintptr_t reference);
    using StateFn       = int32_t   (__thiscall*)(uintptr_t object);
    using ObjectUpdateFn = void     (__thiscall*)(uintptr_t object, void* updateData);

    // The head bone is about ten levels below a character's root
    constexpr uint32_t MaxSkeletonDepth = 16;

    // Small enough to be a point, and not zero for the bones' matrices
    constexpr float HeadShrunk = 0.001f;

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    int32_t virtualState(uintptr_t object, uintptr_t slot) {
      auto vtable = readField<uintptr_t>(object, 0);
      return reinterpret_cast<StateFn>(readField<uintptr_t>(vtable, slot))(object);
    }

    uintptr_t bodyRoot(uintptr_t player) {
      return reinterpret_cast<Get3DFn>(VrGame::RefGet3D)(player);
    }

    // The bones and what hangs on them take the scale until the game updates
    // the skeleton again, in the next frame of its logic
    void scaleHead(uintptr_t head, float scale) {
      uint8_t updateData[VrGame::UpdateDataSize] = { };

      *reinterpret_cast<float*>(head + VrGame::ObjectLocalScale) = scale;
      reinterpret_cast<ObjectUpdateFn>(VrGame::ObjectUpdate)(head, updateData);
      *reinterpret_cast<float*>(head + VrGame::ObjectLocalScale) = 1.0f;
    }

    bool shouldFollowHead(uintptr_t player) {
      if (*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState))
        return false;

      // The frames the game has not switched yet, to third person or back to
      // first person, belong here as well: its camera is already or still the
      // one behind the player
      if (!readField<uint8_t>(player, VrGame::PlayerTempThirdPerson)
       && (!readField<uint8_t>(player, VrGame::PlayerCurrentThirdPerson)
        || readField<uint8_t>(player, VrGame::PlayerWantsThirdPerson)))
        return false;

      if (readField<uint32_t>(player, VrGame::ActorLifeState))
        return false;

      auto process = readField<uintptr_t>(player, VrGame::ActorProcess);
      return process && !virtualState(process, VrGame::ProcessGetKnockedStateSlot);
    }

  }


  std::unique_ptr<VrBodyView> VrBodyView::install() {
    bool found = VrGameMemory::matches(VrGame::TempThirdPersonReadSite,
        VrGame::TempThirdPersonRead, sizeof(VrGame::TempThirdPersonRead))
      && VrGameMemory::matches(VrGame::ProcessGetKnockedStateSite,
        VrGame::ProcessGetKnockedStateRead, sizeof(VrGame::ProcessGetKnockedStateRead))
      && VrGameMemory::matchesUnlessRewritten(VrGame::RefGet3D,
        VrGame::RefGet3DPrologue, sizeof(VrGame::RefGet3DPrologue))
      && VrGameMemory::readable(VrGame::VatsCameraState, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::Player, sizeof(uintptr_t));

    if (!found)
      return nullptr;

    return std::unique_ptr<VrBodyView>(new VrBodyView());
  }


  void VrBodyView::placeCamera(VrGameCameraPose& camera, float dt) {
    // The head's small movements are not the body turning
    constexpr float BodySoftness = 0.1f;

    auto player = *reinterpret_cast<const uintptr_t*>(VrGame::Player);

    if (!player)
      return;

    bool follow = shouldFollowHead(player);
    bool wasOnBody = m_view == View::Body;

    // Looked for only when needed: this runs every frame
    uintptr_t root = follow || wasOnBody ? bodyRoot(player) : 0;
    uintptr_t head = root ? vrFindGameObject(root, VrGame::HeadNodeName, MaxSkeletonDepth) : 0;

    follow = follow && head;

    View view = follow ? View::Body
      : !*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState)
        && virtualState(player, VrGame::GetSitStateSlot) ? View::Seat
      : View::Game;

    if (view != View::Game) {
      float source = readField<float>(player, VrGame::RefHeading);

      if (view == View::Body)
        vrComputeHeadHeading(reinterpret_cast<const float*>(head + VrGame::ObjectWorldRotation), source);
      else if (std::abs(camera.forward.x) + std::abs(camera.forward.y) > 1e-3f)
        source = vrComputeCameraHeading(camera);

      if (view != m_view)
        m_heading.start(source, m_hasView ? m_viewHeading : source);

      float heading = m_heading.update(source, view == View::Body ? BodySoftness : 0.0f, dt);

      if (view == View::Body) {
        VrVector3 position;
        std::memcpy(&position, reinterpret_cast<const void*>(head + VrGame::ObjectWorldTranslation), sizeof(position));

        camera = vrComputeBodyCameraPose(heading, vrComputeHeadViewPosition(position,
          reinterpret_cast<const float*>(head + VrGame::ObjectWorldRotation),
          readField<float>(root, VrGame::ObjectWorldScale)));

        scaleHead(head, HeadShrunk);

        if (!m_logged) {
          Logger::info("VR: The game shows the body for a moment, the eyes follow its head");
          m_logged = true;
        }
      } else {
        camera = vrComputeBodyCameraPose(heading, camera.position);
      }
    }

    // The menus can stop the game's logic, which would not bring the head back
    if (wasOnBody && !follow && head)
      scaleHead(head, 1.0f);

    m_changed = follow != wasOnBody;
    m_view    = view;
  }


  void VrBodyView::finishCamera(VrGameCameraPose& camera, bool headTurnsCamera, float dt) {
    bool onBody = m_view == View::Body;

    // Late in getting up, the head's look takes the view back
    if (onBody && headTurnsCamera != m_headTurns)
      m_changed = true;

    if (m_view != View::Game && headTurnsCamera)
      m_heading.keep(vrComputeCameraHeading(camera));

    m_headTurns = headTurnsCamera;

    camera = m_blend.update(camera, m_changed, dt);
    m_changed = false;

    m_viewHeading = vrComputeCameraHeading(camera);
    m_hasView = true;
  }

}
