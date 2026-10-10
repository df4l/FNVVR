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

    bool g_installed = false;

    VrPointerState g_state;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
    }

    void __fastcall pollHook(void* input, void* unused) {
      reinterpret_cast<PollFn>(VrGame::Poll)(input, unused);

      if (!g_state.cursorActive)
        return;

      // The laser places the cursor, the real mouse would move it away
      field<int32_t>(input, VrGame::InputMouseX)     = 0;
      field<int32_t>(input, VrGame::InputMouseY)     = 0;
      field<int32_t>(input, VrGame::InputMouseWheel) = g_state.wheel;

      uint32_t left = field<int32_t>(input, VrGame::InputMouseSwapped) ? 1 : 0;
      field<uint8_t>(input, VrGame::InputMouseButtons + left) = g_state.button ? ButtonDown : 0;
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
      && VrGameMemory::callsTo(VrGame::PollCallSite, VrGame::Poll)
      && VrGameMemory::callsTo(VrGame::UpdateCursorCallSite, VrGame::UpdateCursor)
      && VrGameMemory::redirectCall(VrGame::PollCallSite, VrGame::Poll,
           reinterpret_cast<const void*>(&pollHook))
      && VrGameMemory::redirectCall(VrGame::UpdateCursorCallSite, VrGame::UpdateCursor,
           reinterpret_cast<const void*>(&updateCursorHook));

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


  void VrMenuCursor::update(const VrPointerState& state) {
    g_state = state;
  }

}
