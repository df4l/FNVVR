#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief Locations in FalloutNV.exe 1.4.0.525
   *
   * Every game address used by the VR layer is defined here. The
   * sources and how each one was verified are in findings/vr-render-hooks.md,
   * and in findings/hud.md for the HUD.
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

    /**
     * HUDMainMenu*, the HUD. Its top-level element groups are Tile* fields,
     * see findings/hud.md. HUDMainMenu::Create reads the pointer before it
     * stores each group, which is checked at these sites
     * (mov ecx,[HudMainMenu] / mov [ecx+offset],eax).
     */
    constexpr uintptr_t HudMainMenu = 0x011D96C0;
    constexpr uintptr_t HudMessagesStoreSite      = 0x0076D4E8;
    constexpr uintptr_t HudQuestReminderStoreSite = 0x0076CF81;
    constexpr uint8_t   HudMessagesStore[]      = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x34, 0x01, 0x00, 0x00 };
    constexpr uint8_t   HudQuestReminderStore[] = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x24, 0x01, 0x00, 0x00 };

    /** HUDMainMenu fields holding the HUD groups shown in front of the head */
    constexpr uintptr_t HudQuestReminder = 0x124;
    constexpr uintptr_t HudMessages      = 0x134;

    /**
     * HUDMainMenu fields holding every top-level HUD group: ActionPoints,
     * HitPoints (with the compass), RadiationMeter, EnemyHealth, QuestReminder,
     * Region_Location, ReticleCenter, SneakMeter, Messages, Info, Subtitles,
     * Hokeys, XPMeter, BreathMeter, Explosive_positioning_rect,
     * crippled_limb_indicator, DDTIcon, DDTIconEnemy, AmmoTypeLabel,
     * HardcoreMode, DDTIcon (second), DDTIconEnemyAP, CNDArrows.
     */
    constexpr uintptr_t HudGroups[] = {
      0x114, 0x118, 0x11C, 0x120, 0x124, 0x128, 0x12C, 0x130, 0x134, 0x138, 0x13C,
      0x140, 0x144, 0x148, 0x14C, 0x150, 0x154, 0x158, 0x15C, 0x160, 0x170, 0x174,
      0x180,
    };

    /**
     * Tile::GetNode, which returns the NiNode* at Tile+0x2C, checked against
     * its first bytes (... add ecx,0x2C)
     */
    constexpr uintptr_t TileGetNode = 0x0056C7F0;
    constexpr uint8_t   TileGetNodePrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0xC1, 0x2C };
    constexpr uintptr_t TileNode = 0x2C;

    /**
     * NiAVObject flags. The game hides a tile's node from one interface pass
     * by setting the app-culled bit (RenderInterface does it for three menus).
     * The flag setter is checked against its first bytes (... mov edx,[ecx+0x30]).
     */
    constexpr uintptr_t ObjectSetFlag = 0x0043B370;
    constexpr uint8_t   ObjectSetFlagPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x0F, 0xB6, 0x45, 0x08,
                                                    0x85, 0xC0, 0x74, 0x11, 0x8B, 0x4D, 0xFC, 0x8B, 0x51, 0x30 };
    constexpr uintptr_t ObjectFlags         = 0x30;
    constexpr uint32_t  ObjectFlagAppCulled = 0x1;

    /** NiCamera layout */
    constexpr uintptr_t CameraWorldRotation    = 0x68;
    constexpr uintptr_t CameraWorldTranslation = 0x8C;
    constexpr uintptr_t CameraFrustum          = 0xDC;

  }

}
