#include <cstdint>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_pipboy_control.h"

namespace dxvk {

  namespace {

    using Callback     = void (__cdecl*)();
    using OpenPipBoyFn = void (__thiscall*)(void* interfaceManager, Callback callback, int32_t menu);

    VrPipBoyControl* g_pipBoyControl = nullptr;

    uintptr_t g_previousOpenPipBoy = VrGame::OpenPipBoy;

    uint8_t* interfaceManager() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::InterfaceManager);
    }

  }


  VrPipBoyControl::~VrPipBoyControl() {
    // The patched call stays in place and falls through to the game's function
    if (g_pipBoyControl == this)
      g_pipBoyControl = nullptr;
  }


  bool VrPipBoyControl::initialize() {
    m_available = VrGameMemory::matches(VrGame::OpenPipBoy,
        VrGame::OpenPipBoyPrologue, sizeof(VrGame::OpenPipBoyPrologue))
      && VrGameMemory::matches(VrGame::InterfaceManagerGetSingleton,
        VrGame::InterfaceManagerGetSingletonBytes, sizeof(VrGame::InterfaceManagerGetSingletonBytes))
      && VrGameMemory::readable(VrGame::InterfaceManager, sizeof(uintptr_t));

    if (m_available && !g_pipBoyControl) {
      if (chainGameOpen())
        g_pipBoyControl = this;
      else
        Logger::warn("VR: The Pip-Boy control was not found, closing the Pip-Boy by looking away may reopen it");
    }

    m_lightAvailable = VrGameMemory::matches(VrGame::PipBoyLightTimerReadSite,
        VrGame::PipBoyLightTimerRead, sizeof(VrGame::PipBoyLightTimerRead))
      && VrGameMemory::readable(VrGame::PipBoyLightTimerSetting + VrGame::SettingValue, sizeof(float));

    if (!m_lightAvailable)
      Logger::warn("VR: The Pip-Boy light setting was not found, the light cannot be switched with the hand");

    return m_available;
  }


  void VrPipBoyControl::blockGameOpen(bool blocked) {
    // lStewieAl's Tweaks writes this call once the game runs, without keeping
    // our hook, when it refuses the Pip-Boy in combat (bNoPipBoyInCombat).
    // Nothing is written while the hook is in place.
    if (blocked && !m_gameOpenBlocked && g_pipBoyControl == this)
      chainGameOpen();

    m_gameOpenBlocked = blocked;
  }


  bool VrPipBoyControl::chainGameOpen() {
    return VrGameMemory::chainCall(VrGame::PipBoyControlOpenCallSite, VrGame::OpenPipBoy,
      reinterpret_cast<const void*>(&VrPipBoyControl::gameOpenHook), &g_previousOpenPipBoy);
  }


  VrPipBoyState VrPipBoyControl::state() const {
    uint8_t* manager = m_available ? interfaceManager() : nullptr;

    if (!manager)
      return VrPipBoyState::Changing;

    switch (*reinterpret_cast<const uint32_t*>(manager + VrGame::InterfacePipBoyState)) {
      case 0:  return VrPipBoyState::Closed;
      case 3:  return VrPipBoyState::Open;
      default: return VrPipBoyState::Changing;
    }
  }


  float VrPipBoyControl::lightHoldTime() const {
    if (!m_lightAvailable)
      return 0.0f;

    return *reinterpret_cast<const float*>(VrGame::PipBoyLightTimerSetting + VrGame::SettingValue);
  }


  void VrPipBoyControl::open() const {
    if (uint8_t* manager = m_available ? interfaceManager() : nullptr)
      reinterpret_cast<OpenPipBoyFn>(VrGame::OpenPipBoy)(manager, nullptr, 0);
  }



  void __fastcall VrPipBoyControl::gameOpenHook(
          void*                 interfaceManager,
          void*                 unused,
          void*                 callback,
          int32_t               menu) {
    if (g_pipBoyControl && g_pipBoyControl->m_gameOpenBlocked) {
      if (!g_pipBoyControl->m_loggedBlockedOpen) {
        Logger::info("VR: The Pip-Boy control's release after closing the Pip-Boy does not reopen it");
        g_pipBoyControl->m_loggedBlockedOpen = true;
      }
      return;
    }

    reinterpret_cast<OpenPipBoyFn>(g_previousOpenPipBoy)(interfaceManager,
      reinterpret_cast<Callback>(callback), menu);
  }

}
