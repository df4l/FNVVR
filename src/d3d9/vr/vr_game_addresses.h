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

    /** NiCamera layout */
    constexpr uintptr_t CameraWorldRotation    = 0x68;
    constexpr uintptr_t CameraWorldTranslation = 0x8C;
    constexpr uintptr_t CameraFrustum          = 0xDC;

  }

}
