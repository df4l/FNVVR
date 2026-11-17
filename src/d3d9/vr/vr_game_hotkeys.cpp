#include <algorithm>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_hotkeys.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    using GetInventoryChangesFn = uint8_t* (__cdecl*)(uint8_t* reference);
    using GetHotKeyItemFn  = uint8_t* (__fastcall*)(uint8_t* changes, void* unused, uint32_t slot);
    using DeleteEntryFn    = void* (__fastcall*)(uint8_t* entry, void* unused, uint32_t flags);
    using EntryNameFn      = const char* (__fastcall*)(uint8_t* entry, void* unused);
    using EntryIconFn      = const char* (__fastcall*)(uint8_t* entry, void* unused, void* reference);
    using EntryWornFn      = uint32_t (__fastcall*)(uint8_t* entry, void* unused, uint32_t locked);
    using WheelSelectFn    = void (__fastcall*)(uint8_t* wheel, void* unused, int32_t slot);
    using WheelUseFn       = void (__fastcall*)(uint8_t* wheel, void* unused);
    using AnimActionFn     = int32_t (__fastcall*)(uint8_t* actor, void* unused);
    using IsTopMenuFn      = uint32_t (__cdecl*)(uint32_t menuId);
    using WheelVisibleFn   = uint32_t (__fastcall*)(uint8_t* wheel, void* unused);

    template<typename T>
    T field(const uint8_t* object, uintptr_t offset) {
      return *reinterpret_cast<const T*>(object + offset);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    uint8_t* inventory() {
      uint8_t* actor = player();
      return actor ? reinterpret_cast<GetInventoryChangesFn>(VrGame::GetInventoryChanges)(actor) : nullptr;
    }

    uint8_t* inventoryMenu() {
      if (!reinterpret_cast<IsTopMenuFn>(VrGame::IsTopMenu)(VrGame::InventoryMenuId))
        return nullptr;

      return *reinterpret_cast<uint8_t* const*>(VrGame::InventoryMenuInstance);
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

    // Replaces HotKeyWheel::HandleHotkeyInput(-1) in the HUD's update: what
    // the game does with the D-pad at rest
    int32_t __fastcall restingHudWheelInput(uint8_t* wheel, void*, int32_t) {
      reinterpret_cast<WheelSelectFn>(VrGame::HotKeyWheelSelect)(wheel, nullptr, -1);
      return -1;
    }

  }


  bool VrGameHotKeys::available() {
    static const bool found = [] {
      const Function functions[] = {
        function(VrGame::GetInventoryChanges,  VrGame::GetInventoryChangesPrologue),
        function(VrGame::GetHotKeyItem,        VrGame::GetHotKeyItemPrologue),
        function(VrGame::InventoryEntryDelete, VrGame::InventoryEntryDeletePrologue),
        function(VrGame::InventoryEntryName,   VrGame::InventoryEntryNamePrologue),
        function(VrGame::InventoryEntryIcon,   VrGame::InventoryEntryIconPrologue),
        function(VrGame::InventoryEntryWorn,   VrGame::InventoryEntryWornPrologue),
        function(VrGame::HotKeyWheelSelect,    VrGame::HotKeyWheelSelectPrologue),
        function(VrGame::HotKeyWheelUse,       VrGame::HotKeyWheelUsePrologue),
        function(VrGame::HotKeyWheelVisible,   VrGame::HotKeyWheelVisiblePrologue),
        function(VrGame::ActorGetAnimAction,   VrGame::ActorGetAnimActionPrologue),
        function(VrGame::IsTopMenu,            VrGame::IsTopMenuPrologue),
      };

      for (const Function& f : functions) {
        if (!VrGameMemory::matches(f.address, f.prologue, f.size)) {
          Logger::warn(str::format("VR: The game's hotkey function at 0x", std::hex, f.address,
            " is not the expected one, the quick wheel is off"));
          return false;
        }
      }

      return true;
    } ();

    return found;
  }


  void VrGameHotKeys::read(std::array<VrHotKeyItem, VrGame::HotKeySlotCount>& items) {
    items = { };
    uint8_t* changes = inventory();

    if (!changes)
      return;

    for (uint32_t slot = 0; slot < VrGame::HotKeySlotCount; slot++) {
      if (slot == VrGame::HotKeyAmmoSwapSlot)
        continue;

      uint8_t* entry = reinterpret_cast<GetHotKeyItemFn>(VrGame::GetHotKeyItem)(changes, nullptr, slot);

      if (!entry)
        continue;

      items[slot] = describe(entry);
      reinterpret_cast<DeleteEntryFn>(VrGame::InventoryEntryDelete)(entry, nullptr, 1);
    }
  }


  bool VrGameHotKeys::use(uint32_t slot) {
    uint8_t* actor = player();
    auto hud = *reinterpret_cast<uint8_t* const*>(VrGame::HudMainMenu);

    if (!actor || !hud || slot >= VrGame::HotKeySlotCount || slot == VrGame::HotKeyAmmoSwapSlot)
      return false;

    uint8_t* changes = inventory();
    uint8_t* entry   = changes ? reinterpret_cast<GetHotKeyItemFn>(VrGame::GetHotKeyItem)(changes, nullptr, slot) : nullptr;

    if (!entry)
      return false;

    VrHotKeyItem item = describe(entry);
    reinterpret_cast<DeleteEntryFn>(VrGame::InventoryEntryDelete)(entry, nullptr, 1);

    // The game would refuse it without a word
    bool gear = item.formType == VrGame::FormTypeWeapon || item.formType == VrGame::FormTypeArmor;

    if (gear && reinterpret_cast<AnimActionFn>(VrGame::ActorGetAnimAction)(actor, nullptr) != -1)
      return false;

    uint8_t* wheel = hud + VrGame::HudHotKeyWheel;
    reinterpret_cast<WheelSelectFn>(VrGame::HotKeyWheelSelect)(wheel, nullptr, int32_t(slot));
    reinterpret_cast<WheelUseFn>(VrGame::HotKeyWheelUse)(wheel, nullptr);
    return true;
  }


  VrHotKeyItem VrGameHotKeys::describe(uint8_t* entry) {
    VrHotKeyItem item;

    if (!entry)
      return item;

    item.form = field<const uint8_t*>(entry, VrGame::InventoryEntryForm);

    if (!item.form)
      return item;

    item.present  = true;
    item.formType = field<uint8_t>(item.form, VrGame::FormType);
    item.count    = uint32_t(std::max(field<int32_t>(entry, VrGame::InventoryEntryCount), 0));
    item.worn     = (reinterpret_cast<EntryWornFn>(VrGame::InventoryEntryWorn)(entry, nullptr, 0) & 0xFF) != 0;

    if (const char* name = reinterpret_cast<EntryNameFn>(VrGame::InventoryEntryName)(entry, nullptr))
      item.name = name;

    if (const char* icon = reinterpret_cast<EntryIconFn>(VrGame::InventoryEntryIcon)(entry, nullptr, nullptr))
      item.icon = icon;

    return item;
  }


  bool VrGameHotKeys::inventoryWheelShown() {
    uint8_t* menu = available() ? inventoryMenu() : nullptr;

    return menu && (reinterpret_cast<WheelVisibleFn>(VrGame::HotKeyWheelVisible)(
      menu + VrGame::InventoryMenuHotKeyWheel, nullptr) & 0xFF);
  }



  bool VrGameHotKeys::muteHudGamepadWheel() {
    if (!VrGameMemory::matches(VrGame::HotKeyWheelSelect,
        VrGame::HotKeyWheelSelectPrologue, sizeof(VrGame::HotKeyWheelSelectPrologue))
     || !VrGameMemory::redirectCall(VrGame::HudHotKeyInputCallSite, VrGame::HotKeyWheelHandleInput,
        reinterpret_cast<const void*>(&restingHudWheelInput))) {
      Logger::warn("VR: The HUD's hotkey wheel call was not found, a D-pad press left over from a menu can change the equipped item");
      return false;
    }

    Logger::info("VR: The HUD's hotkey wheel no longer reads the D-pad, the quick wheel uses the hotkeys");
    return true;
  }

}
