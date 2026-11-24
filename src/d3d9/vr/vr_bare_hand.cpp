#include <cstring>

#include <windows.h>

#include "../../util/log/log.h"

#include "vr_bare_hand.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    // BaseProcess::GetCurrentWeapon, __thiscall without arguments
    using CurrentWeaponFn = void* (__fastcall*)(uintptr_t process, void* unused);

    using CreateBloodFn         = void (__fastcall*)(void* target, void* unused, float damage, const uint8_t* hit);
    using CreateBloodOnWeaponFn = void (__fastcall*)(void* attacker, void* unused, float damage, const uint8_t* hit, void* target);

    VrBareHand* g_bareHand = nullptr;

    /// What the queued blood call reached before us, the game's function or a plugin's hook
    uintptr_t g_queuedBlood = VrGame::ActorCreateBlood;

    // The process whose weapon is hidden on this thread, or zero. The queued
    // blood can run on another thread during a strike.
    thread_local uintptr_t t_hiddenProcess = 0;

    uintptr_t playerProcess(const uint8_t* player) {
      uintptr_t process = 0;
      std::memcpy(&process, player + VrGame::ActorProcess, sizeof(process));
      return process;
    }

    // A melee hit of the player that had no weapon. Actor::CombatHit passes
    // none to the hit in that case.
    const uint8_t* unarmedHitPlayer(const uint8_t* hit) {
      auto player = *reinterpret_cast<const uint8_t* const*>(VrGame::Player);

      if (!hit || !player || *reinterpret_cast<const uint8_t* const*>(hit + VrGame::HitAggressor) != player
       || *reinterpret_cast<const uintptr_t*>(hit + VrGame::HitProjectile))
        return nullptr;

      uintptr_t weapon = *reinterpret_cast<const uintptr_t*>(hit + VrGame::HitWeapon);

      return !weapon || weapon == *reinterpret_cast<const uintptr_t*>(VrGame::FistsForm) ? player : nullptr;
    }

  }


  VrBareHand::~VrBareHand() {
    // The table entry and the calls stay in place and fall through to the
    // game's functions
    g_bareHand = nullptr;
  }


  std::unique_ptr<VrBareHand> VrBareHand::install() {
    if (g_bareHand)
      return nullptr;

    if (!VrGameMemory::redirectVirtual(VrGame::HighProcessVtable + VrGame::ProcessWeaponInfoSlot,
        VrGame::HighProcessCurrentWeapon, reinterpret_cast<const void*>(&VrBareHand::currentWeaponHook)))
      return nullptr;

    bool queued = VrGameMemory::chainCall(VrGame::QueuedCreateBloodSite, VrGame::ActorCreateBlood,
      reinterpret_cast<const void*>(&VrBareHand::queuedBloodHook), &g_queuedBlood);
    queued = VrGameMemory::redirectCall(VrGame::QueuedCreateBloodOnWeaponSite, VrGame::ActorCreateBloodOnWeapon,
      reinterpret_cast<const void*>(&VrBareHand::queuedBloodOnWeaponHook)) && queued;

    if (!queued)
      Logger::warn("VR: The game's queued blood was not found, a bare fist may show the impact of the equipped weapon");

    std::unique_ptr<VrBareHand> bareHand(new VrBareHand());
    g_bareHand = bareHand.get();
    return bareHand;
  }


  bool VrBareHand::covers(const uint8_t* player) const {
    uintptr_t process = player ? playerProcess(player) : 0;

    return process && *reinterpret_cast<const uintptr_t*>(process) == VrGame::HighProcessVtable;
  }


  void* __fastcall VrBareHand::currentWeaponHook(
          uintptr_t             process,
          void*                 unused) {
    if (process == t_hiddenProcess)
      return nullptr;

    return reinterpret_cast<CurrentWeaponFn>(VrGame::HighProcessCurrentWeapon)(process, nullptr);
  }


  void __fastcall VrBareHand::queuedBloodHook(
          void*                 target,
          void*                 unused,
          float                 damage,
    const uint8_t*              hit) {
    Strike strike(g_bareHand, unarmedHitPlayer(hit));
    reinterpret_cast<CreateBloodFn>(g_queuedBlood)(target, nullptr, damage, hit);
  }


  void __fastcall VrBareHand::queuedBloodOnWeaponHook(
          void*                 attacker,
          void*                 unused,
          float                 damage,
    const uint8_t*              hit,
          void*                 target) {
    Strike strike(g_bareHand, unarmedHitPlayer(hit));
    reinterpret_cast<CreateBloodOnWeaponFn>(VrGame::ActorCreateBloodOnWeapon)(attacker, nullptr, damage, hit, target);
  }


  VrBareHand::Strike::Strike(VrBareHand* bareHand, const uint8_t* player) {
    if (!bareHand || !bareHand->covers(player))
      return;

    m_previous = t_hiddenProcess;
    m_hides    = true;
    t_hiddenProcess = playerProcess(player);
  }


  VrBareHand::Strike::~Strike() {
    if (m_hides)
      t_hiddenProcess = m_previous;
  }

}
