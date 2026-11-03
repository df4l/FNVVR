#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_scope_game.h"

namespace dxvk {

  namespace {

    using SetVisibleFn   = void (__cdecl*)(uint32_t visible);
    using IronSightsFn   = uint32_t (__fastcall*)(void* actor, void* unused);
    using WeaponEntryFn  = uint8_t* (__fastcall*)(void* process, void* unused);
    using ModEffectFn    = uint32_t (__fastcall*)(uint8_t* entry, void* unused, uint32_t effect, float* value);

    VrScopeGame* g_scopeGame = nullptr;

    template<typename T>
    T readField(const void* object, uintptr_t offset) {
      return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(object) + offset);
    }

    void* player() {
      return *reinterpret_cast<void* const*>(VrGame::Player);
    }

    uint8_t* equippedEntry() {
      void* actor   = player();
      void* process = actor ? readField<void*>(actor, VrGame::ActorProcess) : nullptr;

      if (!process)
        return nullptr;

      auto table = *reinterpret_cast<const uintptr_t*>(process);
      auto slot  = *reinterpret_cast<const uintptr_t*>(table + VrGame::ProcessWeaponInfoSlot);
      return reinterpret_cast<WeaponEntryFn>(slot)(process, nullptr);
    }

  }


  VrScopeGame::~VrScopeGame() {
    // The redirected calls stay in place and fall through to the game's behaviour
    g_scopeGame = nullptr;
  }


  std::unique_ptr<VrScopeGame> VrScopeGame::install() {
    bool found = VrGameMemory::callsTo(VrGame::GunScopeVisibleCallSite, VrGame::SetGunScopeVisible)
      && VrGameMemory::callsTo(VrGame::GunScopeShowCallSite, VrGame::SetGunScopeVisible)
      && VrGameMemory::callsTo(VrGame::GunScopeHideCallSite, VrGame::SetGunScopeVisible)
      && VrGameMemory::callsTo(VrGame::ScopeModelChildCallSite, VrGame::NodeChildAt)
      && VrGameMemory::callsTo(VrGame::IronSightsCullCallSite, VrGame::ObjectSetAppCulled)
      && VrGameMemory::callsTo(VrGame::ImageSpaceIronSightsCallSite, VrGame::ImageSpaceIronSights)
      && VrGameMemory::callsTo(VrGame::NightScopeCallSite, VrGame::WeaponHasNightScope)
      && VrGameMemory::callsTo(VrGame::GunSkillIronSightsCallSite, VrGame::ActorGetIronSights);

    if (!found) {
      Logger::warn("VR: The game's scope calls were not found, its flat scope stays");
      return nullptr;
    }

    std::unique_ptr<VrScopeGame> game(new VrScopeGame());

    VrGameMemory::redirectCall(VrGame::GunScopeVisibleCallSite, VrGame::SetGunScopeVisible,
      reinterpret_cast<const void*>(&VrScopeGame::setGunScopeVisibleHook));
    VrGameMemory::redirectCall(VrGame::GunScopeShowCallSite, VrGame::SetGunScopeVisible,
      reinterpret_cast<const void*>(&VrScopeGame::setGunScopeVisibleHook));
    VrGameMemory::redirectCall(VrGame::GunScopeHideCallSite, VrGame::SetGunScopeVisible,
      reinterpret_cast<const void*>(&VrScopeGame::setGunScopeVisibleHook));
    VrGameMemory::redirectCall(VrGame::ScopeModelChildCallSite, VrGame::NodeChildAt,
      reinterpret_cast<const void*>(&VrScopeGame::scopeModelChildHook));
    VrGameMemory::redirectCall(VrGame::IronSightsCullCallSite, VrGame::ObjectSetAppCulled,
      reinterpret_cast<const void*>(&VrScopeGame::ironSightsCullHook));
    VrGameMemory::redirectCall(VrGame::ImageSpaceIronSightsCallSite, VrGame::ImageSpaceIronSights,
      reinterpret_cast<const void*>(&VrScopeGame::imageSpaceIronSightsHook));
    VrGameMemory::redirectCall(VrGame::NightScopeCallSite, VrGame::WeaponHasNightScope,
      reinterpret_cast<const void*>(&VrScopeGame::nightScopeHook));
    VrGameMemory::redirectCall(VrGame::GunSkillIronSightsCallSite, VrGame::ActorGetIronSights,
      reinterpret_cast<const void*>(&VrScopeGame::gunSkillIronSightsHook));

    g_scopeGame = game.get();

    Logger::info("VR: The game's flat scope, its blur and its night vision are off, scopes are lenses on the weapons");
    return game;
  }


  bool VrScopeGame::readEquipped(VrEquippedScope& scope) {
    uint8_t* entry = equippedEntry();
    auto weapon    = entry ? readField<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;

    if (!weapon)
      return false;

    scope.weaponModel = readField<const char*>(weapon, VrGame::WeaponModelPath);
    scope.scopeModel  = readField<const char*>(weapon, VrGame::WeaponScopeModelPath);

    if (!scope.scopeModel || !scope.scopeModel[0])
      return false;

    // The game's own test: a scope that is a mod counts once it is fitted, and
    // the mod's value narrows the view (PlayerCharacter::DoIronSightsZoom)
    float modValue = 0.0f;

    bool modFitted = reinterpret_cast<ModEffectFn>(VrGame::ItemModEffectValue)(
      entry, nullptr, VrGame::ModEffectIronSights, &modValue) & 0xFF;

    uint32_t flags = readField<uint32_t>(weapon, VrGame::WeaponFlags2);

    if ((flags & VrGame::WeaponFlag2ModScope) && !modFitted)
      return false;

    scope.sightFov    = readField<float>(weapon, VrGame::WeaponSightFov) - (modFitted ? modValue : 0.0f);
    scope.nightVision = (flags & VrGame::WeaponFlag2NightScope) != 0;
    return scope.sightFov > 0.0f;
  }


  void __cdecl VrScopeGame::setGunScopeVisibleHook(uint32_t visible) {
    // Hiding it still leaves the game's state as it expects
    reinterpret_cast<SetVisibleFn>(VrGame::SetGunScopeVisible)(0);
  }


  void* __fastcall VrScopeGame::scopeModelChildHook(void* node, void* unused, uint32_t index) {
    // HUDMainMenu::SetMenuMode: without a scope model the menu mode stays
    // the iron sights', which keeps the HUD
    return nullptr;
  }


  void __fastcall VrScopeGame::ironSightsCullHook(void* object, void* unused, uint32_t culled) {
    // The first-person model stays: the scope is on it
  }


  void __fastcall VrScopeGame::imageSpaceIronSightsHook(void* player, void* unused) {
    // No depth of field or motion blur on the whole view while aiming
  }


  uint32_t __fastcall VrScopeGame::nightScopeHook(const void* weapon, void* unused) {
    // The lens shows the dark, not the whole view, see VrScopeLens::nightVision
    return 0;
  }


  uint32_t __fastcall VrScopeGame::gunSkillIronSightsHook(void* actor, void* unused) {
    uint32_t aiming = reinterpret_cast<IronSightsFn>(VrGame::ActorGetIronSights)(actor, nullptr) & 0xFF;

    if (aiming || !g_scopeGame || !g_scopeGame->m_steadied || actor != player())
      return aiming;

    if (!g_scopeGame->m_loggedSteadied) {
      Logger::info("VR: A weapon held with both hands has the precision of the game's aim");
      g_scopeGame->m_loggedSteadied = true;
    }

    return 1;
  }

}
