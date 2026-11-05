#include <vector>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_main_menu_prompts.h"

namespace dxvk {

  namespace {

    using MainMenuIdleFn  = uint8_t (__fastcall*)(void* menu, void* unused);
    using MessageBoxFn    = bool (__cdecl*)(const char* text, const char* icon, const char* title,
      void (__cdecl* callback)(), uint32_t firstButton, uint32_t type, float alpha, float width, ...);
    using MessageResultFn = int8_t (__cdecl*)();

    /// Frames the main menu stays idle before a prompt, so it does not
    /// appear while the menu settles
    constexpr uint32_t ReadyFrames = 30;

    /// Value of a message box's first button, the next ones follow it
    constexpr int8_t FirstButton = 1;

    bool g_hooked = false;
    std::vector<VrMainMenuPrompts::Prompt> g_prompts;
    size_t   g_current     = 0;
    uint32_t g_readyFrames = 0;

    /// The main menu has faded in, has no message box and is not loading content
    bool menuReady(const uint8_t* menu) {
      uint32_t flags = *reinterpret_cast<const uint32_t*>(menu + VrGame::StartMenuFlags);
      uint32_t busy = VrGame::StartMenuFadeIn | VrGame::StartMenuContentMessage | VrGame::StartMenuPreFade;

      return !(flags & busy) && !*reinterpret_cast<void* const*>(VrGame::MessageMenuGlobal);
    }

    uint8_t __fastcall mainMenuIdleHook(void* menu, void* unused) {
      uint8_t inMainMenu = reinterpret_cast<MainMenuIdleFn>(VrGame::MainMenuIdle)(menu, unused);

      if (inMainMenu && g_current < g_prompts.size()) {
        g_readyFrames = menuReady(static_cast<const uint8_t*>(menu)) ? g_readyFrames + 1 : 0;

        if (!g_prompts[g_current](g_readyFrames >= ReadyFrames)) {
          g_current++;
          g_readyFrames = 0;
        }
      }

      return inMainMenu;
    }

  }


  bool VrMainMenuPrompts::add(Prompt prompt) {
    if (!g_hooked) {
      g_hooked = VrGameMemory::matches(VrGame::ShowMessageBox,
          VrGame::ShowMessageBoxPrologue, sizeof(VrGame::ShowMessageBoxPrologue))
        && VrGameMemory::matches(VrGame::GetMessageMenuResult,
          VrGame::GetMessageMenuResultPrologue, sizeof(VrGame::GetMessageMenuResultPrologue))
        && VrGameMemory::redirectCall(VrGame::MainMenuIdleCallSite, VrGame::MainMenuIdle,
          reinterpret_cast<const void*>(&mainMenuIdleHook));
    }

    if (g_hooked)
      g_prompts.push_back(prompt);

    return g_hooked;
  }


  void VrMainMenuPrompts::show(
    const char*                 text,
          void                  (__cdecl* callback)(),
    const char*                 first,
    const char*                 second,
    const char*                 third) {
    // The game's list of buttons ends at the first null
    reinterpret_cast<MessageBoxFn>(VrGame::ShowMessageBox)(text, nullptr, nullptr, callback,
      FirstButton, VrGame::MessageBoxStartMenuType, 0.0f, 0.0f, first, second, third, nullptr);
  }


  int32_t VrMainMenuPrompts::answer() {
    return int32_t(reinterpret_cast<MessageResultFn>(VrGame::GetMessageMenuResult)()) - FirstButton;
  }

}
