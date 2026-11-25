#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_menu_cursor.h"

namespace dxvk {

  namespace {

    // Both are __thiscall without arguments
    using PollFn         = void (__fastcall*)(void* input, void* unused);
    using UpdateCursorFn = void (__fastcall*)(void* interfaceManager, void* unused);

    using InterfaceSizeFn = float (__cdecl*)();
    using IsMenuActiveFn  = bool (__cdecl*)(uint32_t menuId, uint32_t unused);

    constexpr uint8_t ButtonDown = 0x80;

    using MouseAxisFn = int32_t (__fastcall*)(void* input, void* unused, int32_t axis);

    bool g_installed = false;

    VrPointerState g_state;

    uintptr_t g_pluginMouseWheel = 0;

    /// What the poll call reached before us, the game's function or a plugin's hook
    uintptr_t g_poll = VrGame::Poll;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
    }

    /// What the hook gave the game at the last poll it wrote, and whether it wrote
    uint8_t g_lastButton = 0;
    bool    g_wrote      = false;

    void __fastcall pollHook(void* input, void* unused) {
      reinterpret_cast<PollFn>(g_poll)(input, unused);

      uint32_t left = field<int32_t>(input, VrGame::InputMouseSwapped) ? 1 : 0;

      if (!g_state.cursorActive) {
        // Without a mouse device the game leaves the mouse state alone: let
        // go of the button and the wheel once, without a release the
        // interface would take for a click
        if (g_wrote) {
          field<int32_t>(input, VrGame::InputMouseWheel) = 0;
          field<uint8_t>(input, VrGame::InputMouseButtons + left)       = 0;
          field<uint8_t>(input, VrGame::InputMouseButtonsBefore + left) = 0;
          g_lastButton = 0;
          g_wrote      = false;
        }
        return;
      }

      // The laser places the cursor, the real mouse would move it away
      field<int32_t>(input, VrGame::InputMouseX)     = 0;
      field<int32_t>(input, VrGame::InputMouseY)     = 0;
      field<int32_t>(input, VrGame::InputMouseWheel) = g_state.wheel;

      // The button before is what we gave at the last poll. With a mouse
      // device Poll has just copied it there; without one (no mouse attached
      // when the game started) nothing does, and the interface, which clicks
      // on a release, would never see the button come up
      uint8_t button = g_state.button ? ButtonDown : 0;
      field<uint8_t>(input, VrGame::InputMouseButtonsBefore + left) = g_lastButton;
      field<uint8_t>(input, VrGame::InputMouseButtons + left)       = button;
      g_lastButton = button;
      g_wrote      = true;
    }

    void __fastcall updateCursorHook(void* interfaceManager, void* unused) {
      void* tile = field<void*>(interfaceManager, VrGame::InterfaceCursorTile);
      void* node = tile ? field<void*>(tile, VrGame::TileNode) : nullptr;

      if (g_state.cursorActive && node) {
        float width  = reinterpret_cast<InterfaceSizeFn>(VrGame::InterfaceWidth)();
        float height = reinterpret_cast<InterfaceSizeFn>(VrGame::InterfaceHeight)();

        // X right and Z up, centred; the game clamps them to the interface
        float* translate = &field<float>(node, VrGame::NodeLocalTranslate);
        translate[0] = (g_state.cursor.x - 0.5f) * width;
        translate[2] = (0.5f - g_state.cursor.y) * height;
      }

      reinterpret_cast<UpdateCursorFn>(VrGame::UpdateCursor)(interfaceManager, unused);
    }

    // Only on a call a plugin holds: its reading of the wheel does not look
    // at the input object, where the pointer's wheel is
    int32_t __fastcall mouseWheelHook(void* input, void* unused, int32_t axis) {
      uintptr_t read = g_state.cursorActive ? VrGame::MouseAxis : g_pluginMouseWheel;
      return reinterpret_cast<MouseAxisFn>(read)(input, unused, axis);
    }

    bool isMenuActive(uint32_t menuId) {
      return reinterpret_cast<IsMenuActiveFn>(VrGame::IsMenuActive)(menuId, 0);
    }

  }


  bool VrMenuCursor::install() {
    bool sizes = VrGameMemory::matches(VrGame::InterfaceWidth,
        VrGame::InterfaceSizePrologue, sizeof(VrGame::InterfaceSizePrologue))
      && VrGameMemory::matches(VrGame::InterfaceHeight,
        VrGame::InterfaceSizePrologue, sizeof(VrGame::InterfaceSizePrologue));

    g_installed = sizes
      && VrGameMemory::canChainCall(VrGame::PollCallSite, VrGame::Poll)
      && VrGameMemory::callsTo(VrGame::UpdateCursorCallSite, VrGame::UpdateCursor)
      && VrGameMemory::chainCall(VrGame::PollCallSite, VrGame::Poll,
           reinterpret_cast<const void*>(&pollHook), &g_poll)
      && VrGameMemory::redirectCall(VrGame::UpdateCursorCallSite, VrGame::UpdateCursor,
           reinterpret_cast<const void*>(&updateCursorHook));

    uintptr_t wheelTarget = 0;

    if (g_installed && VrGameMemory::callTarget(VrGame::MouseWheelCallSite, &wheelTarget)
     && wheelTarget != VrGame::MouseAxis) {
      VrGameMemory::chainCall(VrGame::MouseWheelCallSite, VrGame::MouseAxis,
        reinterpret_cast<const void*>(&mouseWheelHook), &g_pluginMouseWheel);
    }

    if (g_installed)
      Logger::info("VR: Menu cursor hooks installed, the controllers can point at the menu panel");
    else
      Logger::warn("VR: The game's cursor calls were not found, the controllers cannot point at menus");

    return g_installed;
  }


  bool VrMenuCursor::pointerAllowed() {
    return g_installed
      && !isMenuActive(VrGame::LockPickMenuId)
      && !isMenuActive(VrGame::VatsMenuId);
  }


  bool VrMenuCursor::overTarget() {
    if (!g_installed)
      return true;

    void* interfaceManager = *reinterpret_cast<void* const*>(VrGame::InterfaceManager);
    return interfaceManager && field<void*>(interfaceManager, VrGame::InterfaceOverTile);
  }


  bool VrMenuCursor::companionWheelShown() {
    return g_installed && isMenuActive(VrGame::CompanionWheelMenuId);
  }


  void VrMenuCursor::update(const VrPointerState& state) {
    g_state = state;
  }

}
