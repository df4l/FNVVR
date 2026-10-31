#include <cstring>

#include <windows.h>

#include "vr_bare_hand.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    // BaseProcess::GetCurrentWeapon, __thiscall without arguments
    using CurrentWeaponFn = void* (__fastcall*)(uintptr_t process, void* unused);

    VrBareHand* g_bareHand = nullptr;

    uintptr_t playerProcess(const uint8_t* player) {
      uintptr_t process = 0;
      std::memcpy(&process, player + VrGame::ActorProcess, sizeof(process));
      return process;
    }

  }


  VrBareHand::~VrBareHand() {
    // The table entry stays in place and falls through to the game's function
    g_bareHand = nullptr;
  }


  std::unique_ptr<VrBareHand> VrBareHand::install() {
    if (g_bareHand)
      return nullptr;

    if (!VrGameMemory::redirectVirtual(VrGame::HighProcessVtable + VrGame::ProcessWeaponInfoSlot,
        VrGame::HighProcessCurrentWeapon, reinterpret_cast<const void*>(&VrBareHand::currentWeaponHook)))
      return nullptr;

    std::unique_ptr<VrBareHand> bareHand(new VrBareHand());
    g_bareHand = bareHand.get();
    return bareHand;
  }


  bool VrBareHand::covers(const uint8_t* player) const {
    uintptr_t process = player ? playerProcess(player) : 0;

    return process && *reinterpret_cast<const uintptr_t*>(process) == VrGame::HighProcessVtable;
  }


  bool VrBareHand::hides(uintptr_t process) const {
    return process == m_process.load(std::memory_order_acquire)
        && GetCurrentThreadId() == m_thread.load(std::memory_order_acquire);
  }


  void* __fastcall VrBareHand::currentWeaponHook(
          uintptr_t             process,
          void*                 unused) {
    if (g_bareHand && g_bareHand->hides(process))
      return nullptr;

    return reinterpret_cast<CurrentWeaponFn>(VrGame::HighProcessCurrentWeapon)(process, nullptr);
  }


  VrBareHand::Strike::Strike(VrBareHand* bareHand, const uint8_t* player)
  : m_bareHand(bareHand && bareHand->covers(player) ? bareHand : nullptr) {
    if (!m_bareHand)
      return;

    m_bareHand->m_thread.store(GetCurrentThreadId(), std::memory_order_release);
    m_bareHand->m_process.store(playerProcess(player), std::memory_order_release);
  }


  VrBareHand::Strike::~Strike() {
    if (!m_bareHand)
      return;

    m_bareHand->m_process.store(0, std::memory_order_release);
    m_bareHand->m_thread.store(0, std::memory_order_release);
  }

}
