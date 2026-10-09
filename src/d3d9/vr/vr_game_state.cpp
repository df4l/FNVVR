#include <cstdint>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_state.h"

namespace dxvk {

  namespace {

    using IsInStartMenuFn = bool (__cdecl*)();
    using IsPipBoyShownFn = bool (__cdecl*)();

  }


  bool VrGameState::initialize() {
    m_available = VrGameMemory::matches(VrGame::IsInStartMenu,
        VrGame::IsInStartMenuPrologue, sizeof(VrGame::IsInStartMenuPrologue))
      && VrGameMemory::matches(VrGame::IsPipBoyShown,
        VrGame::IsPipBoyShownPrologue, sizeof(VrGame::IsPipBoyShownPrologue))
      && VrGameMemory::readable(VrGame::LoadingMenu, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::MenuMode, sizeof(uint8_t));

    return m_available;
  }


  VrGameStateKind VrGameState::read() const {
    if (!m_available)
      return VrGameStateKind::Unknown;

    // The main menu is checked first: the loading screen shown while the
    // game starts still exists behind it
    if (reinterpret_cast<IsInStartMenuFn>(VrGame::IsInStartMenu)())
      return VrGameStateKind::MainMenu;

    if (*reinterpret_cast<const uintptr_t*>(VrGame::LoadingMenu))
      return VrGameStateKind::Loading;

    if (*reinterpret_cast<const uint8_t*>(VrGame::MenuMode)) {
      return reinterpret_cast<IsPipBoyShownFn>(VrGame::IsPipBoyShown)()
        ? VrGameStateKind::PipBoy
        : VrGameStateKind::Menu;
    }

    return VrGameStateKind::InGame;
  }


  const char* VrGameState::name(VrGameStateKind kind) {
    switch (kind) {
      case VrGameStateKind::MainMenu: return "main menu";
      case VrGameStateKind::Loading:  return "loading";
      case VrGameStateKind::Menu:     return "menu";
      case VrGameStateKind::PipBoy:   return "Pip-Boy";
      case VrGameStateKind::InGame:   return "in game";
      default:                        return "unknown";
    }
  }

}
