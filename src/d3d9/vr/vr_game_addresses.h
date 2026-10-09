#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief Locations in FalloutNV.exe 1.4.0.525
   *
   * Every game address used by the VR layer is defined here. The
   * sources and how each one was verified are in findings/vr-render-hooks.md.
   */
  namespace VrGame {

    /** Call to Main::Swap in the main loop's frame function, after the game logic */
    constexpr uintptr_t SwapCallSite = 0x0086EDE8;

    /** Main::Swap, __thiscall without stack arguments: draws the world and the HUD, then presents */
    constexpr uintptr_t Swap = 0x0086FF70;

    /** Call to RenderInterface in InterfaceManager::Click, right before the HUD is drawn */
    constexpr uintptr_t RenderInterfaceCallSite = 0x007144D3;

    /** InterfaceManager::RenderInterface, __thiscall with two stack arguments */
    constexpr uintptr_t RenderInterface = 0x007134D0;

    /**
     * Function that places the world camera from the player each frame,
     * __thiscall on Main without arguments. It is called from three render
     * paths, after the world has been drawn, and the camera is final when
     * it returns.
     */
    constexpr uintptr_t PlaceCamera = 0x00874C10;

    /** The three calls to PlaceCamera: world render, pause-menu render, wireframe render */
    constexpr uintptr_t PlaceCameraCallSites[] = { 0x00870F33, 0x00870927, 0x00870AF8 };

    /**
     * NiCamera::UpdateWorldData, __thiscall with one argument. The game
     * recomputes the camera's world transform from its parent and local
     * transforms through it, which discards a pose written earlier. The
     * table entry is in the NiCamera virtual function table.
     */
    constexpr uintptr_t CameraUpdateWorldData     = 0x00A711B0;
    constexpr uintptr_t CameraUpdateWorldDataSlot = 0x0109CC54;

    /** Returns the world NiCamera*, __cdecl without arguments */
    constexpr uintptr_t GetWorldCamera = 0x00524C90;

    /** Refreshes the cached matrices of a NiCamera, __thiscall without arguments */
    constexpr uintptr_t CameraUpdateWorldToCamera = 0x00A70BA0;

    /**
     * Call to XInputGetState in OSInputGlobals::Poll, and the game's import
     * thunk (a jump through the XInput import) that it calls
     */
    constexpr uintptr_t XInputPollCallSite  = 0x00A2304C;
    constexpr uintptr_t XInputGetStateThunk = 0x009F996E;

    /**
     * The other calls to the same thunk. The interface keeps its own gamepad
     * flag, which hides the menu cursor: InterfaceManager::Update and the
     * InterfaceManager setup set it, and the start menu builds its entries
     * from a direct check.
     */
    constexpr uintptr_t XInputInterfaceUpdateCallSite = 0x0070C525;
    constexpr uintptr_t XInputInterfaceSetupCallSite  = 0x00709FFB;
    constexpr uintptr_t XInputStartMenuCallSite       = 0x007D4399;

    /**
     * isInStartMenu, __cdecl without arguments: true while the main menu is
     * shown (StartMenu exists without its in-game flag, so not the pause
     * menu). Checked against its first bytes before it is called.
     */
    constexpr uintptr_t IsInStartMenu = 0x0070EDF0;
    constexpr uint8_t   IsInStartMenuPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0xC0, 0xAA, 0x1D, 0x01, 0x00 };

    /** LoadingMenu*, not null while a loading screen is shown */
    constexpr uintptr_t LoadingMenu = 0x011DA0C0;

    /** Main::bInMenuMode, a bool set while any menu (Pip-Boy included) pauses the game */
    constexpr uintptr_t MenuMode = 0x011DEA2B;

    /** NiCamera layout */
    constexpr uintptr_t CameraWorldRotation    = 0x68;
    constexpr uintptr_t CameraWorldTranslation = 0x8C;
    constexpr uintptr_t CameraFrustum          = 0xDC;

  }

}
