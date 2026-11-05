#include <atomic>
#include <cstdint>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_live_casino.h"
#include "vr_live_casino_state.h"

namespace dxvk {

  namespace {

    using IsInMenuModeFn     = bool (__cdecl*)();
    using IsTopMenuFn        = bool (__cdecl*)(uint32_t menuId);
    using IsDeadFn           = uint32_t (__fastcall*)(void* actor, void* unused, uint32_t arg);
    using SetMainRenderingFn = void (__cdecl*)(bool defer);
    using InterfaceIdleFn    = void (__fastcall*)(void* main, void* unused);
    using LastMinuteFn       = void (__cdecl*)();
    using AudioFn            = void (__fastcall*)(void* audio, void* unused);

    // Read by the AI threads too, written between frames
    std::atomic<bool> g_live = { false };

    // The menu had its idle on the main thread this frame
    bool g_interfaceDone = false;

    bool realMenuMode() {
      return reinterpret_cast<IsInMenuModeFn>(VrGame::IsInMenuMode)();
    }

    bool isTopMenu(uint32_t menuId) {
      return reinterpret_cast<IsTopMenuFn>(VrGame::IsTopMenu)(menuId);
    }

    VrLiveCasinoState readState() {
      VrLiveCasinoState state;
      state.casinoOnTop = isTopMenu(VrGame::BlackJackMenuId)
        || isTopMenu(VrGame::RouletteMenuId)
        || isTopMenu(VrGame::SlotMachineMenuId);

      if (!state.casinoOnTop)
        return state;

      state.console = *reinterpret_cast<const uint8_t*>(VrGame::InConsole);

      auto player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

      if (!player) {
        state.playerDead = true;
        return state;
      }

      auto vtable = *reinterpret_cast<uint8_t* const*>(player);
      auto isDead = *reinterpret_cast<const IsDeadFn*>(vtable + VrGame::ActorIsDeadSlot);

      state.playerDead     = isDead(player, nullptr, 0) & 0xFF;
      state.playerInCombat = *(player + VrGame::PlayerInCombat);
      return state;
    }

    void callAudio(uintptr_t function) {
      auto audio = *reinterpret_cast<void* const*>(VrGame::AudioSingleton);

      if (audio)
        reinterpret_cast<AudioFn>(function)(audio, nullptr);
    }

    // The game undoes its pauses only as far as they are counted. So taking
    // them back with their count leaves it nothing to undo at the menu's end,
    // whichever way the menu ends.
    void takeBack(uint8_t& pauses, uintptr_t unpause) {
      if (pauses) {
        pauses--;
        callAudio(unpause);
      }
    }

    void resumeSounds() {
      auto manager = *reinterpret_cast<uint8_t* const*>(VrGame::InterfaceManagerInstance);

      if (manager) {
        takeBack(manager[VrGame::InterfaceSoundPauses], VrGame::AudioUnpauseAnimationSounds);
        takeBack(manager[VrGame::InterfaceVoicePauses], VrGame::AudioUnpauseWorldVoices);
      }
    }

    void pauseSounds() {
      auto manager = *reinterpret_cast<uint8_t* const*>(VrGame::InterfaceManagerInstance);

      if (manager) {
        manager[VrGame::InterfaceSoundPauses]++;
        callAudio(VrGame::AudioPauseAnimationSounds);
        manager[VrGame::InterfaceVoicePauses]++;
        callAudio(VrGame::AudioPauseWorldVoices);
      }
    }

  }


  void VrLiveCasino::install() {
    bool found = VrGameMemory::matchesUnlessRewritten(VrGame::IsInMenuMode,
          VrGame::IsInMenuModePrologue, sizeof(VrGame::IsInMenuModePrologue))
      && VrGameMemory::matches(VrGame::IsTopMenu, VrGame::IsTopMenuPrologue, sizeof(VrGame::IsTopMenuPrologue))
      && VrGameMemory::callsTo(VrGame::LiveMenuModeFrameSite, VrGame::IsInMenuMode)
      && VrGameMemory::callsTo(VrGame::SetMainRenderingSite, VrGame::SetMainRendering);

    for (uintptr_t site : VrGame::LiveMenuModeSites)
      found = found && VrGameMemory::callsTo(site, VrGame::IsInMenuMode);

    for (uintptr_t site : VrGame::InterfaceIdleThreadSites)
      found = found && VrGameMemory::callsTo(site, VrGame::InterfaceIdle);

    if (!found) {
      Logger::warn("VR: The game's menu mode tests were not found, the casino games pause the world");
      return;
    }

    VrGameMemory::redirectCall(VrGame::LiveMenuModeFrameSite, VrGame::IsInMenuMode,
      reinterpret_cast<const void*>(&VrLiveCasino::frameMenuModeHook));

    for (uintptr_t site : VrGame::LiveMenuModeSites)
      VrGameMemory::redirectCall(site, VrGame::IsInMenuMode, reinterpret_cast<const void*>(&VrLiveCasino::menuModeHook));

    VrGameMemory::redirectCall(VrGame::SetMainRenderingSite, VrGame::SetMainRendering,
      reinterpret_cast<const void*>(&VrLiveCasino::setMainRenderingHook));

    for (uintptr_t site : VrGame::InterfaceIdleThreadSites)
      VrGameMemory::redirectCall(site, VrGame::InterfaceIdle, reinterpret_cast<const void*>(&VrLiveCasino::threadInterfaceIdleHook));
  }


  bool __cdecl VrLiveCasino::frameMenuModeHook() {
    bool menuMode = realMenuMode();
    bool live     = menuMode && vrCasinoRunsLive(readState());
    bool wasLive  = g_live.load(std::memory_order_relaxed);

    if (live != wasLive) {
      Logger::info(live ? "VR: The world runs behind the casino game" : "VR: The world no longer runs behind the casino game");

      // Stopped with the menu still open: as the game's menus keep it
      if (!live && menuMode)
        pauseSounds();
    }

    g_live.store(live, std::memory_order_relaxed);
    g_interfaceDone = false;

    return menuMode && !live;
  }


  bool __cdecl VrLiveCasino::menuModeHook() {
    return realMenuMode() && !g_live.load(std::memory_order_relaxed);
  }


  void __cdecl VrLiveCasino::setMainRenderingHook(bool defer) {
    if (g_live.load(std::memory_order_relaxed)) {
      // As the game does when its threads do not run: the menu and its scene
      // stay on this thread, before the threads start. The menu pauses the
      // sounds in this idle on its first frames.
      reinterpret_cast<InterfaceIdleFn>(VrGame::InterfaceIdle)(*reinterpret_cast<void* const*>(VrGame::MainSingleton), nullptr);
      reinterpret_cast<LastMinuteFn>(VrGame::InterfaceLastMinuteUpdate)();

      g_interfaceDone = true;
      resumeSounds();
    }

    reinterpret_cast<SetMainRenderingFn>(VrGame::SetMainRendering)(defer);
  }


  void __fastcall VrLiveCasino::threadInterfaceIdleHook(void* main, void* unused) {
    if (!g_interfaceDone)
      reinterpret_cast<InterfaceIdleFn>(VrGame::InterfaceIdle)(main, nullptr);
  }

}
