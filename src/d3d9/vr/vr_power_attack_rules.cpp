#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_sound.h"
#include "vr_power_attack_rules.h"

namespace dxvk {

  namespace {

    // PlayerCharacter::DamageActionPoints and IsGodMode
    using DamageActionPointsFn = void (__fastcall*)(const uint8_t* player, void* unused, float points);
    using IsGodModeFn          = bool (__cdecl*)();

    // PlayerCharacter::IsOverEncumbered and Actor::IsSwimming, __thiscall
    // without arguments
    using PlayerTestFn = bool (__fastcall*)(const uint8_t* player, void* unused);

    // BaseProcess's weapon entry, TESObjectWEAP::GetWeight(bool) and
    // ItemChange::HasModEffectActive(effect)
    using GetWeaponInfoFn   = const uint8_t* (__fastcall*)(const uint8_t* process, void* unused);
    using GetWeightFn       = float (__fastcall*)(const uint8_t* weapon, void* unused, bool modded);
    using HasModEffectFn    = bool (__fastcall*)(const uint8_t* entry, void* unused, uint32_t effect);

    // BGSEntryPoint::HandleEntryPoint, variadic: one float result here
    using HandleEntryPointFn = void (__cdecl*)(uint32_t entryPoint, const uint8_t* owner, float* result);

    // Interface::ShowMessage, __cdecl with the text, the face, the image,
    // the sound, the time and whether it is shown at once
    using ShowHudMessageFn = uint32_t (__cdecl*)(const char* text, uint32_t emotion, const char* image,
      const char* sound, float time, uint32_t instant);

    // How long the message is shown, in seconds, as V.A.T.S. shows its own
    constexpr float LowActionPointsTime = 2.0f;

    // The sound the inventory plays when action points are short
    constexpr const char* LowActionPointsSound = "UIVATSInsufficientAP";

    // ActorValueOwner::GetActorFloatValue
    using GetAvFloatFn = float (__fastcall*)(const uint8_t* owner, void* unused, uint32_t index);

    template<typename T>
    T readField(const void* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(base) + offset);
    }

    template<typename Fn>
    Fn virtualFunction(const void* object, uintptr_t slot) {
      return reinterpret_cast<Fn>(readField<uintptr_t>(readField<const void*>(object, 0), slot));
    }

    float settingFloat(uintptr_t setting) {
      return readField<float>(reinterpret_cast<const void*>(setting), VrGame::SettingValue);
    }

    const uint8_t* player() {
      return *reinterpret_cast<const uint8_t* const*>(VrGame::Player);
    }

    const char* settingString(uintptr_t setting) {
      const char* text = readField<const char*>(reinterpret_cast<const void*>(setting), VrGame::SettingValue);
      return text ? text : "";
    }

  }


  std::unique_ptr<VrPowerAttackRules> VrPowerAttackRules::install(const VrPowerAttackCost& cost) {
    bool found = VrGameMemory::readable(VrGame::PowerAttackDelay, VrGame::SettingValue + sizeof(float))
      && VrGameMemory::readable(VrGame::AppTimer, VrGame::TimerSize)
      && VrGameMemory::canChainCall(VrGame::ActorIsSwimmingCallSite, VrGame::ActorIsSwimming)
      && VrGameMemory::matches(VrGame::PlayerDamageActionPoints, VrGame::PlayerDamageActionPointsPrologue,
           sizeof(VrGame::PlayerDamageActionPointsPrologue))
      && VrGameMemory::matches(VrGame::PlayerIsGodMode, VrGame::PlayerIsGodModePrologue,
           sizeof(VrGame::PlayerIsGodModePrologue))
      && VrGameMemory::callsTo(VrGame::WeaponGetWeightCallSite, VrGame::WeaponGetWeight)
      && VrGameMemory::callsTo(VrGame::ItemHasModEffectCallSite, VrGame::ItemHasModEffect)
      && VrGameMemory::callsTo(VrGame::HandleEntryPointCallSite, VrGame::HandleEntryPoint)
      && VrGameMemory::readable(VrGame::PackRatModifier, VrGame::SettingValue + sizeof(float))
      && VrGameMemory::readable(VrGame::PackRatThreshold, VrGame::SettingValue + sizeof(float));

    if (!found)
      return nullptr;

    std::unique_ptr<VrPowerAttackRules> rules(new VrPowerAttackRules(cost));
    rules->m_tellsLowActionPoints = VrGameMemory::matches(VrGame::ActionPointsShortReadSite,
        VrGame::ActionPointsShortRead, sizeof(VrGame::ActionPointsShortRead))
      && VrGameMemory::matches(VrGame::LowApMessageReadSite,
        VrGame::LowApMessageRead, sizeof(VrGame::LowApMessageRead))
      && VrGameMemory::matches(VrGame::ShowHudMessage,
        VrGame::ShowHudMessagePrologue, sizeof(VrGame::ShowHudMessagePrologue));

    if (!rules->m_tellsLowActionPoints)
      Logger::warn("VR: The game's low action points message was not found, a refused power attack is only heard");

    Logger::info(str::format("VR: Holding the trigger for ", rules->holdTime(),
      " s charges a power attack with a melee weapon, for ", cost.base, " action points plus ",
      cost.weightMult, " per unit of weight, ", cost.max, " at most"));
    return rules;
  }


  float VrPowerAttackRules::holdTime() const {
    return settingFloat(VrGame::PowerAttackDelay);
  }


  float VrPowerAttackRules::frameTime() const {
    return readField<float>(reinterpret_cast<const void*>(VrGame::AppTimer), VrGame::TimerDelta);
  }


  bool VrPowerAttackRules::allowed(int32_t& cost, bool& lacksActionPoints) const {
    const uint8_t* pc = player();
    cost = 0;
    lacksActionPoints = false;

    if (!pc)
      return false;

    // The game makes a normal attack instead in both cases
    if (virtualFunction<PlayerTestFn>(pc, VrGame::PlayerOverEncumberedSlot)(pc, nullptr)
     || reinterpret_cast<PlayerTestFn>(VrGame::ActorIsSwimming)(pc, nullptr))
      return false;

    cost = vrComputePowerAttackCost(m_cost, weaponWeight(pc));

    if (reinterpret_cast<IsGodModeFn>(VrGame::PlayerIsGodMode)())
      return true;

    lacksActionPoints = float(cost) > actionPoints();
    return !lacksActionPoints;
  }


  void VrPowerAttackRules::tellLowActionPoints() const {
    vrPlayInterfaceSound(LowActionPointsSound);

    if (!m_tellsLowActionPoints)
      return;

    // "AP" and the word V.A.T.S. writes beside its meter, in the game's
    // language
    std::string text = str::format(settingString(VrGame::ActionPointsShortSetting), " ",
      settingString(VrGame::LowApMessageSetting));

    reinterpret_cast<ShowHudMessageFn>(VrGame::ShowHudMessage)(text.c_str(), VrGame::HudMessageSad,
      nullptr, nullptr, LowActionPointsTime, 0);
  }


  void VrPowerAttackRules::spend(int32_t cost) const {
    const uint8_t* pc = player();

    // The game's routine takes nothing in god mode
    if (pc && cost > 0)
      reinterpret_cast<DamageActionPointsFn>(VrGame::PlayerDamageActionPoints)(pc, nullptr, float(cost));
  }


  float VrPowerAttackRules::actionPoints() const {
    const uint8_t* pc = player();

    if (!pc)
      return 0.0f;

    const uint8_t* values = pc + VrGame::ActorValueOwnerBase;
    return virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerFloatSlot)(values, nullptr, VrGame::AvActionPoints);
  }


  float VrPowerAttackRules::weaponWeight(const uint8_t* pc) const {
    auto process = readField<const uint8_t*>(pc, VrGame::ActorProcess);

    if (!process)
      return 0.0f;

    auto entry  = virtualFunction<GetWeaponInfoFn>(process, VrGame::ProcessWeaponInfoSlot)(process, nullptr);
    auto weapon = entry ? readField<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;

    if (!weapon)
      return 0.0f;

    // As the inventory counts it: weight mods, then Pack Rat
    bool modded = reinterpret_cast<HasModEffectFn>(VrGame::ItemHasModEffect)(entry, nullptr, VrGame::WeaponModDecreaseWeight);
    float weight = reinterpret_cast<GetWeightFn>(VrGame::WeaponGetWeight)(weapon, nullptr, modded);

    float lightItems = 0.0f;
    reinterpret_cast<HandleEntryPointFn>(VrGame::HandleEntryPoint)(VrGame::EntryPointModifyLightItems, pc, &lightItems);

    if (lightItems > 0.0f && weight <= settingFloat(VrGame::PackRatThreshold))
      weight *= settingFloat(VrGame::PackRatModifier);

    return weight;
  }

}
