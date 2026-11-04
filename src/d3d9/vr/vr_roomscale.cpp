#include <algorithm>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_npc_drag.h"
#include "vr_roomscale.h"

namespace dxvk {

  namespace {

    using MoveFn         = void* (__fastcall*)(uint8_t* mobile, void* unused, float dt, VrVector3* move, uint8_t flags);
    using IsKnockedOutFn = bool (__fastcall*)(const uint8_t* actor, void* unused);

    // bhkCharacterController::Move does nothing for a shorter time
    constexpr float ShortestStep = 0.0001f;

    VrRoomscale* g_roomscale = nullptr;

    uint8_t* player() {
      return *reinterpret_cast<uint8_t**>(VrGame::Player);
    }


    VrVector2 playerLocation(const uint8_t* player) {
      const float* location = reinterpret_cast<const float*>(player + VrGame::RefLocation);
      return { location[0], location[1] };
    }


    float playerHeading(const uint8_t* player) {
      return *reinterpret_cast<const float*>(player + VrGame::RefHeading);
    }


    bool canWalk(const uint8_t* player) {
      if (*(player + VrGame::PlayerDisabledControls) & VrGame::ControlMovement)
        return false;

      void* const* table = *reinterpret_cast<void* const* const*>(player);
      auto isKnockedOut = reinterpret_cast<IsKnockedOutFn>(table[VrGame::ActorIsKnockedOutSlot / sizeof(void*)]);
      return !isKnockedOut(player, nullptr);
    }

  }


  VrRoomscale::~VrRoomscale() {
    g_roomscale = nullptr;
  }


  std::unique_ptr<VrRoomscale> VrRoomscale::install() {
    if (!VrGameMemory::redirectCall(VrGame::ActorMoveMobileCallSite, VrGame::MobileObjectMove,
        reinterpret_cast<const void*>(&VrRoomscale::moveHook)))
      return nullptr;

    std::unique_ptr<VrRoomscale> roomscale(new VrRoomscale());
    g_roomscale = roomscale.get();
    return roomscale;
  }


  void VrRoomscale::update(
          VrPose&               reference,
    const VrGameCameraPose&     base,
    const VrPose*               head,
          bool                  follows) {
    constexpr float Units = VrGameUnitsPerMetre;

    // The camera already moved with the capsule: the eyes stay where they were
    VrVector2 achieved = m_achieved;
    m_achieved = { };
    m_wanted   = { };

    reference = vrShiftReference(reference, base, { achieved.x, achieved.y, 0.0f }, Units);

    const uint8_t* object = player();

    if (!m_enabled || !follows || !head || !object || !canWalk(object)) {
      m_blockedTime = 0.0f;
      return;
    }

    VrVector3 eye = vrComputeEyeCameraPose(base, vrComputeEyeInReference(reference, *head), Units).position;
    VrVector2 location = playerLocation(object);
    VrVector2 offset = { eye.x - location.x, eye.y - location.y };

    if (m_blockedTime >= VrRoomscaleBlockedTime) {
      VrVector2 back = vrRoomscalePushback(offset, VrRoomscaleHeadRoom * Units);
      reference = vrShiftReference(reference, base, { back.x, back.y, 0.0f }, Units);
      offset = { offset.x - back.x, offset.y - back.y };
    }

    m_wanted = vrRoomscaleWantedMove(offset, VrRoomscaleDeadZone * Units);

    if (!m_loggedActive) {
      Logger::info("VR: The player's capsule follows the head");
      m_loggedActive = true;
    }
  }


  void* VrRoomscale::movePlayer(uint8_t* player, float dt, VrVector3* move, uint8_t flags) {
    constexpr float Units = VrGameUnitsPerMetre;

    float heading = playerHeading(player);
    float maxStep = VrRoomscaleMaxSpeed * Units * dt;

    VrNpcGroundVector stick = vrNpcToWorld({ move->x, move->y }, heading);
    VrVector2 gameMove = { stick.x, stick.y };

    VrVector2 asked = dt > ShortestStep ? vrRoomscaleLimitMove(m_wanted, maxStep) : VrVector2();
    m_wanted = { m_wanted.x - asked.x, m_wanted.y - asked.y };

    VrNpcGroundVector local = vrNpcToActor({ asked.x, asked.y }, heading);
    move->x += local.x;
    move->y += local.y;

    VrVector2 before = playerLocation(player);

    void* controller = reinterpret_cast<MoveFn>(VrGame::MobileObjectMove)(player, nullptr, dt, move, flags);

    VrVector2 after = playerLocation(player);

    if (asked.x != 0.0f || asked.y != 0.0f)
      m_followTime = VrRoomscaleFollowTime;

    VrVector2 achieved = vrRoomscaleAchievedMove({ after.x - before.x, after.y - before.y },
      gameMove, asked, m_followTime > 0.0f, maxStep);

    m_achieved = { m_achieved.x + achieved.x, m_achieved.y + achieved.y };
    m_followTime = std::max(m_followTime - dt, 0.0f);

    // Once the head is back within the dead zone the capsule has nothing
    // more to do, which says nothing about what stands in its way
    m_blockedTime = vrRoomscaleIsBlocked(achieved, asked) ? m_blockedTime + dt : 0.0f;

    return controller;
  }


  void* __fastcall VrRoomscale::moveHook(
          uint8_t*              mobile,
          void*                 unused,
          float                 dt,
          VrVector3*            move,
          uint8_t               flags) {
    if (g_roomscale && mobile == player())
      return g_roomscale->movePlayer(mobile, dt, move, flags);

    return reinterpret_cast<MoveFn>(VrGame::MobileObjectMove)(mobile, nullptr, dt, move, flags);
  }

}
