#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_comfort_settings.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    using SetKillCamModeFn = void (__thiscall*)(uintptr_t player, int32_t mode);

    using SetFirstPersonFn = void (__thiscall*)(uintptr_t player, uint8_t first);

    using Show1stPersonFn = void (__thiscall*)(uintptr_t player, uint8_t first);

    // Actor::IsDead, __thiscall with a bool, returns a bool in AL
    using IsDeadFn = uint32_t (__thiscall*)(uintptr_t actor, uint32_t notEssential);

  }


  std::unique_ptr<VrComfortSettings> VrComfortSettings::install() {
    bool found = VrGameMemory::matches(VrGame::AutoVanityCheckSite,
        VrGame::AutoVanityCheck, sizeof(VrGame::AutoVanityCheck))
      && VrGameMemory::canChainCall(VrGame::AutoVanityCallSite, VrGame::SettingIsOff)
      && VrGameMemory::matches(VrGame::KillCamModeStoreSite,
        VrGame::KillCamModeStore, sizeof(VrGame::KillCamModeStore))
      && VrGameMemory::matches(VrGame::SetKillCamModeCallback,
        VrGame::SetKillCamModeCallbackPrologue, sizeof(VrGame::SetKillCamModeCallbackPrologue))
      && VrGameMemory::matches(VrGame::SetFirstPerson,
        VrGame::SetFirstPersonPrologue, sizeof(VrGame::SetFirstPersonPrologue))
      && VrGameMemory::matches(VrGame::Show1stPerson,
        VrGame::Show1stPersonPrologue, sizeof(VrGame::Show1stPersonPrologue))
      && VrGameMemory::readable(VrGame::VanityModeActive, sizeof(uint8_t))
      && VrGameMemory::matches(VrGame::TempThirdPersonReadSite,
        VrGame::TempThirdPersonRead, sizeof(VrGame::TempThirdPersonRead))
      && VrGameMemory::readable(VrGame::VatsCameraState, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::DisableAutoVanitySetting + VrGame::SettingValue, sizeof(uint8_t))
      && VrGameMemory::readable(VrGame::Player, sizeof(uintptr_t));

    if (!found)
      return nullptr;

    *reinterpret_cast<uint8_t*>(VrGame::DisableAutoVanitySetting + VrGame::SettingValue) = 1;
    Logger::info("VR: The idle third-person camera is turned off");

    return std::unique_ptr<VrComfortSettings>(new VrComfortSettings());
  }


  void VrComfortSettings::update(bool inGame) {
    uintptr_t player = *reinterpret_cast<const uintptr_t*>(VrGame::Player);

    if (!player)
      return;

    if (inGame)
      keepFirstPerson(player);

    int32_t mode = *reinterpret_cast<const int32_t*>(player + VrGame::PlayerKillCamMode);

    if (mode == VrGame::KillCamPlayer)
      return;

    reinterpret_cast<SetKillCamModeFn>(VrGame::SetKillCamModeCallback)(player, VrGame::KillCamPlayer);

    if (!m_loggedKillCam) {
      Logger::info(str::format("VR: The kill camera is set from mode ", mode,
        " to the first-person slow motion"));
      m_loggedKillCam = true;
    }
  }



  void VrComfortSettings::keepFirstPerson(uintptr_t player) {
    if (!*reinterpret_cast<const uint8_t*>(player + VrGame::PlayerWantsThirdPerson)) {
      showFirstPersonModel(player);
      return;
    }

    // V.A.T.S. plays some of its shots in third person, and the game shows
    // the dead player in third person
    if (*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState))
      return;

    // Sitting down and getting up play on the third-person body: in first
    // person only the camera followed the animation, and the player was
    // never placed on the seat. The game comes back to first person itself.
    if (*reinterpret_cast<const uint8_t*>(player + VrGame::PlayerTempThirdPerson)) {
      if (!m_loggedTemp) {
        Logger::info("VR: The game shows the third person for a moment (furniture), it is left to it");
        m_loggedTemp = true;
      }
      return;
    }

    auto vtable = *reinterpret_cast<const uintptr_t*>(player);
    auto isDead = *reinterpret_cast<const IsDeadFn*>(vtable + VrGame::ActorIsDeadSlot);

    if (isDead(player, 0) & 0xFF)
      return;

    reinterpret_cast<SetFirstPersonFn>(VrGame::SetFirstPerson)(player, 1);

    if (!m_loggedView) {
      Logger::info("VR: The game asked for the third-person view, the first person is put back");
      m_loggedView = true;
    }
  }


  void VrComfortSettings::showFirstPersonModel(uintptr_t player) {
    // The camera update shows the first-person model once, when the view
    // goes back to first person. If the temporary third person of a seat
    // is set at that moment (a seat used again while the camera was still
    // coming back), it refuses and leaves the hands culled until the next
    // view switch.
    // The game only tries again under AI control.
    auto byte = [player] (uintptr_t offset) {
      return *reinterpret_cast<const uint8_t*>(player + offset);
    };

    if (byte(VrGame::PlayerCurrentThirdPerson)
     || !byte(VrGame::PlayerThirdPerson)
     || byte(VrGame::PlayerTempThirdPerson)
     || *reinterpret_cast<const uint8_t*>(VrGame::VanityModeActive)
     || *reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState))
      return;

    auto vtable = *reinterpret_cast<const uintptr_t*>(player);
    auto isDead = *reinterpret_cast<const IsDeadFn*>(vtable + VrGame::ActorIsDeadSlot);

    if (isDead(player, 0) & 0xFF)
      return;

    reinterpret_cast<Show1stPersonFn>(VrGame::Show1stPerson)(player, 1);

    if (!m_loggedModel) {
      Logger::info("VR: The game went back to first person with the third-person model shown, the first-person one is put back");
      m_loggedModel = true;
    }
  }

}
