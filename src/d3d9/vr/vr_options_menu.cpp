#include <cstdint>
#include <utility>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_options_menu.h"

namespace dxvk {

  namespace {

    // The page's own bit in the options' masks. The game uses 0x01 to 0x40
    // for its pages and the main menu, and a few higher bits for its
    // confirmation lists.
    constexpr uint32_t PageMask = 0x4000;

    // UserOption types: 0 toggles and wraps around from the last value,
    // 2 is a slider with a position per value
    constexpr int32_t  ToggleType   = 0;
    constexpr uint32_t ToggleValues = 2;
    constexpr int32_t  SliderType   = 2;

    // Space between the arrows of a toggle, the same as the game's toggles
    // with long value names
    constexpr uint32_t ArrowSpacing = 0x55;

    // The game keeps these pointers and never frees them
    constexpr char PageText[]       = "VR";
    constexpr char ConsoleText[]    = "Console";
    constexpr char TurnText[]       = "Turning";
    constexpr char TurnSnapText[]   = "Snap";
    constexpr char TurnSmoothText[] = "Smooth";
    constexpr char SpeedText[]      = "Smooth turn speed";
    constexpr char PipBoySizeText[] = "Pip-Boy size";
    constexpr char ReloadText[]     = "Manual reload";
    constexpr char ReloadOffText[]  = "Off";
    constexpr char ReloadOnText[]   = "On";
    constexpr char SneakText[]      = "Sneak";
    constexpr char SneakButtonText[] = "Stick click";
    constexpr char SneakCrouchText[] = "Real crouch";
    constexpr char ScopeText[]      = "Scope lens";
    constexpr char RoomscaleText[]  = "Room-scale walking";

    // One name per size of VrPipBoyScaleSteps, from the game's size up
    constexpr const char* PipBoySizeNames[VrPipBoyScaleSteps] = {
      "Normal", "125%", "150%", "175%", "200%" };

    using AppendFn   = int   (__fastcall*)(void* array, void* unused, void** option);
    using HeapFn     = void* (__cdecl*)(size_t size);
    using OptionCtorFn = void* (__thiscall*)(void* self, const char* text, void (__cdecl* callback)(), uint32_t mask);
    using UserOptionCallback = void (__cdecl*)(void* option);
    // A toggle's value names follow, one pointer per setting; a slider has none
    using UserOptionCtorFn = void* (__cdecl*)(void* self, const char* text, UserOptionCallback callback,
      uint32_t mask, int32_t type, uint32_t count, uint32_t spacing, ...);
    using PopulateFn = void (__thiscall*)(void* menu, uint32_t mask);
    using SetEnabledFn = void (__thiscall*)(void* list, bool enabled);
    using GetFloatFn = float (__thiscall*)(void* tile, int32_t trait);
    using SetFloatFn = void (__thiscall*)(void* tile, int32_t trait, float value, bool propagate);
    using AddTraitFn = int32_t (__cdecl*)(const char* name, int32_t id);
    using ListInitFn = void* (__thiscall*)(void* list, void* tile, const char* itemTemplate);
    using IsForModesFn = bool (__thiscall*)(void* option, uint32_t modes);
    using IsInStartMenuFn = bool (__cdecl*)();

    VrOptions                    g_options;
    VrOptionsMenu::ChangedFn     g_onChanged;
    VrOptionsMenu::ConsoleFn     g_onConsole;
    void*                        g_consoleOption = nullptr;

    uint8_t* startMenu() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::StartMenuGlobal);
    }


    // The entry in the Settings list: the same steps as the game's entries
    // for its own pages, with the page's mask
    void __cdecl choosePage() {
      uint8_t* menu = startMenu();

      if (!menu)
        return;

      reinterpret_cast<SetEnabledFn>(VrGame::SetListEnabled)(menu + VrGame::StartMenuSettingsList, false);
      reinterpret_cast<PopulateFn>(VrGame::PopulateOptions)(menu, PageMask);

      // Showing a page ends by placing it with a value per page, which the
      // game only knows for its own pages
      void* tile = *reinterpret_cast<void**>(menu + VrGame::StartMenuTile);

      if (!tile)
        return;

      float center = reinterpret_cast<GetFloatFn>(VrGame::TileGetFloat)(tile, int32_t(VrGame::PageCenterTraitGameplay));
      int32_t trait = reinterpret_cast<AddTraitFn>(VrGame::AddUserTrait)(VrGame::CenterXTraitName, -1);

      if (trait != INT32_MIN)
        reinterpret_cast<SetFloatFn>(VrGame::TileSetFloat)(tile, trait, center, true);
    }


    // The callbacks of the options are called by the game twice: when the
    // options are initialised from their settings, which is when they
    // set their value, and when the player changes one
    bool optionsInitialised() {
      uint8_t* menu = startMenu();

      return menu
        && (*reinterpret_cast<const uint32_t*>(menu + VrGame::StartMenuFlags) & VrGame::StartMenuOptionsInitialised);
    }


    uint32_t& optionValue(void* option) {
      return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(option) + VrGame::UserOptionValue);
    }


    uint32_t& optionDefault(void* option) {
      return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(option) + VrGame::UserOptionDefaultValue);
    }


    void notifyChanged() {
      if (g_onChanged)
        g_onChanged(g_options);
    }


    // The Console entry of the Settings list. The game disabled the list
    // before calling this, as before opening one of its pages; the
    // console opens over the list instead, which stays usable.
    void __cdecl chooseConsole() {
      uint8_t* menu = startMenu();

      if (menu)
        reinterpret_cast<SetEnabledFn>(VrGame::SetListEnabled)(menu + VrGame::StartMenuSettingsList, true);

      if (g_onConsole)
        g_onConsole();
    }


    void __cdecl onTurnChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = g_options.smoothTurn ? 1u : 0u;
        optionDefault(option) = 0u;
        return;
      }

      bool smooth = optionValue(option) != 0;

      if (smooth == g_options.smoothTurn)
        return;

      g_options.smoothTurn = smooth;
      notifyChanged();
    }


    void __cdecl onSpeedChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = vrSmoothTurnSpeedToStep(g_options.smoothTurnSpeed);
        optionDefault(option) = vrSmoothTurnSpeedToStep(VrOptions::DefaultSmoothTurnSpeed);
        return;
      }

      float speed = vrSmoothTurnStepToSpeed(optionValue(option));

      // The options can hold a speed between two positions, which stays
      // until the player moves the slider
      if (vrSmoothTurnSpeedToStep(g_options.smoothTurnSpeed) == optionValue(option))
        return;

      g_options.smoothTurnSpeed = speed;
      notifyChanged();
    }


    void __cdecl onPipBoySizeChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = vrPipBoyScaleToStep(g_options.pipBoyScale);
        optionDefault(option) = 0u;
        return;
      }

      // As with the speed, a size between two names stays until changed
      if (vrPipBoyScaleToStep(g_options.pipBoyScale) == optionValue(option))
        return;

      g_options.pipBoyScale = vrPipBoyStepToScale(optionValue(option));
      notifyChanged();
    }


    void __cdecl onReloadChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = g_options.manualReload ? 1u : 0u;
        optionDefault(option) = 1u;
        return;
      }

      bool manual = optionValue(option) != 0;

      if (manual == g_options.manualReload)
        return;

      g_options.manualReload = manual;
      notifyChanged();
    }


    void __cdecl onSneakChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = g_options.physicalSneak ? 1u : 0u;
        optionDefault(option) = 0u;
        return;
      }

      bool physical = optionValue(option) != 0;

      if (physical == g_options.physicalSneak)
        return;

      g_options.physicalSneak = physical;
      notifyChanged();
    }


    void __cdecl onScopeChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = g_options.scopeLens ? 1u : 0u;
        optionDefault(option) = 1u;
        return;
      }

      bool lens = optionValue(option) != 0;

      if (lens == g_options.scopeLens)
        return;

      g_options.scopeLens = lens;
      notifyChanged();
    }


    void __cdecl onRoomscaleChanged(void* option) {
      if (!optionsInitialised()) {
        optionValue(option)   = g_options.roomscale ? 1u : 0u;
        optionDefault(option) = 1u;
        return;
      }

      bool roomscale = optionValue(option) != 0;

      if (roomscale == g_options.roomscale)
        return;

      g_options.roomscale = roomscale;
      notifyChanged();
    }


    // Creates a user option and adds it to the game's list
    template<typename... Names>
    void* appendUserOption(const char* text, UserOptionCallback callback, int32_t type,
        uint32_t count, uint32_t spacing, Names... names) {
      void* memory = reinterpret_cast<HeapFn>(VrGame::HeapAllocate)(VrGame::UserOptionSize);

      if (!memory)
        return nullptr;

      void* option = reinterpret_cast<UserOptionCtorFn>(VrGame::UserOptionConstruct)(
        memory, text, callback, PageMask, type, count, spacing, names...);

      reinterpret_cast<AppendFn>(VrGame::OptionArrayAppend)(
        reinterpret_cast<void*>(VrGame::UserOptionArray), nullptr, &option);
      return option;
    }


    void appendPage(void* optionArray) {
      auto append = reinterpret_cast<AppendFn>(VrGame::OptionArrayAppend);
      auto allocate = reinterpret_cast<HeapFn>(VrGame::HeapAllocate);

      void* optionMemory = allocate(VrGame::OptionSize);

      if (!optionMemory)
        return;

      void* option = reinterpret_cast<OptionCtorFn>(VrGame::OptionConstruct)(
        optionMemory, PageText, &choosePage, VrGame::OptionMaskSettingsList);

      append(optionArray, nullptr, &option);

      void* consoleMemory = allocate(VrGame::OptionSize);

      if (consoleMemory) {
        void* console = reinterpret_cast<OptionCtorFn>(VrGame::OptionConstruct)(
          consoleMemory, ConsoleText, &chooseConsole, VrGame::OptionMaskSettingsList);

        append(optionArray, nullptr, &console);
        g_consoleOption = console;
      }

      // The page lists its options in the order they are added
      appendUserOption(TurnText, &onTurnChanged, ToggleType, ToggleValues, ArrowSpacing,
        TurnSnapText, TurnSmoothText);
      appendUserOption(SpeedText, &onSpeedChanged, SliderType, VrSmoothTurnSpeedSteps, 0);
      appendUserOption(PipBoySizeText, &onPipBoySizeChanged, ToggleType, VrPipBoyScaleSteps, ArrowSpacing,
        PipBoySizeNames[0], PipBoySizeNames[1], PipBoySizeNames[2], PipBoySizeNames[3], PipBoySizeNames[4]);
      appendUserOption(ReloadText, &onReloadChanged, ToggleType, ToggleValues, ArrowSpacing,
        ReloadOffText, ReloadOnText);
      appendUserOption(SneakText, &onSneakChanged, ToggleType, ToggleValues, ArrowSpacing,
        SneakButtonText, SneakCrouchText);
      appendUserOption(ScopeText, &onScopeChanged, ToggleType, ToggleValues, ArrowSpacing,
        ReloadOffText, ReloadOnText);
      appendUserOption(RoomscaleText, &onRoomscaleChanged, ToggleType, ToggleValues, ArrowSpacing,
        ReloadOffText, ReloadOnText);
    }


    // Replaces the call that adds the Controls entry to the Settings list:
    // adds it, then the VR entry and its options, and the Console entry
    int __fastcall appendControlsEntry(void* optionArray, void* unused, void** option) {
      int index = reinterpret_cast<AppendFn>(VrGame::OptionArrayAppend)(optionArray, nullptr, option);
      appendPage(optionArray);
      return index;
    }


    // The console is only opened in game: the main menu has no world to
    // draw its terminal in. StartMenu::_Create sets the menu's in-game
    // flag before it fills its lists.
    bool showsConsoleEntry() {
      return !reinterpret_cast<IsInStartMenuFn>(VrGame::IsInStartMenu)();
    }


    // Replaces the call that tells whether an option goes in the Settings
    // list, to leave the Console entry out of the main menu's
    bool __fastcall isSettingsEntry(void* option, void* unused, uint32_t modes) {
      if (option == g_consoleOption && !showsConsoleEntry())
        return false;

      return reinterpret_cast<IsForModesFn>(VrGame::OptionIsForModes)(option, modes);
    }


    // Number of entries of the Settings list, the game's and the fork's
    uint32_t settingsEntryCount() {
      auto array = reinterpret_cast<const uint8_t*>(VrGame::OptionArray);
      auto options = *reinterpret_cast<uint8_t* const* const*>(array + VrGame::OptionArrayData);
      uint32_t count = *reinterpret_cast<const uint32_t*>(array + VrGame::OptionArrayCount);
      uint32_t entries = 0;

      for (uint32_t i = 0; i < count; i++) {
        if (options[i] && (*reinterpret_cast<const uint32_t*>(options[i] + VrGame::OptionMask) & VrGame::OptionMaskSettingsList)
         && (options[i] != g_consoleOption || showsConsoleEntry()))
          entries += 1;
      }

      return entries;
    }


    // Replaces the call that sets up the Settings list box: the list takes
    // its height from the number of visible items when its first entry is
    // added, and start_menu.xml leaves room for five, so the entries after
    // the fifth would be cut off
    void* __fastcall initSettingsList(void* list, void* unused, void* tile, const char* itemTemplate) {
      void* result = reinterpret_cast<ListInitFn>(VrGame::ListBoxInitialize)(list, tile, itemTemplate);

      if (!tile)
        return result;

      int32_t trait = reinterpret_cast<AddTraitFn>(VrGame::AddUserTrait)(VrGame::VisibleItemsTraitName, -1);

      if (trait == INT32_MIN)
        return result;

      float visible = reinterpret_cast<GetFloatFn>(VrGame::TileGetFloat)(tile, trait);
      float entries = float(settingsEntryCount());

      if (entries > visible)
        reinterpret_cast<SetFloatFn>(VrGame::TileSetFloat)(tile, trait, entries, true);

      return result;
    }


    bool supported() {
      return VrGameMemory::matches(VrGame::OptionConstruct,
          VrGame::OptionConstructPrologue, sizeof(VrGame::OptionConstructPrologue))
        && VrGameMemory::matches(VrGame::UserOptionConstruct,
          VrGame::UserOptionConstructPrologue, sizeof(VrGame::UserOptionConstructPrologue))
        && VrGameMemory::matches(VrGame::PopulateOptions,
          VrGame::PopulateOptionsPrologue, sizeof(VrGame::PopulateOptionsPrologue))
        && VrGameMemory::matches(VrGame::OptionArrayAppend,
          VrGame::OptionArrayAppendPrologue, sizeof(VrGame::OptionArrayAppendPrologue))
        && VrGameMemory::matches(VrGame::SetListEnabled,
          VrGame::SetListEnabledPrologue, sizeof(VrGame::SetListEnabledPrologue))
        && VrGameMemory::matches(VrGame::TileGetFloat,
          VrGame::TileGetFloatPrologue, sizeof(VrGame::TileGetFloatPrologue))
        && VrGameMemory::matches(VrGame::TileSetFloat,
          VrGame::TileSetFloatPrologue, sizeof(VrGame::TileSetFloatPrologue))
        && VrGameMemory::matches(VrGame::AddUserTrait,
          VrGame::AddUserTraitPrologue, sizeof(VrGame::AddUserTraitPrologue))
        && VrGameMemory::matches(VrGame::HeapAllocate,
          VrGame::HeapAllocatePrologue, sizeof(VrGame::HeapAllocatePrologue))
        && VrGameMemory::matches(VrGame::ListBoxInitialize,
          VrGame::ListBoxInitializePrologue, sizeof(VrGame::ListBoxInitializePrologue))
        && VrGameMemory::matches(VrGame::OptionIsForModes,
          VrGame::OptionIsForModesPrologue, sizeof(VrGame::OptionIsForModesPrologue))
        && VrGameMemory::matches(VrGame::IsInStartMenu,
          VrGame::IsInStartMenuPrologue, sizeof(VrGame::IsInStartMenuPrologue))
        && VrGameMemory::readable(VrGame::StartMenuGlobal, sizeof(uintptr_t));
    }

  }


  bool VrOptionsMenu::install(const VrOptions& options, ChangedFn onChanged, ConsoleFn onConsole) {
    if (!supported()) {
      Logger::warn("VR: The options page is not available, the executable is not the supported version");
      return false;
    }

    g_options   = options;
    g_onChanged = std::move(onChanged);
    g_onConsole = std::move(onConsole);

    if (!VrGameMemory::redirectCall(VrGame::ControlsOptionAppendCallSite,
        VrGame::OptionArrayAppend, reinterpret_cast<const void*>(&appendControlsEntry))) {
      Logger::warn("VR: The options page is not available, the settings list was not hooked");
      return false;
    }

    if (!VrGameMemory::redirectCall(VrGame::SettingsListInitCallSite,
        VrGame::ListBoxInitialize, reinterpret_cast<const void*>(&initSettingsList)))
      Logger::warn("VR: The Settings list keeps its height, its last entries may be cut off");

    if (!VrGameMemory::redirectCall(VrGame::SettingsEntryModesCallSite,
        VrGame::OptionIsForModes, reinterpret_cast<const void*>(&isSettingsEntry)))
      Logger::warn("VR: The Console entry is also shown in the main menu's Settings list");

    Logger::info("VR: Added the VR page to the Settings menu");
    return true;
  }

}
