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
     * The game's input object (OSInputGlobals*) and its gamepad binding
     * table: one gamepad input code per ControlCode, 0xFF when unbound.
     * See findings/input.md.
     */
    constexpr uintptr_t InputGlobals             = 0x011F35CC;
    constexpr uintptr_t InputControllerBindings  = 0x1BE8;

    /** The player (PlayerCharacter**) */
    constexpr uintptr_t Player = 0x011DEA3C;

    /**
     * Call to the player's look handler in PlayerCharacter::Update, and the
     * handler: __thiscall with four stack arguments, returns a bool. It
     * turns the player from the mouse or the right stick, or follows the
     * VATS camera. See findings/input.md, motion controls.
     */
    constexpr uintptr_t LookCallSite = 0x0093F8D9;
    constexpr uintptr_t HandleLook   = 0x009445B0;

    /** MobileObject::SetLooking, __thiscall with a float: sets the pitch, clamped to about 89 degrees */
    constexpr uintptr_t SetLooking = 0x00931D90;
    constexpr uint8_t   SetLookingPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x89, 0x4D, 0xF4 };

    /** Virtual function table slots of the player: heading setter (__thiscall, float) and sit state getter */
    constexpr uintptr_t SetHeadingSlot    = 0x2C4;
    constexpr uintptr_t GetSitStateSlot   = 0x214;

    /** Fields of the player: pitch and heading in radians, life state, disabled controls */
    constexpr uintptr_t RefPitch          = 0x24;
    constexpr uintptr_t RefHeading        = 0x2C;
    constexpr uintptr_t ActorLifeState    = 0x108;
    constexpr uintptr_t PlayerDisabledControls = 0x680;
    constexpr uint8_t   ControlLook       = 0x2;

    /**
     * Weapon in the hand, see findings/weapon.md.
     *
     * Main::PlaceCamera draws the first-person model around the origin: it
     * sets the root's translation to zero, calls NiAVObject::Update
     * (__thiscall, one stack argument) on it at this site and restores the
     * translation. The first-person camera (NiCamera*, Main+0xA0) is placed
     * relative to the root as well.
     */
    constexpr uintptr_t FirstPersonUpdateCallSite = 0x00874F55;
    constexpr uintptr_t ObjectUpdate              = 0x00A59C60;
    constexpr uintptr_t MainFirstPersonCamera     = 0xA0;
    constexpr uintptr_t FirstPersonZeroSite       = 0x00874F41;
    constexpr uint8_t   FirstPersonZero[]         = { 0x68, 0x6C, 0x42, 0x1F, 0x01 };
    constexpr uintptr_t FirstPersonCameraSite     = 0x00874F16;
    constexpr uint8_t   FirstPersonCamera[]       = { 0x81, 0xC1, 0xA0, 0x00, 0x00, 0x00 };

    /**
     * Call to Projectile::Create in TESObjectWEAP::Fire (0x00523150), and the
     * function: __cdecl with 16 arguments, see ProjectileCreateFn
     */
    constexpr uintptr_t ProjectileCreateCallSite = 0x005245BD;
    constexpr uintptr_t ProjectileCreate         = 0x009BCA60;

    /**
     * Player fields: first-person root (NiNode*) and whether the first-person
     * model is used (0) or the third-person one, as Fire reads it (0x00524D10).
     * Actor process (+0x68) and its virtual function telling whether the
     * weapon is out (HighProcess 0x00915D40, reads +0x135).
     */
    constexpr uintptr_t PlayerFirstPersonRoot = 0x694;
    constexpr uintptr_t PlayerThirdPerson     = 0x64B;
    constexpr uintptr_t ActorProcess          = 0x68;
    constexpr uintptr_t ProcessIsWeaponOutSlot = 0x454;

    /**
     * Equipped weapon: the process's virtual function at this slot returns
     * its inventory entry (HighProcess 0x008D81E0 reads +0x114), whose form
     * is at +0x08 (0x0044DDC0). The weapon's animation type is a byte at
     * +0xF4 (0x00446390); Fire treats 10 to 13 as thrown and placed weapons.
     */
    constexpr uintptr_t ProcessWeaponInfoSlot = 0x148;
    constexpr uintptr_t InventoryEntryForm    = 0x08;
    constexpr uintptr_t WeaponAnimationType   = 0xF4;

    /** Nodes of the first-person model: the weapon, under the right hand, and the muzzle in the weapon's model */
    constexpr char WeaponNodeName[]           = "Weapon";
    constexpr char ProjectileNodeName[]       = "ProjectileNode";
    constexpr char ProjectileNodeAltName[]    = "##ProjectileNode";

    /** Hand bones of the first-person skeleton (strings 0x010C4C40 and 0x010C4C20) */
    constexpr char LeftHandNodeName[]         = "Bip01 L Hand";
    constexpr char RightHandNodeName[]        = "Bip01 R Hand";

    /**
     * Skinned geometry: GetAsNiGeometry vtable slot (0x00E68810 returns this
     * in the NiTriShape vtable 0x0109D454, null for nodes), NiGeometry skin
     * instance, and the skin instance's data and bone array. The bone count
     * is in the skin data. See findings/weapon.md §5.
     */
    constexpr uintptr_t ObjectGetAsGeometrySlot = 0x18;
    constexpr uintptr_t GeometrySkinInstance  = 0xBC;
    constexpr uintptr_t SkinInstanceData      = 0x08;
    constexpr uintptr_t SkinInstanceBones     = 0x14;
    constexpr uintptr_t SkinDataBoneCount     = 0x44;

    /**
     * Bind pose, read by 0x00E6FE30: the skin instance's root parent
     * (NiAVObject*), the skin data's transform from the root parent to the
     * skin, and its array of bone data, each starting with the transform
     * from the skin to the bone. Transforms are NiTransform: rotation
     * (row-major 3x3), translation, scale.
     */
    constexpr uintptr_t SkinInstanceRootParent = 0x10;
    constexpr uintptr_t SkinDataRootParentToSkin = 0x0C;
    constexpr uintptr_t SkinDataBones         = 0x40;
    constexpr uintptr_t SkinBoneDataSize      = 0x4C;
    constexpr uintptr_t TransformTranslation  = 0x24;
    constexpr uintptr_t TransformScale        = 0x30;

    /** NiAVObject transforms: parent, local rotation (row-major 3x3) and world transform */
    constexpr uintptr_t ObjectParent          = 0x18;
    constexpr uintptr_t ObjectLocalRotation   = 0x34;
    constexpr uintptr_t ObjectWorldRotation   = 0x68;
    constexpr uintptr_t ObjectWorldTranslation = 0x8C;
    constexpr uintptr_t ObjectWorldScale      = 0x98;

    /** State of the VATS camera, 0 when VATS is not running */
    constexpr uintptr_t VatsCameraState = 0x011F2258;

    /**
     * Call to PlayerCharacter::FocusOnActor in the dialogue menu's update,
     * and the function: __thiscall (actor, float blend, bool skipTurn). It
     * zooms on the speaker and, unless skipTurn is set, turns the player
     * towards the speaker's head. This is the only call that turns, see
     * findings/camera.md, dialogue.
     */
    constexpr uintptr_t FocusOnActorTurnCallSite = 0x00762F85;
    constexpr uintptr_t FocusOnActor             = 0x00953060;

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
     * InterfaceManager::Update shows a message box when its gamepad flag
     * changes: sLostController when the pad goes away, sControllerOption
     * when one appears. Both are calls to ShowMessageBox (__cdecl, the
     * caller pops the arguments, the result is unused).
     */
    constexpr uintptr_t ShowMessageBox                 = 0x00703E80;
    constexpr uintptr_t PadLostMessageCallSite         = 0x0070C5AA;
    constexpr uintptr_t PadConnectedMessageCallSite    = 0x0070C5E2;

    /**
     * isInStartMenu, __cdecl without arguments: true while the main menu is
     * shown (StartMenu exists without its in-game flag, so not the pause
     * menu). Checked against its first bytes before it is called.
     */
    constexpr uintptr_t IsInStartMenu = 0x0070EDF0;
    constexpr uint8_t   IsInStartMenuPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0xC0, 0xAA, 0x1D, 0x01, 0x00 };

    /**
     * The interface scene graph is culled into the shader accumulator after
     * the tile updates, which rewrite the nodes' app-culled flags from the
     * tiles' visible values. In game, the world render prepares it early:
     * it updates the tiles and calls MTRenderManager::AddAccumTask
     * (__thiscall, nine stack arguments), which culls on a worker thread
     * that RenderInterface waits for. Otherwise RenderInterface updates the
     * tiles and culls itself (__cdecl, three arguments: camera, scene graph,
     * culling data). RenderScene then draws the accumulated geometry.
     */
    constexpr uintptr_t InterfaceAccumTaskCallSite = 0x00713F97;
    constexpr uintptr_t AddAccumTask               = 0x00BA3390;
    constexpr uintptr_t InterfaceCullCallSite = 0x007136B7;
    constexpr uintptr_t InterfaceCull         = 0x00B6BEE0;

    /**
     * Returns true while the Pip-Boy is opening or open (InterfaceManager+0x4BC
     * is 2 or 3), __cdecl without arguments, see findings/pipboy.md. Checked
     * against its first bytes before it is called.
     */
    constexpr uintptr_t IsPipBoyShown = 0x00705A00;
    constexpr uint8_t   IsPipBoyShownPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0xE8, 0x07, 0x18, 0xDB, 0xFF, 0x85, 0xC0, 0x74, 0x42 };

    /**
     * Main's copy of the bStaticMenuBackground:Display setting, a bool read
     * once at startup (stored at 0x0086E123). While it is set, opening most
     * menus renders the world once, blurred by an image space modifier, into
     * a background texture that every frame then shows instead of the world.
     * The menu background update reads it at the checked site.
     */
    constexpr uintptr_t StaticMenuBackground = 0x011DEA28;
    constexpr uintptr_t StaticMenuBackgroundReadSite = 0x0086F4D5;
    constexpr uint8_t   StaticMenuBackgroundRead[] = { 0x0F, 0xB6, 0x15, 0x28, 0xEA, 0x1D, 0x01 };

    /**
     * Menus with a 3D scene of their own (lockpicking, casino games, ...),
     * see findings/menu-scenes.md. The game draws them in Main::Swap's menu
     * branch, which renders the frozen menu background instead of the world
     * and is not taken while the static background is off. That branch's
     * dispatch function checks each menu with IsMenuActive (__cdecl, menu id
     * and 0, returns a bool) and calls the menu's scene render (__cdecl
     * without arguments). Each entry starts at the menu's check
     * (push 0 / push id / call IsMenuActive / ... / call render).
     */
    constexpr uintptr_t IsMenuActive = 0x00702680;

    struct MenuScene {
      uintptr_t checkSite;
      uint32_t  menuId;
      uintptr_t render;
    };

    constexpr uintptr_t MenuSceneActiveCallOffset = 0x07;
    constexpr uintptr_t MenuSceneRenderCallOffset = 0x16;

    /** Surgery, LockPick, SlotMachine, BlackJack, Roulette, Caravan, LoveTester, SPECIALBook */
    constexpr MenuScene MenuScenes[] = {
      { 0x0087299C, 0x41E, 0x00709AE0 },
      { 0x008729B7, 0x3F6, 0x00709AF0 },
      { 0x008729D2, 0x438, 0x00709B00 },
      { 0x008729ED, 0x439, 0x00709B10 },
      { 0x00872A08, 0x43A, 0x00709B20 },
      { 0x00872A23, 0x43B, 0x00709B30 },
      { 0x00872A3E, 0x432, 0x007948D0 },
      { 0x00872AAD, 0x424, 0x007C9CA0 },
    };

    /**
     * Lockpicking scene. LockPickMenu's setup (0x0078E1C0) attaches
     * LockInterface01.NIF and BobbyPin01.NIF to the NiNode* held here. The
     * lock model has a soft dark disc under its frame, a NiTriStrips named
     * "shadow", that darkens the frozen menu background around the lock.
     */
    constexpr uint32_t  LockPickMenuId    = 0x3F6;
    constexpr uintptr_t LockPickSceneRoot = 0x011DA24C;
    constexpr char      LockPickBackdrop[] = "shadow";

    /**
     * NiObjectNET name (const char*), NiNode children (NiTArray: entries,
     * then the used size, which may include null entries) and the
     * GetAsNiNode vtable slot (returns this for nodes, null for geometry).
     */
    constexpr uintptr_t ObjectName          = 0x08;
    constexpr uintptr_t ObjectGetAsNodeSlot = 0x0C;
    constexpr uintptr_t NodeChildren        = 0xA0;
    constexpr uintptr_t NodeChildCount      = 0xA6;

    /**
     * Display settings (Setting objects, the value is at +4), see
     * findings/resolution.md. The renderer setup copies iSize W and iSize H
     * into its own globals once, at the checked site, long after the first
     * Direct3DCreate9 (the launcher check). bFull Screen is read at run time
     * through GetIsFullscreen, which is checked too.
     */
    constexpr uintptr_t SettingValue          = 0x4;
    constexpr uintptr_t SettingSizeWidth      = 0x011C73DC;
    constexpr uintptr_t SettingSizeHeight     = 0x011C718C;
    constexpr uintptr_t SettingFullScreen     = 0x011C77B4;
    constexpr uintptr_t RendererSizeReadSite  = 0x004DA730;
    constexpr uint8_t   RendererSizeRead[] = {
      0xB9, 0xDC, 0x73, 0x1C, 0x01, 0xE8, 0xB6, 0x5C, 0xF7, 0xFF, 0xA3, 0x7C, 0x94, 0x18, 0x01,
      0xB9, 0x8C, 0x71, 0x1C, 0x01, 0xE8, 0xA7, 0x5C, 0xF7, 0xFF, 0xA3, 0x80, 0x94, 0x18, 0x01,
    };
    constexpr uintptr_t GetIsFullscreen = 0x00446E10;
    constexpr uint8_t   GetIsFullscreenPrologue[] = { 0x55, 0x8B, 0xEC, 0xB9, 0xB4, 0x77, 0x1C, 0x01 };

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
    constexpr uintptr_t HudSubtitlesStoreSite     = 0x0076DF31;
    constexpr uint8_t   HudMessagesStore[]      = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x34, 0x01, 0x00, 0x00 };
    constexpr uint8_t   HudQuestReminderStore[] = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x24, 0x01, 0x00, 0x00 };
    constexpr uint8_t   HudSubtitlesStore[]     = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x3C, 0x01, 0x00, 0x00 };

    /**
     * Tile of every menu, indexed by menu id minus FirstMenuId: an array of
     * Tile* and its uint16 count. GetMenuTile (0x00A09030) reads both, which
     * is checked at the given site (movzx ecx,[count] / cmp / jbe / mov edx,[tiles]).
     * The HUD is menu HudMenuId.
     */
    constexpr uintptr_t MenuTiles       = 0x011F350C;
    constexpr uintptr_t MenuTileCount   = 0x011F3512;
    constexpr uint32_t  FirstMenuId     = 0x3E9;
    constexpr uint32_t  HudMenuId       = 0x3EC;
    constexpr uintptr_t MenuTileReadSite = 0x00A0904E;
    constexpr uint8_t   MenuTileRead[]  = { 0x0F, 0xB7, 0x0D, 0x12, 0x35, 0x1F, 0x01, 0x3B, 0xC8,
                                            0x76, 0x15, 0x8B, 0x15, 0x0C, 0x35, 0x1F, 0x01 };

    /**
     * InterfaceManager*, read by InterfaceManager::GetSingleton, which is
     * checked against its bytes (push ebp / mov ebp,esp / mov eax,[pointer] / pop ebp / ret)
     */
    constexpr uintptr_t InterfaceManager = 0x011D8A80;
    constexpr uintptr_t InterfaceManagerGetSingleton = 0x004B7210;
    constexpr uint8_t   InterfaceManagerGetSingletonBytes[] = { 0x55, 0x8B, 0xEC, 0xA1, 0x80, 0x8A, 0x1D, 0x01, 0x5D, 0xC3 };

    /** HUDMainMenu fields holding the HUD groups shown in front of the head */
    constexpr uintptr_t HudQuestReminder = 0x124;
    constexpr uintptr_t HudMessages      = 0x134;
    constexpr uintptr_t HudSubtitles     = 0x13C;

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

    /**
     * Menu cursor, see findings/input.md (laser pointer).
     *
     * OSInputGlobals::Poll (__thiscall, no arguments) is called once per
     * frame from Main::OnIdle_PollControls. It reads the mouse into the
     * input object: the movement (lX, lY, lZ as int32) and the buttons
     * (one byte each, 0x80 when down). Logical button 0 is the left one
     * unless the buttons are swapped.
     */
    constexpr uintptr_t PollCallSite         = 0x0086F39E;
    constexpr uintptr_t Poll                 = 0x00A23010;
    constexpr uintptr_t InputMouseX          = 0x1B24;
    constexpr uintptr_t InputMouseY          = 0x1B28;
    constexpr uintptr_t InputMouseWheel      = 0x1B2C;
    constexpr uintptr_t InputMouseButtons    = 0x1B30;
    constexpr uintptr_t InputMouseSwapped    = 0x1B4C;

    /**
     * InterfaceManager::UpdateCursor (__thiscall, no arguments), called by
     * InterfaceManager::Update while the interface is in mouse mode. It
     * moves the cursor tile's node by the mouse movement, clamps it to the
     * interface and derives the cursor's screen position from it. The node
     * position is in interface units, centred: X right, Z up.
     */
    constexpr uintptr_t UpdateCursorCallSite = 0x0070CD31;
    constexpr uintptr_t UpdateCursor         = 0x007118D0;
    constexpr uintptr_t InterfaceCursorTile  = 0x28;
    constexpr uintptr_t NodeLocalTranslate   = 0x58;

    /**
     * Size of the interface in its own units (__cdecl, returns a float):
     * 960 high and as wide as the screen's aspect makes it, or 1280 wide
     * and taller on a portrait screen.
     */
    constexpr uintptr_t InterfaceWidth  = 0x00715D40;
    constexpr uintptr_t InterfaceHeight = 0x00715DA0;
    constexpr uint8_t   InterfaceSizePrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xE8 };

    /** Menus driven by the sticks, where the pointer is off */
    constexpr uint32_t  VatsMenuId = 0x420;

  }

}
