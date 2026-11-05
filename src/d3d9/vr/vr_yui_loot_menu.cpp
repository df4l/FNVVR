#include <windows.h>

#include <cstring>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_main_menu_prompts.h"
#include "vr_module_path.h"
#include "vr_yui_loot_menu.h"

namespace dxvk {

  namespace {

    constexpr wchar_t SettingsFile[] = L"Data\\NVSE\\Plugins\\yUI.ini";
    constexpr wchar_t MenuFile[]     = L"Data\\menus\\yUI\\LootMenu.xml";
    constexpr wchar_t DeclinedFile[] = L"fnvvr\\no_yui_loot_menu_prompt.txt";
    constexpr wchar_t Section[]      = L"General";
    constexpr wchar_t Key[]          = L"bLootMenu";

    /// Indices of the prompt's buttons
    constexpr int32_t TurnOffButton = 0;
    constexpr int32_t NeverButton   = 2;

    enum class Step {
      WaitForMenu,  ///< The prompt is shown once the menu is ready
      Asked,        ///< The prompt is open
      Report,       ///< A failure is shown once the menu is ready again
      Done,
    };

    const VrTexts* g_texts = nullptr;
    std::wstring   g_directory;
    Step           g_step = Step::WaitForMenu;

    const char* text(const char* name, const char* english) {
      return g_texts->text(name, english);
    }

    bool fileExists(const std::wstring& path) {
      return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    /// yUI turns it on when its settings do not mention it
    bool lootMenuOn() {
      return GetPrivateProfileIntW(Section, Key, 1, (g_directory + SettingsFile).c_str()) != 0;
    }

    bool turnOff() {
      std::wstring path = g_directory + SettingsFile;

      if (!WritePrivateProfileStringW(Section, Key, L"0", path.c_str())) {
        Logger::err(str::format("VR: Could not write ", str::fromws(path.c_str()), ": error ", GetLastError()));
        return false;
      }

      if (lootMenuOn()) {
        Logger::err(str::format("VR: ", str::fromws(path.c_str()), " still has yUI's Loot Menu on"));
        return false;
      }

      Logger::info("VR: yUI's Loot Menu is now off");
      return true;
    }

    void writeDeclinedFile() {
      std::wstring path = g_directory + DeclinedFile;
      CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);

      const char* declined = text("yuilootmenu.declinedfile",
        "While this file exists, FNVVR does not offer to turn off yUI's Loot Menu.\r\n"
        "Delete it to be asked again.");

      HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      DWORD written = 0;

      if (file != INVALID_HANDLE_VALUE
       && WriteFile(file, declined, DWORD(std::strlen(declined)), &written, nullptr)
       && written == std::strlen(declined))
        Logger::info("VR: The player keeps yUI's Loot Menu, this is not asked again");
      else
        Logger::warn(str::format("VR: Could not write ", str::fromws(path.c_str())));

      if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    }

    void __cdecl onPromptAnswered() {
      int32_t button = VrMainMenuPrompts::answer();
      g_step = Step::Done;

      if (button == TurnOffButton) {
        if (!turnOff())
          g_step = Step::Report;
      } else if (button == NeverButton) {
        writeDeclinedFile();
      } else {
        Logger::info("VR: The player postponed turning off yUI's Loot Menu");
      }
    }

    bool promptStep(bool ready) {
      switch (g_step) {
        case Step::WaitForMenu:
          if (ready) {
            g_step = Step::Asked;
            VrMainMenuPrompts::show(
              text("yuilootmenu.prompt",
                "yUI's Loot Menu does not work with VR controllers: containers and corpses may no longer open. "
                "FNVVR can turn it off, the rest of yUI stays on: bLootMenu is set to 0 in yUI.ini."),
              &onPromptAnswered,
              text("yuilootmenu.turnoff", "Turn it off"),
              text("yuilootmenu.later", "Not now"),
              text("yuilootmenu.never", "Don't ask again"));
          }
          return true;

        case Step::Report:
          if (ready) {
            g_step = Step::Done;
            VrMainMenuPrompts::show(
              text("yuilootmenu.failed",
                "yUI.ini could not be changed, the d3d9 log says why. "
                "Set bLootMenu to 0 in Data\\NVSE\\Plugins\\yUI.ini yourself."),
              nullptr, text("yuilootmenu.ok", "OK"));
          }
          return true;

        case Step::Asked:
          return true;

        case Step::Done:
          break;
      }

      return false;
    }

  }


  bool VrYuiLootMenu::install(const VrTexts& texts) {
    g_texts     = &texts;
    g_directory = vrModuleDirectory();

    if (!GetModuleHandleW(L"yUI.dll") || g_directory.empty()
     || !fileExists(g_directory + MenuFile) || !lootMenuOn())
      return false;

    Logger::warn("VR: yUI's Loot Menu is on. With ShowOff it reads the real gamepad and refuses containers "
      "opened with the controllers: bLootMenu = 0 in Data/NVSE/Plugins/yUI.ini turns it off");

    if (fileExists(g_directory + DeclinedFile))
      return false;

    if (!VrMainMenuPrompts::add(&promptStep)) {
      Logger::warn("VR: The main menu was not found, turning off yUI's Loot Menu is not offered");
      return false;
    }

    return true;
  }

}
