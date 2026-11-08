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
    using SetRestrainedFn    = void (__fastcall*)(void* actor, void* unused, bool restrained);
    using EndMovementFn      = void (__fastcall*)(void* actor, void* unused);
    using GetAnimationFn     = uint8_t* (__fastcall*)(void* actor, void* unused);
    using IdlePlayingFn      = bool (__fastcall*)(void* animation, void* unused);
    using SetIdleFn          = void (__fastcall*)(void* process, void* unused, void* idle);
    using PostAnimationFn    = void (__fastcall*)(void* process, void* unused, uint32_t action);
    using FormByIdFn         = uint8_t* (__cdecl*)(uint32_t id);
    using FreeIdleFn         = void (__fastcall*)(void* process, void* unused, void* actor);

    // Frames left before the hold pose is asked again: the AI picks the
    // idle a frame or two after it is asked
    constexpr uint32_t PoseRetryFrames = 30;

    // Read by the AI threads too, written between frames
    std::atomic<bool> g_live = { false };

    // The menu had its idle on the main thread this frame
    bool g_interfaceDone = false;

    // The menus that may run live, set once at the start
    bool g_casino      = false;
    bool g_vigorTester = false;
    bool g_caravan     = false;

    // The Caravan challenger held by the mod, main thread only
    uint8_t* g_heldChallenger = nullptr;

    // Whether the challenger holds a hand up as if it held cards, the idle
    // of that pose once found, whether it was asked for this game, and the
    // frames to wait before asking for it again
    bool     g_caravanPose = false;
    uint8_t* g_poseForm    = nullptr;
    bool     g_posed       = false;
    uint32_t g_poseWait    = 0;

    bool realMenuMode() {
      return reinterpret_cast<IsInMenuModeFn>(VrGame::IsInMenuMode)();
    }

    bool isTopMenu(uint32_t menuId) {
      return reinterpret_cast<IsTopMenuFn>(VrGame::IsTopMenu)(menuId);
    }

    VrLiveCasinoState readState() {
      VrLiveCasinoState state;
      state.casinoOnTop = (g_casino && (isTopMenu(VrGame::BlackJackMenuId)
          || isTopMenu(VrGame::RouletteMenuId)
          || isTopMenu(VrGame::SlotMachineMenuId)))
        || (g_caravan && isTopMenu(VrGame::CaravanMenuId))
        || (g_vigorTester && isTopMenu(VrGame::LoveTesterMenuId));

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

    uint32_t lifeState(const uint8_t* actor) {
      return *reinterpret_cast<const uint32_t*>(actor + VrGame::ActorLifeState);
    }

    template<typename Fn>
    Fn slot(const void* object, uintptr_t offset) {
      auto vtable = *reinterpret_cast<const uint8_t* const*>(object);
      return *reinterpret_cast<const Fn*>(vtable + offset);
    }

    uint8_t* processOf(uint8_t* actor) {
      return *reinterpret_cast<uint8_t* const*>(actor + VrGame::ActorProcess);
    }

    uint8_t* animationOf(uint8_t* actor) {
      return processOf(actor) ? slot<GetAnimationFn>(actor, VrGame::ActorGetAnimationSlot)(actor, nullptr) : nullptr;
    }

    // The AnimIdle of the hold pose if it is the actor's idle, else null
    uint8_t* poseIdle(const uint8_t* animation) {
      auto idle = animation ? *reinterpret_cast<uint8_t* const*>(animation + VrGame::AnimationIdle) : nullptr;
      return idle && g_poseForm && *reinterpret_cast<uint8_t* const*>(idle + VrGame::AnimIdleForm) == g_poseForm
        ? idle : nullptr;
    }

    // The challenger keeps a hand raised for the cards during the game, as
    // a script's PlayIdle would have it, since the restrained actor's AI
    // picks no idle of its own (findings/caravan.md §3.8). Once it plays,
    // its section loops for ever and the sheet it brings stays hidden: the
    // mod's cards take its place.
    void keepCaravanPose(uint8_t* challenger) {
      auto animation = animationOf(challenger);

      if (!animation)
        return;

      if (auto idle = poseIdle(animation)) {
        auto section = *reinterpret_cast<const uint32_t*>(idle + VrGame::AnimIdleSection);

        if (section < VrGame::AnimationSections)
          reinterpret_cast<int32_t*>(animation + VrGame::AnimationLoopCounts)[section] = VrGame::AnimationLoopForever;

        auto addOns = reinterpret_cast<uint8_t* const*>(idle + VrGame::AnimIdleAddOns);

        for (uint32_t i = 0; i < VrGame::AnimIdleAddOnCount; i++) {
          if (addOns[i])
            *reinterpret_cast<uint32_t*>(addOns[i] + VrGame::ObjectFlags) |= VrGame::ObjectFlagAppCulled;
        }

        return;
      }

      if (g_poseWait) {
        g_poseWait--;
        return;
      }

      if (reinterpret_cast<IdlePlayingFn>(VrGame::AnimationSpecialIdlePlaying)(animation, nullptr)
       || *reinterpret_cast<void* const*>(animation + VrGame::AnimationIdleQueued))
        return;

      if (!g_poseForm) {
        auto form = reinterpret_cast<FormByIdFn>(VrGame::FormById)(VrGame::CaravanHoldCardsIdle);

        if (!form || form[VrGame::FormType] != VrGame::FormTypeIdle) {
          Logger::warn("VR: The idle that holds Caravan's cards up is not in the game, the challenger keeps its pose");
          g_caravanPose = false;
          return;
        }

        g_poseForm = form;
      }

      auto process = processOf(challenger);
      slot<SetIdleFn>(process, VrGame::ProcessSetCurrentIdleSlot)(process, nullptr, g_poseForm);
      slot<PostAnimationFn>(process, VrGame::ProcessAddPostAnimationSlot)(process, nullptr, VrGame::PostAnimationPickIdle);
      g_posed    = true;
      g_poseWait = PoseRetryFrames;
      Logger::info("VR: The Caravan challenger raises a hand for the cards");
    }

    // Ends the pose as JIP LN's StopIdle does, if the challenger still has
    // it: nothing else ends an idle that loops for ever
    void endCaravanPose(uint8_t* challenger) {
      g_posed = false;

      if (!poseIdle(animationOf(challenger)))
        return;

      auto process = processOf(challenger);
      slot<FreeIdleFn>(process, VrGame::ProcessFreeSpecialIdleSlot)(process, nullptr, challenger);
      Logger::info("VR: The Caravan challenger lowers the hand");
    }

    // Called on the main thread at the start of the frame, before the AI
    // threads run, as a script's SetRestrained would be. Held only while
    // Caravan is the top menu and the console is closed: the world is still
    // under the pause menu or the console anyway, and a save made from them
    // never keeps the challenger restrained.
    void updateCaravanHold() {
      auto menu       = *reinterpret_cast<uint8_t* const*>(VrGame::CaravanInstance);
      bool onTop      = menu && isTopMenu(VrGame::CaravanMenuId) && !*reinterpret_cast<const uint8_t*>(VrGame::InConsole);
      auto challenger = onTop ? *reinterpret_cast<uint8_t* const*>(menu + VrGame::CaravanChallenger) : nullptr;
      bool alive      = challenger && lifeState(challenger) == VrGame::ActorLifeAlive;

      auto step = vrCaravanHoldStep(g_heldChallenger, challenger, alive);

      bool inGame = menu && *reinterpret_cast<const uint32_t*>(menu + VrGame::CaravanActiveMenu) == VrGame::CaravanGameMenu;

      if (g_posed && g_heldChallenger && (step.release || !inGame))
        endCaravanPose(g_heldChallenger);

      if (step.release) {
        // Left as it is if anything else changed it meanwhile
        if (lifeState(g_heldChallenger) == VrGame::ActorLifeRestrained) {
          reinterpret_cast<SetRestrainedFn>(VrGame::ActorSetRestrained)(g_heldChallenger, nullptr, false);
          Logger::info("VR: The Caravan challenger is released");
        }

        g_heldChallenger = nullptr;
      }

      if (step.hold) {
        reinterpret_cast<SetRestrainedFn>(VrGame::ActorSetRestrained)(challenger, nullptr, true);
        reinterpret_cast<EndMovementFn>(VrGame::ActorEndMovement)(challenger, nullptr);
        g_heldChallenger = challenger;
        g_poseWait       = 0;
        Logger::info("VR: The Caravan challenger is held in place");
      }

      // The cards are dealt in the game menu: none to hold before
      if (g_caravanPose && g_heldChallenger && inGame)
        keepCaravanPose(g_heldChallenger);
    }

  }


  void VrLiveCasino::install(bool casino, bool vigorTester, bool caravan) {
    // Without a way to hold the challenger, Caravan keeps the pause
    if (caravan && !(VrGameMemory::matches(VrGame::ActorSetRestrained,
          VrGame::ActorSetRestrainedPrologue, sizeof(VrGame::ActorSetRestrainedPrologue))
        && VrGameMemory::matches(VrGame::ActorEndMovement,
          VrGame::ActorEndMovementPrologue, sizeof(VrGame::ActorEndMovementPrologue)))) {
      Logger::warn("VR: The game's SetRestrained was not found, Caravan pauses the world");
      caravan = false;
    }

    // Without a way to play the idle, the challenger is held as they stand
    bool pose = caravan
      && VrGameMemory::matchesUnlessRewritten(VrGame::AnimationSpecialIdlePlaying,
        VrGame::AnimationSpecialIdlePlayingPrologue, sizeof(VrGame::AnimationSpecialIdlePlayingPrologue))
      && VrGameMemory::matchesUnlessRewritten(VrGame::FormById, VrGame::FormByIdPrologue, sizeof(VrGame::FormByIdPrologue));

    if (caravan && !pose)
      Logger::warn("VR: The game's idles were not found, the Caravan challenger holds no cards up");

    if (!casino && !vigorTester && !caravan)
      return;

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
      Logger::warn("VR: The game's menu mode tests were not found, the casino games and the vigor tester pause the world");
      return;
    }

    g_casino      = casino;
    g_vigorTester = vigorTester;
    g_caravan     = caravan;
    g_caravanPose = pose;

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
    if (g_caravan)
      updateCaravanHold();

    bool menuMode = realMenuMode();
    bool live     = menuMode && vrCasinoRunsLive(readState());
    bool wasLive  = g_live.load(std::memory_order_relaxed);

    if (live != wasLive) {
      Logger::info(live ? "VR: The world runs behind the menu" : "VR: The world no longer runs behind the menu");

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
