#include <cmath>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_head_look.h"

namespace dxvk {

  namespace {

    // The look handler is __thiscall with four stack arguments and returns
    // a bool in AL, of which only the low byte is meaningful
    using HandleLookFn = uint32_t (__fastcall*)(uint8_t* player, void* unused,
      float dt, uint32_t arg1, void* moveFlags, uint32_t arg3);

    // Setters of the heading and the look angle, __thiscall with a float
    using SetAngleFn  = void (__fastcall*)(uint8_t* object, void* unused, float angle);

    // Sit state getter, __thiscall without arguments
    using SitStateFn  = int32_t (__fastcall*)(uint8_t* object, void* unused);

    // FocusOnActor is __thiscall with three stack arguments
    using FocusOnActorFn = void (__fastcall*)(uint8_t* player, void* unused,
      void* actor, float blend, uint32_t skipTurn);

    HandleLookFn g_originalHandleLook = reinterpret_cast<HandleLookFn>(VrGame::HandleLook);

    VrHeadLook* g_headLook = nullptr;

    // Tolerance when checking that the heading is still the one written
    constexpr float AngleTolerance = 1e-4f;

    constexpr float Pi = 3.14159265f;

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    template<typename Fn>
    Fn virtualFunction(const uint8_t* object, uintptr_t slot) {
      auto vtable = readField<const uint8_t*>(object, 0);
      return reinterpret_cast<Fn>(readField<uintptr_t>(vtable, slot));
    }

    // Difference of two angles, in [-pi, pi]
    float angleDifference(float a, float b) {
      return std::remainder(a - b, 2.0f * Pi);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

  }


  VrHeadLook::VrHeadLook(const VrTurnConfig& turn)
  : m_turn(turn) { }


  VrHeadLook::~VrHeadLook() {
    // The patched call stays in place and falls through to the game's handler
    g_headLook = nullptr;
  }


  std::unique_ptr<VrHeadLook> VrHeadLook::install(const VrTurnConfig& turn) {
    if (g_headLook)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::SetLooking,
      VrGame::SetLookingPrologue, sizeof(VrGame::SetLookingPrologue));

    found = found && VrGameMemory::redirectCall(VrGame::LookCallSite,
      VrGame::HandleLook, reinterpret_cast<const void*>(&VrHeadLook::lookHook));

    if (!found) {
      Logger::info("VR: The player's look handler was not found, the head does not aim");
      return nullptr;
    }

    if (!VrGameMemory::redirectCall(VrGame::FocusOnActorTurnCallSite,
        VrGame::FocusOnActor, reinterpret_cast<const void*>(&VrHeadLook::focusHook)))
      Logger::warn("VR: The dialogue camera was not found, dialogues turn the view");

    std::unique_ptr<VrHeadLook> headLook(new VrHeadLook(turn));
    g_headLook = headLook.get();

    Logger::info(turn.smooth
      ? str::format("VR: The head aims, smooth turning at ", turn.smoothSpeed, " degrees per second")
      : str::format("VR: The head aims, snap turns of ", turn.snapAngle, " degrees"));
    return headLook;
  }


  void VrHeadLook::update(const VrPose* headInReference, float turnStick, float dt) {
    m_hasHead = headInReference != nullptr;

    if (m_hasHead)
      m_head = vrComputeHeadAngles(*headInReference);

    if (m_active)
      m_pendingTurn += m_turn.update(turnStick, dt);
    else
      m_turn.reset();
  }


  bool VrHeadLook::adjustCamera(VrGameCameraPose& pose) const {
    const uint8_t* object = player();

    if (!m_active || !object)
      return false;

    // Something else turned the player since, such as a dialogue camera
    if (std::abs(angleDifference(readField<float>(object, VrGame::RefHeading), m_writtenHeading)) > AngleTolerance
     || std::abs(readField<float>(object, VrGame::RefPitch) - m_writtenPitch) > AngleTolerance)
      return false;

    pose = vrComputeBodyCameraPose(m_bodyHeading, pose.position);
    return true;
  }


  uint32_t __fastcall VrHeadLook::lookHook(
          uint8_t*              player,
          void*                 unused,
          float                 dt,
          uint32_t              arg1,
          void*                 moveFlags,
          uint32_t              arg3) {
    uint32_t result = g_originalHandleLook(player, nullptr, dt, arg1, moveFlags, arg3);

    if (g_headLook)
      g_headLook->applyLook(player);

    return result;
  }


  void __fastcall VrHeadLook::focusHook(
          uint8_t*              player,
          void*                 unused,
          void*                 actor,
          float                 blend,
          uint32_t              skipTurn) {
    // The user turns their head towards the speaker; turning the player
    // would turn the whole view, and the speaker with it
    if (g_headLook && g_headLook->m_hasHead)
      skipTurn = 1;

    reinterpret_cast<FocusOnActorFn>(VrGame::FocusOnActor)(player, nullptr, actor, blend, skipTurn);
  }


  bool VrHeadLook::headControlsLook(uint8_t* player) {
    if (*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState) != 0)
      return false;

    if (readField<uint8_t>(player, VrGame::PlayerDisabledControls) & VrGame::ControlLook)
      return false;

    if (readField<uint32_t>(player, VrGame::ActorLifeState) != 0)
      return false;

    return virtualFunction<SitStateFn>(player, VrGame::GetSitStateSlot)(player, nullptr) == 0;
  }


  void VrHeadLook::applyLook(uint8_t* player) {
    if (!m_hasHead || !headControlsLook(player)) {
      m_active = false;
      m_pendingTurn = 0.0f;
      return;
    }

    float heading = readField<float>(player, VrGame::RefHeading);

    // Whatever turned the player since the last update, the mouse
    // included, turns the body
    if (!m_active)
      m_bodyHeading = heading - m_head.yaw;
    else
      m_bodyHeading += angleDifference(heading, m_writtenHeading);

    m_bodyHeading = vrWrapHeading(m_bodyHeading + m_pendingTurn);
    m_pendingTurn = 0.0f;

    virtualFunction<SetAngleFn>(player, VrGame::SetHeadingSlot)(player, nullptr, vrWrapHeading(m_bodyHeading + m_head.yaw));
    reinterpret_cast<SetAngleFn>(VrGame::SetLooking)(player, nullptr, m_head.pitch);

    // The setters wrap and clamp
    m_writtenHeading = readField<float>(player, VrGame::RefHeading);
    m_writtenPitch   = readField<float>(player, VrGame::RefPitch);
    m_bodyHeading    = vrWrapHeading(m_writtenHeading - m_head.yaw);
    m_active         = true;

    if (!m_loggedActive) {
      Logger::info("VR: The head drives the player's heading and look angle");
      m_loggedActive = true;
    }
  }

}
