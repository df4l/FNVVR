#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_ammo.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    using GetInventoryChangesFn  = uint8_t* (__cdecl*)(uint8_t* reference);
    using InventoryObjectCountFn = int32_t (__fastcall*)(uint8_t* changes, void* unused, const uint8_t* item);
    using ProcessEntryFn   = uint8_t* (__fastcall*)(uint8_t* process, void* unused);
    using SetCurrentAmmoFn = void (__fastcall*)(uint8_t* process, void* unused, uint8_t* entry);
    using EntryNameFn      = const char* (__fastcall*)(const void* entry, void* unused);
    using EntryHotKeyFn    = int32_t (__fastcall*)(uint8_t* entry, void* unused);
    using SetAmmoForHotKeyFn = void (__fastcall*)(uint8_t* player, void* unused, int32_t slot, const uint8_t* ammo);
    using AllocateFn       = uint8_t* (__cdecl*)(size_t size);
    using ConstructEntryFn = uint8_t* (__fastcall*)(uint8_t* entry, void* unused, const uint8_t* form, int32_t count);
    using AmmoSwapFn       = void (__fastcall*)(uint8_t* player, void* unused, uint32_t animate, uint32_t onlyIfEmpty);

    // Size of an inventory entry: extra lists, count, form
    constexpr size_t EntrySize = 0xC;

    template<typename T>
    T field(const uint8_t* object, uintptr_t offset) {
      return *reinterpret_cast<const T*>(object + offset);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    uint8_t* process() {
      uint8_t* actor = player();
      return actor ? field<uint8_t*>(actor, VrGame::ActorProcess) : nullptr;
    }

    template<typename Fn>
    Fn virtualFunction(const uint8_t* object, uintptr_t slot) {
      auto vtable = field<const uint8_t*>(object, 0);
      return reinterpret_cast<Fn>(field<uintptr_t>(vtable, slot));
    }

    uint8_t* weaponEntry() {
      uint8_t* p = process();
      return p ? virtualFunction<ProcessEntryFn>(p, VrGame::ProcessWeaponInfoSlot)(p, nullptr) : nullptr;
    }

    uint8_t* clipEntry() {
      uint8_t* p = process();
      return p ? virtualFunction<ProcessEntryFn>(p, VrGame::ProcessCurrentAmmoSlot)(p, nullptr) : nullptr;
    }

    int32_t heldRounds(const uint8_t* ammo) {
      uint8_t* actor   = player();
      uint8_t* changes = actor ? reinterpret_cast<GetInventoryChangesFn>(VrGame::GetInventoryChanges)(actor) : nullptr;
      return changes ? reinterpret_cast<InventoryObjectCountFn>(VrGame::InventoryObjectCount)(changes, nullptr, ammo) : 0;
    }

    struct Function {
      uintptr_t      address;
      const uint8_t* prologue;
      size_t         size;
    };

    template<size_t N>
    Function function(uintptr_t address, const uint8_t (&prologue)[N]) {
      return { address, prologue, N };
    }

  }


  bool VrGameAmmo::available() {
    static const bool found = [] {
      const Function functions[] = {
        function(VrGame::GetInventoryChanges,     VrGame::GetInventoryChangesPrologue),
        function(VrGame::InventoryObjectCount,    VrGame::InventoryObjectCountPrologue),
        function(VrGame::InventoryEntryName,      VrGame::InventoryEntryNamePrologue),
        function(VrGame::InventoryEntryHotKey,    VrGame::InventoryEntryHotKeyPrologue),
        function(VrGame::SetAmmoForHotKey,        VrGame::SetAmmoForHotKeyPrologue),
        function(VrGame::InventoryEntryConstruct, VrGame::InventoryEntryConstructPrologue),
        function(VrGame::AmmoSwapHelper,          VrGame::AmmoSwapHelperPrologue),
      };

      auto refuse = [] (uintptr_t address) {
        Logger::warn(str::format("VR: The game's ammunition function at 0x", std::hex, address,
          " is not the expected one, the ammunition cannot be chosen by hand"));
        return false;
      };

      for (const Function& f : functions) {
        if (!VrGameMemory::matches(f.address, f.prologue, f.size))
          return refuse(f.address);
      }

      if (!VrGameMemory::matchesUnlessRewritten(VrGame::GameAllocate,
          VrGame::GameAllocatePrologue, sizeof(VrGame::GameAllocatePrologue)))
        return refuse(VrGame::GameAllocate);

      return true;
    } ();

    return found;
  }


  std::vector<VrAmmoType> VrGameAmmo::types() {
    std::vector<VrAmmoType> result;
    uint8_t* entry = weaponEntry();
    auto weapon = entry ? field<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;
    auto ammo   = weapon ? field<const uint8_t*>(weapon, VrGame::WeaponAmmo) : nullptr;

    if (!ammo)
      return result;

    auto add = [&result] (const uint8_t* form) {
      if (form && field<uint8_t>(form, VrGame::FormType) == VrGame::FormTypeAmmo)
        result.push_back({ form, name(form), heldRounds(form) });
    };

    if (field<uint8_t>(ammo, VrGame::FormType) != VrGame::FormTypeList) {
      add(ammo);
      return result;
    }

    // BSSimpleList of forms: the item, then the next node
    for (auto node = ammo + VrGame::ListFormItems; node; node = field<const uint8_t*>(node, sizeof(void*)))
      add(field<const uint8_t*>(node, 0));

    return result;
  }


  const uint8_t* VrGameAmmo::current() {
    uint8_t* entry = clipEntry();
    return entry ? field<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;
  }


  bool VrGameAmmo::select(const uint8_t* ammo) {
    uint8_t* actor = player();
    uint8_t* p     = process();
    uint8_t* entry = weaponEntry();

    if (!ammo || !actor || !p || !entry || ammo == current())
      return false;

    int32_t slot = reinterpret_cast<EntryHotKeyFn>(VrGame::InventoryEntryHotKey)(entry, nullptr);

    if (slot >= 0)
      reinterpret_cast<SetAmmoForHotKeyFn>(VrGame::SetAmmoForHotKey)(actor, nullptr, slot, ammo);

    uint8_t* clip = reinterpret_cast<AllocateFn>(VrGame::GameAllocate)(EntrySize);

    if (!clip)
      return false;

    reinterpret_cast<ConstructEntryFn>(VrGame::InventoryEntryConstruct)(clip, nullptr, ammo, heldRounds(ammo));
    virtualFunction<SetCurrentAmmoFn>(p, VrGame::ProcessSetCurrentAmmoSlot)(p, nullptr, clip);

    // As the game's swap leaves it, then empty as a magazine just taken out
    if (uint8_t* current = clipEntry()) {
      *reinterpret_cast<void**>(current) = nullptr;
      *reinterpret_cast<int32_t*>(current + VrGame::InventoryEntryCount) = 0;
    }

    Logger::info(str::format("VR: The weapon's ammunition is now '", name(ammo), "', its clip is empty"));
    return true;
  }


  void VrGameAmmo::swapToNext() {
    if (uint8_t* actor = player())
      reinterpret_cast<AmmoSwapFn>(VrGame::AmmoSwapHelper)(actor, nullptr, 1, 0);
  }


  std::string VrGameAmmo::name(const uint8_t* ammo) {
    if (!ammo)
      return std::string();

    // The game's name of an inventory entry reads only its form
    const uint8_t* entry[3] = { nullptr, nullptr, ammo };
    const char* text = reinterpret_cast<EntryNameFn>(VrGame::InventoryEntryName)(entry, nullptr);
    return text ? text : std::string();
  }

}
