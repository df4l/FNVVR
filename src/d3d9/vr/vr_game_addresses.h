#pragma once

#include <cstddef>
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

    /**
     * The game's frame timer, see findings/main-loop.md §2. The main loop
     * updates appTimer (BSTimer) through the call below to BSTimer::Update
     * (__fastcall, timer in ECX, no stack arguments). It reads the tick
     * count through the GetTickCount import and takes the step in whole
     * milliseconds, at most 166, and sleeps up to 10 ms when the step is
     * shorter. BSTimer::Update(ms) (__thiscall, one stack argument) is the
     * part that takes the tick count as an argument; with a fixed step it
     * takes that step instead, without sleeping. BSTimer: +0x00 disabled
     * (byte), +0x04 fixed step in ms (float, 0 when off), +0x10 step before
     * the time multiplier (float, seconds), +0x14 last tick and +0x18 first
     * tick (ms, the last one relative to the first), +0x1D the tick count had
     * not advanced (byte).
     */
    constexpr uintptr_t TimerUpdateCallSite = 0x0086F296;
    constexpr uintptr_t TimerUpdate         = 0x00771520;
    constexpr uintptr_t TimerUpdateMs       = 0x00AA4EE0;
    constexpr uintptr_t AppTimer            = 0x011F6394;
    constexpr uintptr_t TickCountImport     = 0x00FDF060;
    constexpr uintptr_t TimerDisabled       = 0x00;
    constexpr uintptr_t TimerFixedStep      = 0x04;
    constexpr uintptr_t TimerStep           = 0x10;
    constexpr uintptr_t TimerLastTime       = 0x14;
    constexpr uintptr_t TimerFirstTime      = 0x18;
    constexpr uintptr_t TimerNoTick         = 0x1D;
    constexpr size_t    TimerSize           = 0x20;

    /**
     * States in which the frame timer keeps the game's own step (the New
     * Vegas Tick Fix does the same): BGSSaveLoadGame* with its flags at
     * +0x244, bit 2 set while a save loads (BGSSaveLoadGame::IsLoading
     * 0x0042CE10); a byte set while a new game loads; a byte set during the
     * pause menu's fade. See findings/main-loop.md §2.
     */
    constexpr uintptr_t SaveLoadGame        = 0x011DDF38;
    constexpr uintptr_t SaveLoadFlags       = 0x244;
    constexpr uint32_t  SaveLoadLoading     = 0x2;
    constexpr uintptr_t LoadingNewGame      = 0x011D8907;
    constexpr uintptr_t PauseFade           = 0x011DEA2D;

    /**
     * Value of the fMaxTime:HAVOK setting (float, seconds, 1/60 by default):
     * Havok's simulation step, scaled by the time multiplier, read every
     * frame by bhkWorld::SetDeltaTime 0x00C66760 and bhkWorld::Update.
     */
    constexpr uintptr_t HavokMaxStep        = 0x01267B38;

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
     * BoundInFrustum (0x004B5FF0, used by NodeInFrustum and PointInFrustum)
     * keeps the frustum planes of the last camera it was given, and only
     * sets them again for another camera pointer or another value of the
     * game's time, see findings/render.md "Frustum plane cache". The
     * checked site stores that camera.
     */
    constexpr uintptr_t FrustumCacheCamera     = 0x011C632C;
    constexpr uintptr_t FrustumCacheStoreSite  = 0x004B6090;
    constexpr uint8_t   FrustumCacheStore[]    = { 0x89, 0x0D, 0x2C, 0x63, 0x1C, 0x01 };

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

    /** Fields of the player: pitch and heading in radians, position, life state, disabled controls */
    constexpr uintptr_t RefPitch          = 0x24;
    constexpr uintptr_t RefHeading        = 0x2C;
    constexpr uintptr_t RefLocation       = 0x30;
    constexpr uintptr_t ActorLifeState    = 0x108;
    constexpr uintptr_t PlayerDisabledControls = 0x680;
    constexpr uint8_t   ControlLook       = 0x2;
    constexpr uint8_t   ControlSneak      = 0x40;

    /**
     * Actor::IsSneaking, __thiscall without arguments, returns a bool: the
     * sneak bit (0x400) of the actor's move mode, unless it swims. Checked
     * against its first bytes (... call Actor::GetMoveMode). The Sneak
     * control toggles that bit in PlayerCharacter::Update when it is
     * pressed (0x00940D54). See findings/sneak.md.
     */
    constexpr uintptr_t ActorIsSneaking = 0x004997B0;
    constexpr uint8_t   ActorIsSneakingPrologue[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8,
    };

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
     * RenderPipBoy (0x008761E0) sets the first-person root's translation to
     * the draw origin and updates it again at this site (NiAVObject::Update),
     * before it draws the model for depth and the Pip-Boy screen, see
     * findings/pipboy.md §3.4
     */
    constexpr uintptr_t PipBoyModelUpdateCallSite = 0x008762C6;

    /**
     * Call to Projectile::Create in TESObjectWEAP::Fire (0x00523150), and the
     * function: __cdecl with 16 arguments, see ProjectileCreateFn
     */
    constexpr uintptr_t ProjectileCreateCallSite = 0x005245BD;
    constexpr uintptr_t ProjectileCreate         = 0x009BCA60;

    /**
     * Call to PlayerCharacter::ApplyAutoAimActor in Projectile::AimProjectile
     * (0x009BD860), which Projectile::Create reaches for the player's
     * projectiles, and the function: __thiscall with 7 stack arguments, see
     * ApplyAutoAimFn. It turns the projectile towards the point under the
     * camera's centre, findings/weapon.md §9.
     */
    constexpr uintptr_t AutoAimCallSite = 0x009BD9E2;
    constexpr uintptr_t ApplyAutoAim    = 0x00965620;

    /**
     * Thrown weapons, see findings/throw.md (GOG database).
     * TESObjectWEAP::Fire(actor) is __thiscall with one stack argument; for
     * a weapon of type 10 to 13 it launches from the hand's weapon node,
     * spends one of the weapons and culls the node's model (child 0), or
     * unequips the weapon when it was the last. Projectile::AddInitialVelocity
     * calls Projectile::GetSpeed (__thiscall, returns a float) at the site
     * below to push a Havok projectile (grenades) along its Y axis;
     * missiles read GetSpeed again as they move. GetSpeed is
     * (fArrowWeakSpeed * (1 - power) + speed * power) * the speed multiplier
     * at +0xD0. Projectile::Launch gives the player's thrown and placed weapons
     * (TESObjectWEAP::IsThrownWeapon: ranged but not a gun) the power
     * min(1, fArrowBowTimerBase + mult * the player's release timer at
     * +0x730), which the game sets to 10 for V.A.T.S. throws. The weapon's
     * attack animation (TESObjectWEAP::GetAttackAnim 0x0051F5F0) is a byte
     * at +0x11D.
     */
    constexpr uintptr_t WeaponFire                 = 0x00523150;
    constexpr uint8_t   WeaponFirePrologue[]       = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x3C, 0x4E, 0xEF, 0x00,
                                                       0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr uintptr_t ProjectileSpeedCallSite    = 0x009BEBD9;
    constexpr uintptr_t ProjectileGetSpeed         = 0x009669C0;
    constexpr uintptr_t ProjectileSpeedMult        = 0xD0;
    constexpr uintptr_t PlayerProjectileReleaseTimer = 0x730;
    constexpr float     ThrowReleaseTimerFull      = 10.0f;
    constexpr uintptr_t WeaponAttackAnimation      = 0x11D;

    /**
     * Where a throw by hand is fired, see findings/throw.md §4 (GOG
     * database). PlayerCharacter::Update (0x0093E860) calls
     * Actor::HandlePostAnimationActions (__thiscall, no argument), which
     * fires the player's weapon when its animation releases it, at the
     * site below: on the main thread, before the AI threads that step
     * Havok run alongside the rendering.
     */
    constexpr uintptr_t PlayerPostAnimationCallSite = 0x0094380E;
    constexpr uintptr_t HandlePostAnimationActions  = 0x008BA600;

    /**
     * TESForm::GetFormByNumericID (cdecl, the form id, returns the form or
     * null), checked by its prologue. A form's id is at +0x0C. A
     * projectile's impacts are a BSSimpleList at +0x88 (item, next) of
     * Projectile::ImpactData whose first field is the reference hit, with
     * bHasImpacted at +0x90; the player's reference id is 0x14.
     */
    constexpr uintptr_t FormById              = 0x004839C0;
    constexpr uint8_t   FormByIdPrologue[]    = { 0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
                                                  0x83, 0x3D, 0xC0, 0x54, 0x1C };
    constexpr uintptr_t FormId                = 0x0C;
    constexpr uintptr_t ProjectileImpacts     = 0x88;
    constexpr uintptr_t ProjectileHasImpacted = 0x90;
    constexpr uint32_t  PlayerRefId           = 0x14;

    /**
     * FlameProjectile vtable entries (table 0x0108F2F4), both __thiscall:
     * Process3D (no argument) stores the flame's rotation relative to the
     * shooter's projectile node in Projectile::kFollowOffset (+0x94), and
     * UpdateProjectile (frame time) puts the flame at that node's world
     * translate and turned with it every frame before its target pick
     * (vtable +0x328, RunTargetPick). The node comes from the shooter's
     * GetProjectileNode (TESObjectREFR vtable slot, __thiscall without
     * arguments; Actor::GetProjectileNode 0x008BE0A0 returns the
     * first-person model's for the player). The shooter is at +0xFC
     * (Projectile::GetShooter 0x00874480). See findings/weapon.md §10.
     */
    constexpr uintptr_t FlameProcess3DSlot   = 0x0108F600;
    constexpr uintptr_t FlameProcess3D       = 0x009B3320;
    constexpr uintptr_t FlameUpdateSlot      = 0x0108F604;
    constexpr uintptr_t FlameUpdate          = 0x009B34C0;

    /**
     * BeamProjectile vtable entries (table 0x0108C3C4, same slots as the
     * flame's), both __thiscall: Process3D (no argument) stores the beam's
     * rotation relative to the shooter's projectile node in kFollowOffset,
     * and UpdateProjectile (frame time) puts the beam at that node's world
     * translate, and for the player turned with it, every frame until it
     * fades, at most 5 s. Lasers are beams. See findings/weapon.md §10.1.
     */
    constexpr uintptr_t BeamProcess3DSlot    = 0x0108C6D0;
    constexpr uintptr_t BeamProcess3D        = 0x00979140;
    constexpr uintptr_t BeamUpdateSlot       = 0x0108C6D4;
    constexpr uintptr_t BeamUpdate           = 0x00979330;
    constexpr uintptr_t ProjectileShooter    = 0xFC;
    constexpr uintptr_t RefProjectileNodeSlot = 0x20C;

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
     * PlayerCharacter::GetLookingAtLocation, the point other actors look at
     * (Actor::ProcessTracking aims their look-at IK at it): __thiscall with
     * a NiPoint3* result on the stack (ret 4), returns the result. Its only
     * table entry, in the PlayerCharacter vtable, and the current view flag
     * it tests (b3rdPerson): in first person it gives the world position of
     * the first-person camera node (0x011E07D0), see findings/npc-gaze.md.
     */
    constexpr uintptr_t PlayerLookingAtSlot      = 0x0108ABD0;
    constexpr uintptr_t PlayerLookingAt          = 0x00952FF0;
    constexpr uintptr_t PlayerCurrentThirdPerson = 0x64A;

    /**
     * Equipped weapon: the process's virtual function at this slot returns
     * its inventory entry (HighProcess 0x008D81E0 reads +0x114), whose form
     * is at +0x08 (0x0044DDC0). The weapon's animation type is a byte at
     * +0xF4 (0x00446390); Fire treats 10 to 13 as thrown and placed weapons.
     */
    constexpr uintptr_t ProcessWeaponInfoSlot = 0x148;
    constexpr uintptr_t InventoryEntryForm    = 0x08;
    constexpr uintptr_t WeaponAnimationType   = 0xF4;
    constexpr int32_t   WeaponTypeHandToHand       = 0;

    /**
     * Animation sequences, see findings/weapon.md. Every sequence an actor
     * plays starts in AnimData::MorphOrBlendToSequence (__thiscall on
     * AnimData with three stack arguments: sequence, 16-bit group ID,
     * sequence type; returns the sequence), called from these four sites.
     * It blends from the previous pose unless the byte at AnimData+0x120 is
     * set, which the game clears after each animation update. The player's
     * first-person AnimData is at +0x690 (PlayerCharacter::GetAnimation,
     * 0x00950A60). The low byte of a group ID indexes the group table
     * (0x011977D8): AimIS, AimISUp and AimISDown are 0x14 to 0x16.
     */
    constexpr uintptr_t PlaySequence             = 0x004949A0;
    constexpr uintptr_t PlaySequenceCallSites[]  = { 0x00494989, 0x004956FF, 0x00495E2A, 0x004973B4 };
    constexpr uintptr_t AnimDataSkipBlend        = 0x120;
    constexpr uintptr_t PlayerFirstPersonAnimData = 0x690;
    constexpr uint8_t   AnimGroupAimIS           = 0x14;
    constexpr uint8_t   AnimGroupAimISDown       = 0x16;

    /** Nodes of the first-person model: the weapon, under the right hand, and the muzzle in the weapon's model */
    constexpr char WeaponNodeName[]           = "Weapon";
    constexpr char ProjectileNodeName[]       = "ProjectileNode";
    constexpr char ProjectileNodeAltName[]    = "##ProjectileNode";

    /** Hand bones of the first-person skeleton (strings 0x010C4C40 and 0x010C4C20) */
    constexpr char LeftHandNodeName[]         = "Bip01 L Hand";
    constexpr char RightHandNodeName[]        = "Bip01 R Hand";

    /**
     * The left forearm's twist bone, which carries the Pip-Boy, and the
     * Pip-Boy's screen geometry below it (FOPipboyManager::Initialize looks
     * for both names), see findings/weapon.md §6.3 and findings/pipboy.md.
     * A geometry's world bound is a pointer at NiAVObject+0x20 to its centre
     * and radius.
     */
    constexpr char LeftForeTwistNodeName[]    = "Bip01 L ForeTwist";
    constexpr char PipBoyScreenNodeName[]     = "pipboyscreen";
    constexpr char PipBoyScreenNodeAltName[]  = "pipboyscreen:0";
    constexpr uintptr_t ObjectWorldBound      = 0x20;

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
     * NiAVObject transforms: parent, local rotation (row-major 3x3),
     * translation and scale, and world transform. The local scale is the
     * float NiAVObject::SetLocalScale (0x00440490, `fstp [eax+0x64]`) writes.
     */
    constexpr uintptr_t ObjectParent          = 0x18;
    constexpr uintptr_t ObjectLocalRotation   = 0x34;
    constexpr uintptr_t ObjectLocalTranslation = 0x58;
    constexpr uintptr_t ObjectLocalScale      = 0x64;
    constexpr uintptr_t ObjectWorldRotation   = 0x68;
    constexpr uintptr_t ObjectWorldTranslation = 0x8C;
    constexpr uintptr_t ObjectWorldScale      = 0x98;

    /**
     * Size of the world transform above (rotation, translation and scale,
     * from ObjectWorldRotation to the end of ObjectWorldScale) and of the
     * world bound ObjectWorldBound points to (centre and radius).
     */
    constexpr size_t    ObjectWorldTransformSize = 0x34;
    constexpr size_t    BoundSize             = 0x10;

    /** State of the VATS camera, 0 when VATS is not running */
    constexpr uintptr_t VatsCameraState = 0x011F2258;

    /**
     * Visibility pre-pass of Main::OnIdle, before Main::Swap ("Multibound
     * Culling and Occlusion" in the Xbox build): calls to
     * ShadowSceneNode::UpdateOcclusionPlaneVisibility and
     * TES::UpdateMultiBoundVisibility, both __thiscall with the world camera
     * as their only stack argument. They cull the multibound groups of
     * objects, the terrain and the interior rooms against that camera for the
     * whole frame, see findings/render.md.
     */
    constexpr uintptr_t OcclusionPlaneVisibilityCallSite = 0x0086EBF5;
    constexpr uintptr_t OcclusionPlaneVisibility         = 0x00B5AC90;
    constexpr uintptr_t MultiBoundVisibilityCallSite     = 0x0086EC18;
    constexpr uintptr_t MultiBoundVisibility             = 0x0045B070;

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
     * TESObjectREFR::Get3D, virtual, __thiscall without arguments, returns
     * the reference's NiAVObject* or null when it is not loaded. FocusOnActor
     * calls it on the speaker (call [vtable+0x1D0] at 0x009531F5) and looks
     * for the head under it by these names, see findings/hud.md, dialogue
     * panel.
     */
    constexpr uintptr_t RefGet3DSlot = 0x1D0;
    constexpr char HeadNodeName[]        = "Bip01 Head";
    constexpr char SpeakerNodeName[]     = "Bip01 Speaker";

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
     * The Pip-Boy's state machine, InterfaceManager+0x4BC: 0 closed, 1 open
     * requested, 2 raising the arm, 3 open, 4 close requested, 5 lowering
     * the arm. InterfaceManager::OpenPipboy (__thiscall: callback, menu to
     * open, 0 keeps the last tab) checks that the player may open it and
     * moves 0 to 1. InterfaceManager::Update steps the machine. Names from
     * the Xbox 360 build in IDA, see findings/pipboy.md §1. OpenPipboy is
     * checked against its first bytes before it is called. The VR layer
     * does not call ClosePipboy (0x0070F690): see findings/pipboy.md §3.5.
     */
    constexpr uintptr_t InterfacePipBoyState = 0x4BC;
    constexpr uintptr_t OpenPipBoy           = 0x0070F4E0;
    /** The game's Pip-Boy control, on release, while the state is 0 (InterfaceManager::Update) */
    constexpr uintptr_t PipBoyControlOpenCallSite = 0x0070E913;
    constexpr uint8_t   OpenPipBoyPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x56, 0x89, 0x4D, 0xFC,
                                                 0x6A, 0x04, 0x8B, 0x0D, 0x3C, 0xEA, 0x1D, 0x01 };

    /**
     * The fPlayerPipBoyLightTimer game setting (Setting object, float value
     * at SettingValue, 0.8 s by default): PlayerCharacter::UpdateMenuModeButton
     * (0x009673D0) switches the Pip-Boy light once the Pip-Boy control has
     * been held longer than this, and its release then does not open the
     * Pip-Boy, see findings/pipboy.md §6. The checked bytes are where the
     * function tests the control's release and reads the setting.
     */
    constexpr uintptr_t PipBoyLightTimerSetting  = 0x011CD098;
    constexpr uintptr_t PipBoyLightTimerReadSite = 0x00967411;
    constexpr uint8_t   PipBoyLightTimerRead[] = {
      0x6A, 0x02, 0x6A, 0x0E, 0x8B, 0x4D, 0xF0, 0xE8, 0x43, 0xD2, 0x0B, 0x00, 0x85, 0xC0, 0x74, 0x2D,
      0xB9, 0x98, 0xD0, 0x1C, 0x01, 0xE8, 0xF5, 0xC9, 0xA9, 0xFF,
    };

    /**
     * The two waits on the arm animation in InterfaceManager::UpdatePipboy
     * (0x0070EE80), see findings/pipboy.md §3.6.
     *
     * Raising (state 2): Animation::GetGroupAction (__thiscall on the
     * first-person AnimData, section; returns the section's action from
     * AnimData+0x5C) is called with the left arm section. At AGA_STOP the
     * game snaps the arm to the group's stop key itself, from the current
     * sequence's offset (NiControllerSequence+0x48), and opens the menus.
     *
     * Lowering (state 5): PlayerCharacter::IsPipboyActive (__thiscall on the
     * player) stays true while the left arm section plays the PipBoy or
     * PipBoyChild group. Animation::ClearGroup (__thiscall: section, blend
     * time) stops that section's sequence.
     *
     * The AnimData holds each section's group ID (16 bits, 0xFF when none)
     * at +0x4C and its current sequence at +0xE0.
     */
    constexpr uintptr_t PipBoyRaiseWaitCallSite  = 0x0070F0A4;
    constexpr uintptr_t AnimGetGroupAction       = 0x0070F490;
    constexpr uintptr_t PipBoyLowerWaitCallSite  = 0x0070F3DA;
    constexpr uintptr_t IsPipBoyActive           = 0x00967AE0;
    constexpr uintptr_t AnimClearGroup           = 0x00496080;
    constexpr uint8_t   AnimClearGroupPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0x08 };
    constexpr uintptr_t AnimDataSectionGroups    = 0x4C;
    constexpr uintptr_t AnimDataSectionSequences = 0xE0;
    constexpr uintptr_t SequenceOffset           = 0x48;
    constexpr int32_t   AnimSectionLeftArm       = 2;
    constexpr uint32_t  AnimActionStop           = 1;
    constexpr uint8_t   AnimGroupPipBoy          = 0xE2;
    constexpr uint8_t   AnimGroupPipBoyChild     = 0xF0;

    /**
     * Drawing and holstering the weapon, see findings/weapon.md §7.
     *
     * Actor::PickAnimations (0x00895110) plays the Equip or Unequip group on
     * the weapon section when the actor wants the weapon drawn and it is not,
     * or the reverse, and sets the matching anim action (0 or 1). On a later
     * frame it calls Animation::GetGroupAction with the weapon section at
     * this site, and once the action is at least AGA_STOP the process marks
     * the weapon drawn or holstered and places it (ForceWeaponDrawnSheathed).
     * For the player this is the third-person AnimData; its actor is at +0x04.
     *
     * Actor::SetWantWeaponDrawn (__thiscall, bool) and GetWantWeaponDrawn
     * (__thiscall, returns bool). PlayerCharacter::Update holsters the weapon
     * at the second site when the ready/reload control is held long enough.
     * PlayerCharacter::SetControlsDisabled holsters it while the fighting
     * control (0x08) is disabled.
     */
    constexpr uintptr_t EquipWaitCallSite        = 0x0089606E;
    constexpr uintptr_t HolsterInputCallSite     = 0x00940C05;
    constexpr uintptr_t SetWantWeaponDrawn       = 0x008A6840;
    constexpr uint8_t   SetWantWeaponDrawnPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x56, 0x89, 0x4D, 0xF0 };
    constexpr uintptr_t GetWantWeaponDrawn       = 0x008A6970;
    constexpr uint8_t   GetWantWeaponDrawnPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC };
    constexpr uintptr_t AnimDataActor            = 0x04;
    constexpr int32_t   AnimSectionWeapon        = 4;
    constexpr uint8_t   AnimGroupEquip           = 0x18;
    constexpr uint8_t   AnimGroupUnequip         = 0x19;
    constexpr uint8_t   ControlFighting          = 0x8;

    /**
     * MiddleHighProcess::SitOrSleep (0x009213E0) runs while the process has
     * a furniture (+0x140). It first holsters the weapon (call 0x00921476)
     * and only starts sitting once it is holstered, so the sit state is
     * still 0 while the actor walks up to the furniture.
     */
    constexpr uintptr_t ProcessFurniture         = 0x140;

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
    /**
     * Rendered menus other than the Pip-Boy (character creation, see
     * findings/menu-scenes.md). The rendered-menu render (0x008707C0) draws
     * the current rendered menu's model, such as the Reflectron of the
     * character creation, with the menu's own camera through
     * DrawWorld_DrawRenderedMenuParent (__thiscall on Main, no arguments,
     * returns at once for the Pip-Boy or without a rendered menu). Main is
     * the object held at MainSingleton. GetCurrentRenderedMenu and GetPipBoy
     * are __cdecl without arguments and return a FORenderedMenu*.
     */
    constexpr uintptr_t MainSingleton              = 0x011DEA0C;
    constexpr uintptr_t RenderedMenuParentCallSite = 0x00870967;
    constexpr uintptr_t DrawRenderedMenuParent     = 0x00875BF0;
    constexpr uintptr_t GetCurrentRenderedMenu     = 0x00707AD0;
    constexpr uintptr_t GetPipBoy                  = 0x00705990;

    /**
     * The rendered-menu render then draws the menu's screen, such as the
     * Reflectron's with the menu and the face, through RenderPipBoy (see
     * PipBoyScreenCallSite), with the same camera. Its arguments are the
     * menu's screen geometry (FORenderedMenu::GetScreenGeometry, __thiscall
     * on the menu without arguments) and the renderer held at Renderer.
     */
    constexpr uintptr_t GetScreenGeometry          = 0x007FA950;
    constexpr uintptr_t Renderer                   = 0x011F4748;

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

    /**
     * The idle camera, see findings/camera.md §7. PlayerCharacter::Update
     * counts the time without input and turns the third-person camera
     * around the player past fVanityModeAutoDelay, unless the
     * bDisableAutoVanityMode:General setting (bool value at SettingValue)
     * is set. The checked site is where it reads the setting.
     */
    constexpr uintptr_t DisableAutoVanitySetting = 0x011E09E0;
    constexpr uintptr_t AutoVanityCheckSite      = 0x0094358E;
    constexpr uint8_t   AutoVanityCheck[] = { 0xB9, 0xE0, 0x09, 0x1E, 0x01, 0xE8, 0x98, 0x63, 0xB7, 0xFF };

    /**
     * The kill camera, see findings/camera.md §7. The player's mode
     * (KILL_CAM_TYPE: 0 none, 1 player, 2 cinematic) is at
     * PlayerKillCamMode, copied from iKillCamera:GamePlay by the player's
     * constructor at the checked site. PlayerCharacter::SetKillCamModeCallback
     * (__thiscall, mode) sets it and ends a kill camera of the old mode
     * that is running.
     */
    constexpr uintptr_t PlayerKillCamMode      = 0x7C0;
    constexpr int32_t   KillCamPlayer          = 1;
    constexpr uintptr_t KillCamModeStoreSite   = 0x00938F73;
    constexpr uint8_t   KillCamModeStore[] = {
      0xB9, 0x7C, 0x08, 0x1E, 0x01, 0xE8, 0x53, 0x45, 0xB0, 0xFF,
      0x8B, 0x8D, 0x54, 0xFF, 0xFF, 0xFF, 0x8B, 0x10, 0x89, 0x91, 0xC0, 0x07, 0x00, 0x00,
    };
    constexpr uintptr_t SetKillCamModeCallback = 0x007D12D0;
    constexpr uint8_t   SetKillCamModeCallbackPrologue[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x4D, 0xF8, 0xE8, 0x5F, 0x00, 0x00, 0x00,
    };

    /**
     * The view the player asks for, see findings/camera.md §3 and §8. The
     * player's bWant3rdPerson byte is set by the POV control and kept in
     * saves; PlayerCharacter::SetFirstPerson (__thiscall, bool first)
     * clears it, and the camera update then goes back to first person.
     */
    constexpr uintptr_t PlayerWantsThirdPerson = 0x64C;
    constexpr uintptr_t SetFirstPerson         = 0x00950110;
    constexpr uint8_t   SetFirstPersonPrologue[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0xF8,
      0x0F, 0xB6, 0x88, 0x4C, 0x06, 0x00, 0x00,
    };

    /**
     * The game's temporary third person, see findings/camera.md §8.1:
     * PlayerCharacter::ForceTemp3rdPerson sets the player's bTemp3rdPerson
     * byte to use furniture (TESFurniture::Activate, sitting down and
     * getting up) and for the Sandman and Cannibal kills;
     * PlayerCharacter::UpdateTemp3rdPerson (0x009503D0) clears it and goes
     * back to first person once the animation is over. Its read there,
     * movzx ecx, byte [eax+0x64D], checks the offset.
     */
    constexpr uintptr_t PlayerTempThirdPerson     = 0x64D;
    constexpr uintptr_t TempThirdPersonReadSite   = 0x009503DA;
    constexpr uint8_t   TempThirdPersonRead[]     = { 0x0F, 0xB6, 0x88, 0x4D, 0x06, 0x00, 0x00 };

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
    constexpr uintptr_t HudHitPointsStoreSite     = 0x0076C7AE;
    constexpr uintptr_t HudInfoStoreSite          = 0x0076D6BF;
    constexpr uintptr_t HudActionPointsStoreSite  = 0x0076C185;
    constexpr uint8_t   HudHitPointsStore[]     = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x18, 0x01, 0x00, 0x00 };
    constexpr uint8_t   HudInfoStore[]          = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x38, 0x01, 0x00, 0x00 };
    constexpr uint8_t   HudActionPointsStore[]  = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x14, 0x01, 0x00, 0x00 };

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
    constexpr uintptr_t HudXpMeter       = 0x144;

    /**
     * XPMeter group (XP gained and "LEVEL UP"). HUDMainMenu::Create stores
     * it at the given site and sets its x, y and base height (trait
     * TileTraitXpMeterBaseY) from the screen size; UpdateVisibilityState
     * sets its y from that base, which is checked at the given site
     * (push 0x1007 / mov edx,[HudMainMenu] / mov ecx,[edx+0x144]). See
     * findings/hud.md §11.
     */
    constexpr uintptr_t HudXpMeterStoreSite = 0x0076E2D9;
    constexpr uint8_t   HudXpMeterStore[]   = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x44, 0x01, 0x00, 0x00 };
    constexpr uintptr_t HudXpMeterBaseReadSite = 0x00771BA3;
    constexpr uint8_t   HudXpMeterBaseRead[]   = { 0x68, 0x07, 0x10, 0x00, 0x00, 0x8B, 0x15, 0xC0, 0x96, 0x1D, 0x01,
                                                   0x8B, 0x8A, 0x44, 0x01, 0x00, 0x00 };
    constexpr uint32_t  TileTraitXpMeterBaseY  = 0x1007;

    /**
     * RadiationMeter group (radiation bar and rads per second).
     * HUDMainMenu::Create stores it at the given site and sets its x and y
     * from the screen size; no other HUD code moves it. See
     * findings/hud.md §12.
     */
    constexpr uintptr_t HudRadiationMeter = 0x11C;
    constexpr uintptr_t HudRadiationMeterStoreSite = 0x0076CB86;
    constexpr uint8_t   HudRadiationMeterStore[]   = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x1C, 0x01, 0x00, 0x00 };

    /**
     * SneakMeter group ([HIDDEN], [CAUTION], [DANGER] or [DETECTED] while
     * the player sneaks). HUDMainMenu::Create stores it at the given site
     * and sets its x and y from the screen size, at the top of the screen,
     * in the middle; HUDMainMenu::SetMenuMode only sets whether it is
     * visible. See findings/sneak.md.
     */
    constexpr uintptr_t HudSneakMeter = 0x130;
    constexpr uintptr_t HudSneakMeterStoreSite = 0x0076D419;
    constexpr uint8_t   HudSneakMeterStore[]   = { 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0x89, 0x81, 0x30, 0x01, 0x00, 0x00 };

    /**
     * HUDMainMenu fields holding the HUD groups shown on the hands: HitPoints
     * (health bar and compass), ActionPoints (action points, ammunition and
     * weapon condition), with AmmoTypeLabel and CNDArrows, which are placed
     * inside the ActionPoints group's rectangle, and Info (interaction prompt)
     */
    constexpr uintptr_t HudHitPoints     = 0x118;
    constexpr uintptr_t HudActionPoints  = 0x114;
    constexpr uintptr_t HudAmmoTypeLabel = 0x15C;
    constexpr uintptr_t HudConditionArrows = 0x180;
    constexpr uintptr_t HudInfo          = 0x138;

    /**
     * Traits of a tile's rectangle, in interface units, read with
     * TileGetFloat: x and y relative to the parent, width and height.
     * HUDMainMenu::Create sets those of the HUD groups from the screen size
     * and safe zone, with the HUD menu's own tile at the origin, see
     * findings/hud.md §9.
     */
    constexpr uint32_t  TileTraitX      = 0xFA1;
    constexpr uint32_t  TileTraitY      = 0xFA2;
    constexpr uint32_t  TileTraitHeight = 0xFB0;
    constexpr uint32_t  TileTraitWidth  = 0xFB1;

    /**
     * Interface::GetActivateREFR, __cdecl without arguments: the reference
     * the player would activate (InterfaceManager+0xFC), whose name and
     * action the HUD's Info group shows, or null. Checked against its first
     * bytes (push ebp / mov ebp,esp / call GetSingleton / test eax,eax / jz).
     */
    constexpr uintptr_t GetActivateRef = 0x00703350;
    constexpr uint8_t   GetActivateRefPrologue[] = { 0x55, 0x8B, 0xEC, 0xE8, 0xB8, 0x3E, 0xDB, 0xFF, 0x85, 0xC0, 0x74, 0x21 };

    /**
     * ViewCaster::FindActivateREFR, __thiscall on the ViewCaster at
     * InterfaceManager+0x13C, stack arguments (const NiPoint3* origin,
     * const NiPoint3* direction, float distance, float* hitDistance,
     * bool* fuzzy), ret 0x14, returns the reference or null. It sweeps a
     * sphere along the ray and takes the nearest thing to activate, or a
     * body near the ray when the ray misses (fuzzy). Called once from
     * InterfaceManager::RefreshActivateREFR (0x0070BC20), which passes its
     * own origin and direction variables: the water pick and the pick
     * location after the call use whatever they hold. See findings/hud.md §10.
     */
    constexpr uintptr_t FindActivateRef         = 0x00631D60;
    constexpr uintptr_t FindActivateRefCallSite = 0x0070C130;

    /**
     * ViewCaster::pFuzzyBody, the rigid body of the last fuzzy pick, which
     * the Z grab holds by. FindActivateREFR clears it first; checked
     * against that store (mov dword ptr [0x011CC5EC], 0).
     */
    constexpr uintptr_t FuzzyPickBody        = 0x011CC5EC;
    constexpr uintptr_t FuzzyPickBodyClear   = 0x00631DBF;
    constexpr uint8_t   FuzzyPickBodyClearBytes[] = { 0xC7, 0x05, 0xEC, 0xC5, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x00 };

    /**
     * Button icon of the interaction prompt. The HUDMainMenu field holds the
     * Info group's justify_center_hotrect tile, whose "_xbox_button" trait
     * picks the gamepad button image (9 A, 11 X, ...). SetInfoForRef sets it
     * from the Activate control's binding, inlined at the given site, which
     * is checked (push 9 / push "_xbox_button" / call TextToTrait / ... /
     * mov ecx,[HudMainMenu] / mov ecx,[ecx+0x9C]). Tile::TextToTrait is
     * __cdecl (name) and returns TileTraitUnknown for an unknown name. See
     * findings/hud.md §9.2.
     */
    constexpr uintptr_t HudPromptButtonTile   = 0x9C;
    constexpr uintptr_t HudPromptIconSite     = 0x00777BFA;
    constexpr uint8_t   HudPromptIconCode[]   = { 0x6A, 0x09, 0x68, 0xD0, 0x34, 0x07, 0x01, 0xE8, 0x5A, 0x9C, 0x28, 0x00,
                                                  0x83, 0xC4, 0x04, 0x50, 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01,
                                                  0x8B, 0x89, 0x9C, 0x00, 0x00, 0x00 };
    constexpr uintptr_t TileTextToTrait       = 0x00A01860;
    constexpr uint8_t   TileTextToTraitPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xF6, 0xCD, 0xF1, 0x00 };
    constexpr uint32_t  TileTraitUnknown      = 0x80000000;
    constexpr char      PromptIconTraitName[] = "_xbox_button";

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

    /**
     * Pip-Boy render, see findings/pipboy.md §3.
     *
     * When the Pip-Boy opens, Main::Swap renders the world once, mono and
     * blurred, into a background texture (0x00871DC0, __thiscall on Main
     * without arguments, called at the first site) and sets the flag
     * 0x011DEA29. While that flag is set, Swap draws the rendered menu
     * through LaTerreur::Render (__thiscall on Main: target, rendered menu
     * flag, flag; called with zeros at the world branch's site). The
     * rendered-menu render (0x008707C0) initialises its render target
     * (__thiscall on Main, four stack arguments, returns the target), draws
     * the frozen background (DrawWorld_CopyMenuBG, __thiscall on Main: image
     * space manager, renderer, multisampling, destination), the first-person
     * model and finally the Pip-Boy screen (RenderPipBoy, __thiscall on
     * Main: screen geometry, renderer).
     */
    constexpr uintptr_t PipBoyBackgroundCallSite   = 0x0087000F;
    constexpr uintptr_t MenuBackgroundReady        = 0x011DEA29;

    /**
     * FORenderedMenuRaceSex::OnMenuOpen (0x007FD6C0) captures the same
     * frozen background, with Main in ECX, as soon as the character
     * creation opens. See findings/menu-scenes.md.
     */
    constexpr uintptr_t RaceSexBackgroundCallSite  = 0x007FD6F6;
    constexpr uintptr_t PipBoyBackground           = 0x00871DC0;
    constexpr uintptr_t WorldRenderCallSite        = 0x00870244;
    constexpr uintptr_t Render                     = 0x008706B0;
    constexpr uintptr_t IsPipBoyOpen               = 0x007079B0;
    constexpr uintptr_t RenderedMenuTargetCallSite = 0x0087086B;
    constexpr uintptr_t InitRenderTexture          = 0x00872F50;
    constexpr uintptr_t RenderedMenuBackgroundCallSite = 0x00870887;
    constexpr uintptr_t CopyMenuBackground         = 0x00874B90;
    constexpr uintptr_t PipBoyScreenCallSite       = 0x008709AA;
    constexpr uintptr_t RenderPipBoy               = 0x008761E0;

    /**
     * Pieces of the world render that Main::Swap's normal path calls
     * (LaTerreur::RenderSubfonction, 0x00870BD0), used to draw the live
     * world behind the Pip-Boy instead of the frozen background. The same
     * recipe as the Real-Time Menus plugin, which targets this executable,
     * without its image space pass: the rendered-menu render applies it.
     * All __thiscall on Main unless noted:
     * - DrawWorld_Init, returns the sun;
     * - DrawWorld_UpdateWater;
     * - DrawWorld_RestartRenderTexture (target, multisampling, clear flags);
     * - DrawWorld_DrawScene (sun, draw hands, wireframe, VATS);
     * - DrawWorld_Refraction and DrawWorld_DoDepth (accumulator, target);
     * - IsMultisampling, IsRefractionEnabled and the ImageSpaceManager
     *   singleton: __cdecl without arguments;
     * - RefractivePassCount: __thiscall on the world accumulator (Main+0x88);
     * - ImageSpaceManager::GetEffect (__thiscall, effect id, 4 is depth of
     *   field) and the effect's DoDepth (__thiscall, reads +0x70);
     * - ShadowSceneNode::IsWireframe: __thiscall on the world shadow scene
     *   node (first entry of 0x011F91C8).
     */
    constexpr uintptr_t DrawWorldInit            = 0x00872B00;
    constexpr uintptr_t DrawWorldUpdateWater     = 0x008727D0;
    constexpr uintptr_t DrawWorldRestartTarget   = 0x00874B50;
    constexpr uintptr_t DrawWorldDrawScene       = 0x00873200;
    constexpr uintptr_t DrawWorldRefraction      = 0x00874920;
    constexpr uintptr_t DrawWorldDoDepth         = 0x00875E40;
    constexpr uintptr_t IsMultisampling          = 0x004DC310;
    constexpr uintptr_t IsRefractionEnabled      = 0x005B9B00;
    constexpr uintptr_t ImageSpaceManagerSingleton = 0x004E3270;
    constexpr uintptr_t RefractivePassCount      = 0x00B63A90;
    constexpr uintptr_t ImageSpaceGetEffect      = 0x004EBBC0;
    constexpr uintptr_t ImageSpaceDoDepth        = 0x00871220;
    constexpr uint32_t  ImageSpaceDepthOfField   = 4;
    constexpr uintptr_t IsWireframe              = 0x00870770;
    constexpr uintptr_t ShadowSceneNodes         = 0x011F91C8;
    constexpr uintptr_t MainWorldAccumulator     = 0x88;

    /** Clear flags of NiRenderer: back buffer 1, depth 4 */
    constexpr uint32_t  ClearBackAndDepth = 5;
    constexpr uint32_t  ClearDepth        = 4;

    /**
     * DrawWorld_DrawScene reads iNumHWThreads (Setting::Int, __thiscall on
     * the setting, returns a pointer to its value) at this site and, above
     * 1, hands rendering back to the AI threads. Its normal caller has
     * synchronised the render threads for that; the rendered-menu render
     * has not, so the value reads 0 while the VR layer draws the world there.
     */
    constexpr uintptr_t DrawSceneThreadsCallSite = 0x008743B0;

    /**
     * Calls of the world render (LaTerreur::RenderSubfonction, 0x00870BD0)
     * that the VR layer times per eye, see findings/render.md "Passes drawn
     * per eye". All __thiscall on Main:
     * - DrawWorld_UpdateWater (no stack argument): water reflections from
     *   the world camera (TESWaterSystem::UpdateWaterSystem);
     * - DrawWorld_UpdateOffscreenBuffers (no stack argument): chooses the
     *   actors that cast shadows, culls them against the world camera and
     *   draws their shadow maps (ShadowSceneNode::UpdateDynamicShadows),
     *   then the Pip-Boy's offscreen menu;
     * - DrawWorld_DrawScene (sun, draw hands, wireframe, VATS);
     * - the first-person render (renderer, geometry, sun, target);
     * - DrawWorld_ImageSpace (multisampling, target, renderer, destination).
     * Both update functions also step the render threads' stages, which only
     * do something with bMTRendering:Display set (byte below, 0 by default).
     */
    constexpr uintptr_t WaterPassCallSite        = 0x00870C29;
    constexpr uintptr_t ShadowPassCallSite       = 0x00870C3C;
    constexpr uintptr_t DrawWorldShadows         = 0x00871290;
    constexpr uintptr_t ScenePassCallSite        = 0x00870E18;
    constexpr uintptr_t FirstPersonPassCallSite  = 0x00870F74;
    constexpr uintptr_t DrawWorldFirstPerson     = 0x00875110;

    /**
     * The first-person render starts by clearing the depth buffer:
     * ClearBuffer (__thiscall on the renderer: NiRect<float>* rect, flags;
     * ret 8) with flag 4, CLEAR_ZBUFFER, called at this site. Both passes
     * use the world camera's near and far planes (SceneGraph::SetCameraFOV
     * sets the same ones for either field of view) and the same render
     * target, so without the clear the model is tested against the world's
     * depth. See findings/grab.md §14.
     */
    constexpr uintptr_t FirstPersonDepthClearSite = 0x008751C6;
    constexpr uintptr_t RendererClearBuffer       = 0x007148C0;
    constexpr uint32_t  ClearDepthBuffer          = 4;
    constexpr uintptr_t ImageSpacePassCallSite   = 0x008710E4;
    constexpr uintptr_t DrawWorldImageSpace      = 0x00875FD0;
    constexpr uintptr_t MultiThreadedRendering   = 0x011F94B4;
    constexpr uintptr_t SettingInt               = 0x0043D4D0;

    /**
     * Shadow lights of the world shadow scene node (ShadowSceneNodes[0]):
     * the list's first item, each item being { next, previous, light }.
     * A shadow light keeps the planes of its shadow box in the clip space of
     * the camera that drew the first shadowed geometry, and only renders
     * again with fresh planes once the flag below is cleared, which
     * ShadowSceneLight::MTRenderFrustumDepthMap (0x00B9F780) does after
     * drawing the light's shadow map.
     */
    constexpr uint32_t ShadowSceneNodeShadowLightsOffset   = 0xC0;
    constexpr uint32_t ShadowListItemNextOffset            = 0x00;
    constexpr uint32_t ShadowListItemLightOffset           = 0x08;
    constexpr uint32_t ShadowLightClipPlanesReadyOffset    = 0x208;

    /**
     * The first-person model is drawn with its root's translation set to
     * this point (also pushed by PlaceCamera at FirstPersonZeroSite), so
     * that it sits near the origin. RenderPipBoy updates the model there
     * again and moves the world camera into that space.
     */
    constexpr uintptr_t FirstPersonDrawOrigin = 0x011F426C;

    /**
     * Sky placement, see findings/sky.md. DrawWorld_Init (above) moves the
     * sky root and the weather root (NiNode* globals) to the camera root's
     * translation at the start of the world render. Its calls in the world
     * render and the wireframe render.
     */
    constexpr uintptr_t DrawWorldInitCallSites[] = { 0x00870C1E, 0x00870A56 };
    constexpr uintptr_t SkyRoot                  = 0x011DEB34;
    constexpr uintptr_t WeatherRoot              = 0x011DEDA4;

    /**
     * Sun glare, see findings/sky.md. DrawWorld_Init returns the Sun; its
     * glare billboard (NiBillboardNode*) is at SunGlareNode. The billboard
     * face mode is stored in the low bits of the 16-bit flags at
     * BillboardFlags (NiBillboardNode::SetMode 0x0051D000).
     */
    constexpr uintptr_t SunGlareNode             = 0x0C;
    constexpr uintptr_t BillboardFlags           = 0xAC;
    constexpr uint16_t  BillboardModeMask        = 0x7;
    constexpr uint16_t  BillboardAlwaysFaceCenter = 3;

    /**
     * Settings pages of the start menu, see findings/options-menu.md. The
     * menu builds its pages from two global arrays of option objects, filled
     * by CreateMenuElements; an option's mask selects the page it shows on.
     *
     * - StartMenuGlobal: StartMenu*, null when no start menu exists;
     * - the option array and the user option array, and their append
     *   function (__thiscall on the array, one stack argument: a pointer to
     *   the option pointer), called at ControlsOptionAppendCallSite for the
     *   Controls entry of the Settings list;
     * - OptionConstruct: __thiscall (text, callback, mask), objects are
     *   OptionSize bytes from the game's heap (HeapAllocate, __cdecl size);
     * - UserOptionConstruct: __cdecl (object, text, callback, mask, type,
     *   number of settings, arrow spacing, then one name per setting),
     *   objects are UserOptionSize bytes;
     * - PopulateOptions: __thiscall on the menu with a mask, shows the
     *   options of a page; SetListEnabled: __thiscall on a list box with a
     *   bool; the Settings list box is at StartMenuSettingsList;
     * - StartMenuFlags: bit StartMenuOptionsInitialised is set once the
     *   option callbacks were run to initialise their values;
     * - the tile of the menu holds the page's _center_x, copied from the
     *   Gameplay page's trait (PageCenterTraitGameplay);
     * - Tile::GetFloat and Tile::SetFloat: __thiscall on the tile with the
     *   trait (and a float and a bool); AddUserTrait: __cdecl (name, id);
     * - the option array holds OptionArrayCount pointers at OptionArrayData,
     *   each option's mask at OptionMask;
     * - StartMenu::_Create sets up the Settings list box with ListBoxInitialize
     *   (__thiscall on the list box: its tile, the item template name) at
     *   SettingsListInitCallSite. The first item added fixes the list's
     *   height to the tile's _number_of_visible_items (5 in start_menu.xml),
     *   which the game updates only after adding them, so a longer list is
     *   cut off (findings/options-menu.md §4);
     * - StartMenu::_Create sets the menu's in-game flag, then sorts the
     *   options into its lists with Option::IsForModes (__thiscall on the
     *   option with the modes, returns a bool in AL); the Settings list's
     *   call is at SettingsEntryModesCallSite (findings/options-menu.md §5).
     */
    constexpr uintptr_t StartMenuGlobal              = 0x011DAAC0;
    constexpr uintptr_t OptionArray                  = 0x011DAB88;
    constexpr uintptr_t UserOptionArray              = 0x011DAB50;
    constexpr uintptr_t OptionArrayAppend            = 0x007CB2E0;
    constexpr uintptr_t ControlsOptionAppendCallSite = 0x007CCD02;
    constexpr uintptr_t OptionConstruct              = 0x007CE620;
    constexpr uintptr_t UserOptionConstruct          = 0x007D6350;
    constexpr uintptr_t HeapAllocate                 = 0x00401000;
    constexpr uintptr_t PopulateOptions              = 0x007D42B0;
    constexpr uintptr_t SetListEnabled               = 0x007D74F0;
    constexpr uintptr_t TileGetFloat                 = 0x00A011B0;
    constexpr uintptr_t TileSetFloat                 = 0x00A012D0;
    constexpr uintptr_t AddUserTrait                 = 0x00A00940;
    constexpr size_t    OptionSize                   = 0x10;
    constexpr size_t    UserOptionSize               = 0x30;
    constexpr uintptr_t StartMenuSettingsList        = 0xB4;
    constexpr uintptr_t StartMenuTile                = 0x04;
    constexpr uintptr_t StartMenuFlags               = 0x1A8;
    constexpr uint32_t  StartMenuOptionsInitialised  = 0x20;
    constexpr uintptr_t UserOptionValue              = 0x14;
    constexpr uintptr_t UserOptionDefaultValue       = 0x18;
    constexpr uint32_t  OptionMaskSettingsList       = 0x04;
    constexpr uintptr_t OptionArrayData              = 0x04;
    constexpr uintptr_t OptionArrayCount             = 0x08;
    constexpr uintptr_t OptionMask                   = 0x0C;
    constexpr uintptr_t ListBoxInitialize            = 0x0075A0D0;
    constexpr uintptr_t SettingsListInitCallSite     = 0x007CBBFD;
    constexpr uintptr_t OptionIsForModes             = 0x007CC5D0;
    constexpr uintptr_t SettingsEntryModesCallSite   = 0x007CBF27;
    constexpr char      VisibleItemsTraitName[]      = "_number_of_visible_items";
    constexpr uint32_t  PageCenterTraitGameplay      = 0x1005;
    constexpr char      CenterXTraitName[]           = "_center_x";

    /** First bytes of the functions above that the options page calls, checked before hooking */
    constexpr uint8_t   OptionConstructPrologue[]     = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B };
    constexpr uint8_t   UserOptionConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x18, 0xEA };
    constexpr uint8_t   PopulateOptionsPrologue[]     = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x89, 0x4D };
    constexpr uint8_t   OptionArrayAppendPrologue[]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D };
    constexpr uint8_t   TileGetFloatPrologue[]        = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D };
    constexpr uint8_t   TileSetFloatPrologue[]        = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x62, 0xCD };
    constexpr uint8_t   AddUserTraitPrologue[]        = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x94, 0xCC };
    constexpr uint8_t   OptionIsForModesPrologue[]    = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC, 0x8B, 0x40, 0x0C };
    constexpr uint8_t   HeapAllocatePrologue[]        = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xE8 };
    constexpr uint8_t   SetListEnabledPrologue[]      = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x4E, 0xEA };
    constexpr uint8_t   ListBoxInitializePrologue[]   = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B };

    /**
     * The console, see findings/console.md.
     *
     * - ConsoleGlobal: MenuConsole*, null until the interface creates it;
     *   ConsoleVisible (char, > 0 while shown), the printed lines (a list of
     *   nodes: next, previous, then a BSStringT, pointer and 16-bit length),
     *   the index of the last line shown (ConsoleLastLine, lower while the
     *   output is scrolled up) and the command line's position
     *   (ConsoleTextX, ConsoleTextY, integers);
     * - OpenConsoleRequest: byte TESMain::OnIdle reads each frame; when set,
     *   it clears it and opens the console (Interface::OpenConsole);
     * - CloseConsole: Interface::CloseConsole, __cdecl without arguments;
     * - ConsoleKeyCallSite: InterfaceManager::Idle takes the first key
     *   typed in a frame in menu mode from ConsoleReadKey
     *   (GetKeyFromScancodeAndSetModifiers, __thiscall with the key state
     *   and the scan code, returns the key or 0) and gives it to
     *   MenuConsole::Idle while the console is shown;
     * - DebugTextGlobal: DebugText*, which holds the command line; the
     *   console's MenuConsole::Idle reads it back with GetDebugText
     *   (__thiscall: a buffer of DebugTextBufferSize bytes, x and y as
     *   floats, the alignment ConsoleTextAlignment; returns whether found).
     *   The text cursor is drawn in it as ConsoleCursorChar.
     */
    constexpr uintptr_t ConsoleGlobal          = 0x011D8CE8;
    constexpr uintptr_t ConsolePrintedLines    = 0x04;
    constexpr uintptr_t ConsolePrintedCount    = 0x0C;
    constexpr uintptr_t ConsoleLastLine        = 0x24;
    constexpr uintptr_t ConsoleTextX           = 0x30;
    constexpr uintptr_t ConsoleTextY           = 0x34;
    constexpr uintptr_t ConsoleVisible         = 0x38;
    constexpr uintptr_t ConsoleLineNodeText    = 0x08;
    constexpr uintptr_t OpenConsoleRequest     = 0x011DEA2F;
    constexpr uintptr_t SetOpenConsoleRequest  = 0x0070ED20;
    constexpr uintptr_t CloseConsole           = 0x00703DA0;
    constexpr uintptr_t ConsoleKeyCallSite     = 0x0070E02F;
    constexpr uintptr_t ConsoleReadKey         = 0x007154B0;
    constexpr uintptr_t DebugTextGlobal        = 0x011F33A8;
    constexpr uintptr_t GetDebugText           = 0x00A103E0;
    constexpr size_t    DebugTextBufferSize    = 0x800;
    constexpr int32_t   ConsoleTextAlignment   = 1;
    constexpr char      ConsoleCursorChar      = '|';

    /** First bytes of the console functions above; SetOpenConsoleRequest writes OpenConsoleRequest */
    constexpr uint8_t   SetOpenConsoleRequestBytes[] = { 0x55, 0x8B, 0xEC, 0x8A, 0x45, 0x08, 0xA2, 0x2F, 0xEA, 0x1D, 0x01 };
    constexpr uint8_t   CloseConsolePrologue[]       = { 0x55, 0x8B, 0xEC, 0xE8, 0x68, 0x34, 0xDB, 0xFF };
    constexpr uint8_t   ConsoleReadKeyPrologue[]     = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D };
    constexpr uint8_t   GetDebugTextPrologue[]       = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0x89, 0x4D };

    /**
     * The game's rendered terminal, which shows the console in 3D, see
     * findings/console.md §6. All checked in Ghidra on the Steam executable.
     *
     * - TerminalConstruct: FORenderedTerminal::FORenderedTerminal,
     *   __thiscall on TerminalSize bytes from the game's heap. Its vtable:
     *   Draw (+0x0C, target, clear flags, unused target), OnIdle (+0x14),
     *   Initialize (+0x24), OnMenuOpen (+0x30), OnMenuClose (+0x34), all
     *   __thiscall. Fields: screen geometry, model (the monitor), menu
     *   root node (NiPointer), menu tile, camera (NiCamera*), initialised
     *   flag;
     * - InterfaceManager fields: the menus root tile, the "in a rendered
     *   menu" flag and the current rendered menu;
     * - the XML of the console's tiles is stored with CompressAndStoreXML
     *   (__cdecl: name, text) and XmlCacheSetAt (__thiscall on XmlCache:
     *   name, storage), then read by TileReadFile (__thiscall on the parent
     *   tile: name), which frees the storage, and taken out of the cache
     *   again with XmlCacheRemoveAt (__thiscall on XmlCache: name); TileGetChildByName (__thiscall: name), TileSetString
     *   (__thiscall: trait, text, propagate), traits below, a tile's
     *   destructor is vtable slot 0 (__thiscall, 1 frees);
     * - OffscreenInterfaceCallSite: DrawWorld_UpdateOffscreenBuffers calls
     *   UpdateOffscreenInterface (__cdecl) there, outside the renderer's
     *   frame; the rendered menu's Draw renders into InterfaceFxTexture
     *   (NiPointer<BSRenderedTexture>), borrowed from the texture manager
     *   (GetTextureManager __cdecl, BorrowRenderedTexture __thiscall:
     *   renderer, InterfaceFxTextureType, 0, 0, 0) and assigned with
     *   NiPointerAssign (__thiscall on the pointer: object); Main::Swap
     *   gives that texture back (ReturnRenderedTexture, __thiscall on the
     *   texture manager: texture) and clears the pointer at the end of each
     *   frame it draws, so the console keeps a texture of its own and lends
     *   it to InterfaceFxTexture only while it draws its screen;
     * - the debug text the console prints its lines and command line with
     *   is drawn under the node at InterfaceDebugTextRoot (NiNode*), which
     *   FORenderedMenu::Render culls for its own pass;
     * - DrawRenderedMenuParent and RenderPipBoy (above) draw the current
     *   rendered menu's model and screen with the menu's camera;
     * - the world's culling process (WorldSceneGraph holds the scene graph,
     *   its culling process at SceneGraphCuller) keeps the camera they set
     *   at CullerCamera;
     * - sComputersHeader1 and 2: the terminal's header lines (Setting*,
     *   text at SettingValue, above), whose address ComputersMenu::FinishIntro
     *   loads at ComputersHeaderUseSite.
     */
    constexpr uintptr_t TerminalConstruct          = 0x00705FE0;
    constexpr size_t    TerminalSize               = 0xF0;
    constexpr uintptr_t TerminalDraw               = 0x0C;
    constexpr uintptr_t TerminalOnIdle             = 0x14;
    constexpr uintptr_t TerminalInitialize         = 0x24;
    constexpr uintptr_t TerminalOnMenuOpen         = 0x30;
    constexpr uintptr_t TerminalOnMenuClose        = 0x34;
    constexpr uintptr_t TerminalScreen             = 0x04;
    constexpr uintptr_t TerminalModel              = 0x08;
    constexpr uintptr_t TerminalMenuRoot           = 0x14;
    constexpr uintptr_t TerminalMenuTile           = 0x18;
    constexpr uintptr_t TerminalCamera             = 0x24;
    constexpr uintptr_t TerminalInitialized        = 0x28;
    constexpr uintptr_t InterfaceMenusRoot         = 0x9C;
    constexpr uintptr_t InterfaceInRenderedMenu    = 0x168;
    constexpr uintptr_t InterfaceRenderedMenu      = 0x16C;
    constexpr uintptr_t CompressAndStoreXml        = 0x00A1CE70;
    constexpr uintptr_t XmlCacheSetAt              = 0x00A0C900;
    constexpr uintptr_t XmlCache                   = 0x011F34E0;
    constexpr uintptr_t XmlCacheRemoveAt           = 0x00A1E240;
    constexpr uintptr_t TileReadFile               = 0x00A01B00;
    constexpr uintptr_t TileGetChildByName         = 0x00A03DA0;
    constexpr uintptr_t TileSetString              = 0x00A01350;
    constexpr uint32_t  TileTraitVisible           = 0xFA3;
    constexpr uint32_t  TileTraitString            = 0xFC4;
    constexpr uintptr_t OffscreenInterfaceCallSite = 0x008719F8;
    constexpr uintptr_t UpdateOffscreenInterface   = 0x00871A50;
    constexpr uintptr_t InterfaceFxTexture         = 0x011DEC64;
    constexpr uintptr_t GetTextureManager          = 0x004A0EA0;
    constexpr uintptr_t BorrowRenderedTexture      = 0x00B6E110;
    constexpr uint32_t  InterfaceFxTextureType     = 0x2D;
    constexpr uintptr_t NiPointerAssign            = 0x0066B0D0;
    constexpr uintptr_t ReturnRenderedTexture      = 0x00B6DA10;
    constexpr uintptr_t InterfaceDebugTextRoot     = 0xA4;
    constexpr uintptr_t WorldSceneGraph            = 0x011DEB7C;
    constexpr uintptr_t SceneGraphCuller           = 0xB4;
    constexpr uintptr_t CullerCamera               = 0x0C;
    constexpr uintptr_t ComputersHeader1           = 0x011D3D78;
    constexpr uintptr_t ComputersHeader2           = 0x011D4978;

    /** First bytes of the functions above, checked before any is used */
    constexpr uint8_t   TerminalConstructPrologue[]    = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xE8, 0x79, 0xF0, 0x00 };
    constexpr uint8_t   CompressAndStoreXmlPrologue[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x6C, 0xC7, 0x45, 0xF4 };
    constexpr uint8_t   XmlCacheSetAtPrologue[]        = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x89, 0x4D, 0xD4 };
    constexpr uint8_t   XmlCacheRemoveAtPrologue[]     = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x89, 0x4D, 0xEC };
    constexpr uint8_t   TileReadFilePrologue[]         = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x19, 0xCE, 0xF1, 0x00 };
    constexpr uint8_t   TileGetChildByNamePrologue[]   = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xC3, 0xCE, 0xF1, 0x00 };
    constexpr uint8_t   TileSetStringPrologue[]        = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x85, 0xCD, 0xF1, 0x00 };
    constexpr uint8_t   GetTextureManagerBytes[]       = { 0x55, 0x8B, 0xEC, 0xA1, 0xA8, 0x91, 0x1F, 0x01, 0x5D, 0xC3 };
    constexpr uint8_t   BorrowRenderedTexturePrologue[] = { 0x6A, 0xFF, 0x68, 0xB8, 0x89, 0xF2, 0x00 };
    constexpr uint8_t   ReturnRenderedTexturePrologue[] = { 0x6A, 0xFF, 0x68, 0xD0, 0x88, 0xF2, 0x00 };
    constexpr uint8_t   NiPointerAssignPrologue[]      = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC };
    constexpr uint8_t   DrawRenderedMenuParentPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38, 0x56, 0x89, 0x4D, 0xE0 };
    constexpr uint8_t   RenderPipBoyPrologue[]         = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00 };
    constexpr uint8_t   ComputersHeaderUse[]           = { 0xB9, 0x78, 0x3D, 0x1D, 0x01 };
    constexpr uintptr_t ComputersHeaderUseSite         = 0x00758DCF;

    /**
     * The world terminal's keyboard on the console's monitor, see
     * findings/console.md §11. Found in the GOG database, bytes compared
     * with the Steam executable in Ghidra.
     *
     * - ModelLoaderLoadFile: ModelLoader::LoadFile, __thiscall on the
     *   loader (ModelLoaderSingleton, loaded at ModelLoaderSingletonUseSite):
     *   path, LOD fade type, assign shaders, 0, keep UVs, no use count;
     *   returns the model's cached root (NiNode*), which the references of
     *   the world may use directly, so it is cloned;
     * - ObjectClone: NiObject::Clone, __thiscall: cloning process, built by
     *   CloningProcessConstruct (__thiscall: scale) on CloningProcessSize
     *   bytes and destroyed by CloningProcessDestruct (__thiscall);
     * - ObjectGetController: NiObjectNET::GetController, __thiscall: RTTI,
     *   with NiControllerManager's (ControllerManagerRtti, pushed at
     *   ControllerManagerRttiUseSite);
     * - NiControllerManager: its flags (NiTimeController, ControllerActive
     *   set as NiTimeController::SetActive does), its sequences (NiTArray:
     *   items, used size);
     *   ActivateSequence (__thiscall: sequence, priority, start over,
     *   weight, ease in, time sync sequence), DeactivateSequence
     *   (__thiscall: sequence, ease out), DeactivateAll (__thiscall: ease
     *   out); StartAnimations (__cdecl: object) starts every controller;
     * - NiControllerSequence: name, end key time, last scaled time, offset
     *   (SequenceOffset above; -FLT_MAX starts it at the next update);
     * - NiControllerSequence: its text keys (NiTextKeyExtraData, held by
     *   a NiPointer at SequenceTextKeys: key count and array of 8-byte
     *   NiTextKey, time then text) and the scaled time of the update
     *   before the last (SequenceStartTime), read as
     *   BSAnimGroupSequence::PlaySounds (0x004EEF00) reads them. That
     *   function plays a sound with no reference as a 3D sound at the
     *   world's origin, out of hearing, so the fork plays the "Sound:"
     *   keys itself: DataHandlerGetSound (__thiscall on the data handler:
     *   editor ID) finds the TESSound;
     * - NiNode vtable: AttachChild (__thiscall: child, first free slot),
     *   DetachChild without result (__thiscall: child);
     * - NiAVObject: its collision object (NiPointer); NiUpdateData (time,
     *   then flags, the first one "update the controllers"), UpdateDataSize
     *   bytes, for ObjectUpdate;
     * - BSFadeNode: FadeNodeSetRange (__thiscall: near, far),
     *   FadeNodeTurnOff (__thiscall) and the LOD multiplier type, set as
     *   FORenderedTerminal sets them on its monitor (LodMultRenderedMenu);
     * - ShadowSceneNodeAddObject and ShadowSceneNodeRemoveObject
     *   (__thiscall: node) link a model's geometry with the lights of a
     *   shadow scene node, here the menu scene's
     *   (ShadowSceneNodes[MenuShadowSceneNode]);
     * - shader property (ObjectGetProperty, ShadePropertyType): its
     *   textures (array of texture arrays, [0][0] the base map), a texture's
     *   renderer data, and the Direct3D texture in it.
     */
    constexpr uintptr_t ModelLoaderLoadFile           = 0x00447080;
    constexpr uintptr_t ModelLoaderSingleton          = 0x011C3B3C;
    constexpr uintptr_t ModelLoaderSingletonUseSite   = 0x007FEF24;
    constexpr uintptr_t ObjectClone                   = 0x00A5D2C0;
    constexpr uintptr_t CloningProcessConstruct       = 0x004AD050;
    constexpr uintptr_t CloningProcessDestruct        = 0x004AD270;
    constexpr size_t    CloningProcessSize            = 0x1C;
    constexpr uintptr_t ObjectGetController           = 0x00A5C570;
    constexpr uintptr_t ControllerManagerRtti         = 0x011F36AC;
    constexpr uintptr_t ControllerManagerRttiUseSite  = 0x00732818;
    constexpr uintptr_t ControllerManagerSequences    = 0x38;
    constexpr uintptr_t ControllerManagerSequenceCount = 0x3E;
    constexpr uintptr_t ControllerFlags               = 0x08;
    constexpr uint16_t  ControllerActive              = 0x8;
    constexpr uintptr_t ActivateSequence              = 0x0047AAB0;
    constexpr uintptr_t DeactivateSequence            = 0x0047B220;
    constexpr uintptr_t DeactivateAllSequences        = 0x0048FEF0;
    constexpr uintptr_t StartAnimations               = 0x00A6D2D0;
    constexpr uintptr_t SequenceName                  = 0x08;
    constexpr uintptr_t SequenceLastScaledTime        = 0x3C;
    constexpr uintptr_t SequenceTextKeys              = 0x20;
    constexpr uintptr_t SequenceStartTime             = 0x4C;
    constexpr uintptr_t TextKeyCount                  = 0x0C;
    constexpr uintptr_t TextKeyArray                  = 0x10;
    constexpr size_t    TextKeySize                   = 0x08;
    constexpr uintptr_t TextKeyText                   = 0x04;
    constexpr uintptr_t DataHandlerGetSound           = 0x004616C0;
    constexpr uintptr_t NodeAttachChildSlot           = 0xDC;
    constexpr uintptr_t NodeDetachChildSlot           = 0xE8;
    constexpr uintptr_t ObjectCollision               = 0x1C;
    constexpr size_t    UpdateDataSize                = 0x0C;
    constexpr uintptr_t FadeNodeSetRange              = 0x00B4DFD0;
    constexpr uintptr_t FadeNodeTurnOff               = 0x0054B800;
    constexpr uintptr_t FadeNodeLodMultType           = 0xC4;
    constexpr uint32_t  LodMultRenderedMenu           = 8;
    constexpr uintptr_t ShadowSceneNodeAddObject      = 0x00B5EEB0;
    constexpr uintptr_t ShadowSceneNodeRemoveObject   = 0x00B5B1C0;
    constexpr uint32_t  MenuShadowSceneNode           = 1;
    constexpr uint32_t  ShadePropertyType             = 3;
    constexpr uintptr_t ShaderPropertyTextures        = 0xAC;
    constexpr uintptr_t TextureRendererData           = 0x24;
    constexpr uintptr_t TextureDataD3DTexture         = 0x64;

    /** First bytes of the functions and uses above, checked before any is used */
    constexpr uint8_t   ModelLoaderLoadFilePrologue[]     = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x88, 0xCA, 0xEE, 0x00 };
    constexpr uint8_t   ModelLoaderSingletonUse[]         = { 0x8B, 0x0D, 0x3C, 0x3B, 0x1C, 0x01 };
    constexpr uint8_t   ObjectClonePrologue[]             = { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8B, 0xF1 };
    constexpr uint8_t   CloningProcessConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x58, 0x04, 0xEF, 0x00 };
    constexpr uint8_t   CloningProcessDestructPrologue[]  = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8, 0x51, 0xFF, 0xFF, 0xFF };
    constexpr uint8_t   ObjectGetControllerPrologue[]     = { 0x56, 0x8B, 0x71, 0x0C, 0x57, 0x85, 0xF6, 0x74, 0x29 };
    constexpr uint8_t   ControllerManagerRttiUse[]        = { 0x68, 0xAC, 0x36, 0x1F, 0x01 };
    constexpr uint8_t   ActivateSequencePrologue[]        = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x6A, 0x00, 0x8B, 0x45, 0x1C };
    constexpr uint8_t   DeactivateSequencePrologue[]      = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x6A, 0x00, 0x51, 0xD9, 0x45, 0x0C };
    constexpr uint8_t   DeactivateAllSequencesPrologue[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4 };
    constexpr uint8_t   StartAnimationsPrologue[]         = { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0x77, 0x0C };
    constexpr uint8_t   DataHandlerGetSoundPrologue[]     = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0x08, 0x50 };
    constexpr uint8_t   FadeNodeSetRangePrologue[]        = { 0x51, 0xA1, 0x4C, 0x91, 0x1F, 0x01, 0x89, 0x04, 0x24 };
    constexpr uint8_t   FadeNodeTurnOffPrologue[]         = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC, 0xD9, 0xEE };
    constexpr uint8_t   ShadowSceneNodeAddObjectPrologue[] = { 0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x0E, 0xF6, 0x40, 0x30, 0x01 };
    constexpr uint8_t   ShadowSceneNodeRemoveObjectPrologue[] = { 0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x08, 0x85, 0xC9, 0x74, 0x13 };

    /**
     * Files of the game read through its own lookup, loose or in an archive,
     * see findings/console.md §7. FileFinderGetFile: __cdecl (path, mode
     * FileReadOnly, buffer size, archive type), returns a BSFile* or null.
     * A BSFile: vtable slot 0 destroys it (__thiscall, 1 frees), slot
     * FileGetSize (__thiscall) gives its size, FileRead (__cdecl: file,
     * buffer, bytes, component sizes, component count) reads it, FilePosition
     * is moved by the caller, FileGood is set when it opened.
     */
    constexpr uintptr_t FileFinderGetFile    = 0x00AFDF20;
    constexpr uint32_t  FileReadOnly         = 0;
    constexpr uint32_t  FileBufferSize       = 0x4000;
    constexpr uint32_t  ArchiveTextures      = 0x2;
    constexpr uintptr_t FileGetSize          = 0x28;
    constexpr uintptr_t FileRead             = 0x08;
    constexpr uintptr_t FilePosition         = 0x04;
    constexpr uintptr_t FileGood             = 0x2C;
    constexpr uint8_t   FileFinderGetFilePrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x95, 0x37, 0xF2, 0x00 };

    /**
     * Menu tile internals (VrTileTextureRetry), see findings/pipboy.md §3.7.
     * Tile: vtable slot 3 (+0x0C) is GetType (__thiscall, 0x386 = image),
     * children list head at +0x04 (node: next at +0, Tile* at +8), name
     * (char*) at +0x20, flags at +0x30 (0x3FF update bits, 0x20 texture).
     * Tile::GetValue (__thiscall, trait id) returns a Value* (number at +8,
     * string at +0xC) or null when the tile has no such trait.
     * Tile::AddNeedsUpdate (__thiscall, update bits) marks the tile dirty.
     * NiAVObject::GetProperty (__thiscall, type, 3 = shade) returns the
     * image's TileShaderProperty, whose texture is at +0x60. The tile update
     * lock is a CRITICAL_SECTION.
     */
    constexpr uintptr_t TileChildren        = 0x04;
    constexpr uintptr_t TileName            = 0x20;
    constexpr uintptr_t TileFlags           = 0x30;
    constexpr uintptr_t TileGetTypeSlot     = 0x0C;
    constexpr uint32_t  TileTypeImage       = 0x386;
    constexpr uint32_t  TileUpdateTexture   = 0x20;
    constexpr uintptr_t TileGetValue        = 0x00A00F30;
    constexpr uint8_t   TileGetValuePrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x89, 0x4D, 0xF0 };
    constexpr uintptr_t TileAddNeedsUpdate  = 0x00A07530;
    constexpr uint8_t   TileAddNeedsUpdatePrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8,
                                                          0x81, 0x7D, 0x08, 0xFF, 0x03, 0x00, 0x00 };
    constexpr uintptr_t ObjectGetProperty   = 0x00A59D30;
    constexpr uint8_t   ObjectGetPropertyPrologue[] = { 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x83, 0xFB, 0x07 };
    constexpr int32_t   PropertyTypeShade   = 3;
    constexpr uintptr_t TileShaderTexture   = 0x60;
    constexpr uintptr_t TileUpdateLock      = 0x011F3330;
    constexpr uint32_t  TraitTextureAtlas   = 0xFF9;

    /**
     * Developer mode (VrDeveloperMode), see findings/developer-mode.md.
     * SetGodMode (__cdecl, bool) is what the console's tgm calls. The data
     * handler's object list (+0x04) chains every loaded TESObject through
     * +0x20. A weapon (form type 0x28; armour is 0x18) is not playable when bit 0x80 of its
     * flags at +0x100 is set. Items are added as the console's AddItem does:
     * a TESContainer (12 bytes) built on the stack, filled with AddObject
     * (__thiscall, object, count, extra) and handed to the player with
     * AddContainerToReference (__thiscall, reference, silent).
     * GetInventoryChanges (__cdecl, reference) and GetObjectCount
     * (__thiscall, object) count an item in an inventory. A weapon's type
     * is WeaponAnimationType (0 HandToHandMelee to 13 OneHandThrown), its
     * BGSAmmoForm is at +0xA4 and its clip size a byte at +0xB0 (getter
     * 0x00525BE0). GetAmmo (BGSAmmoForm::GetAmmoHelper, __thiscall on the
     * BGSAmmoForm) returns the ammo, or an entry of the ammo list.
     */
    constexpr uintptr_t SetGodMode                 = 0x009526A0;
    constexpr uint8_t   SetGodModeCode[]           = { 0x55, 0x8B, 0xEC, 0x8A, 0x45, 0x08, 0xA2, 0xBA, 0x07, 0x1E, 0x01 };
    constexpr uintptr_t DataHandler                = 0x011C3F2C;
    constexpr uintptr_t DataHandlerObjectList      = 0x04;
    constexpr uintptr_t ObjectListRoot             = 0x04;
    constexpr uintptr_t ObjectNext                 = 0x20;
    constexpr uintptr_t FormType                   = 0x04;
    constexpr uintptr_t FormFlags                  = 0x08;
    constexpr uint32_t  FormFlagDeleted            = 0x20;
    constexpr uint8_t   FormTypeWeapon             = 0x28;
    constexpr uint8_t   FormTypeArmor              = 0x18;
    constexpr uintptr_t WeaponFlags                = 0x100;
    constexpr uint8_t   WeaponFlagNotPlayable      = 0x80;
    /// TESObjectWEAP::IsAutomaticWeapon (0x00524B40) tests this bit of the
    /// same flags, see findings/melee.md §11
    constexpr uint8_t   WeaponFlagAutomatic        = 0x02;
    constexpr size_t    ContainerSize              = 0x0C;
    constexpr uintptr_t ContainerConstruct         = 0x00481610;
    constexpr uintptr_t ContainerDestruct          = 0x00481680;
    constexpr uint8_t   ContainerConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x1B, 0x8F, 0xF0, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t ContainerAddObject         = 0x004818E0;
    constexpr uint8_t   ContainerAddObjectPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xB6, 0x77, 0xF1, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t ContainerAddToReference    = 0x004821A0;
    constexpr uint8_t   ContainerAddToReferencePrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x79, 0xEF, 0xEE, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t GetInventoryChanges        = 0x004BF220;
    constexpr uint8_t   GetInventoryChangesPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x13, 0x12, 0xEF, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t InventoryObjectCount       = 0x004C8F30;
    constexpr uint8_t   InventoryObjectCountPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x89, 0x4D, 0xF0, 0xC7, 0x45, 0xF4 };
    constexpr uint8_t   FormTypeAmmo               = 0x29;
    constexpr uintptr_t WeaponAmmoForm             = 0xA4;
    constexpr uintptr_t WeaponClipRounds           = 0xB0;
    constexpr uintptr_t AmmoFormGetAmmo            = 0x00474920;
    constexpr uint8_t   AmmoFormGetAmmoPrologue[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x89, 0x4D, 0xEC,
                                                     0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00 };

    /**
     * Melee hits, see findings/melee.md §8. Actor::CombatHit(target, power,
     * projectile, meleeEffect) is __thiscall with four stack arguments,
     * the bytes read as bytes (ret 0x10); Actor::MeleeAttack calls it with
     * no projectile when the swing animation reaches its Hit key. Checked
     * by its prologue (SEH frame with handler 0x00F14D56).
     */
    constexpr uintptr_t CombatHit = 0x00899CB0;
    constexpr uint8_t   CombatHitPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x56, 0x4D, 0xF1, 0x00,
                                                0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };

    /**
     * Actor::MeleeAttack(power, inCombat), __thiscall, two byte arguments
     * (ret 8). Actor::HandlePostAnimationActions calls it at this site for
     * the player when the swing animation reaches its Hit key, see
     * findings/melee.md §11.
     */
    constexpr uintptr_t MeleeAttack         = 0x00899200;
    constexpr uintptr_t MeleeAttackCallSite = 0x008BAEFE;

    /**
     * ReferenceProcessLists (0x011E0E80, as CombatUtilities::GetFacingTarget
     * reads it): its process array at +4 starts with a NiTArray of actors
     * (data pointer at +4) and holds the end index of each process level at
     * +0x20 + level * 4. High-process actors come first, up to the end of
     * level 0. The player is not in it.
     */
    constexpr uintptr_t ProcessActors      = 0x011E0E88;
    constexpr uintptr_t ProcessHighEnd     = 0x011E0EA4;

    /**
     * TESObjectREFR::IsActor vtable slot, __thiscall without arguments,
     * returns a bool (called by GetFacingTarget on each process entry).
     */
    constexpr uintptr_t RefIsActorSlot     = 0x100;

    /**
     * Actor::IsDead(bool), vtable slot, __thiscall, ret 4, returns a bool
     * (called on the target by HitData::CheckCriticalStrike, Steam
     * 0x009B7060, with 0).
     */
    constexpr uintptr_t ActorIsDeadSlot    = 0x22C;

    /**
     * Hit limb, see findings/melee.md §10. HitData::SetTargetedLimb
     * (Projectile*), __thiscall, ret 4, picks the limb of a melee hit along
     * the attacker's view; the non-projectile HitData::InitializeHitData
     * calls it once, after the damage, critical and armour are computed.
     * The callees it uses are checked by their calls inside it.
     */
    constexpr uintptr_t SetTargetedLimb         = 0x009B6620;
    constexpr uintptr_t SetTargetedLimbCallSite = 0x009B562D;

    /// Actor::GetActorBase, __thiscall without arguments
    constexpr uintptr_t ActorGetBase            = 0x004181E0;
    constexpr uintptr_t ActorGetBaseCallSite    = 0x009B6693;
    /// TESActorBase::GetBodyPartData, virtual, __thiscall without arguments
    constexpr uintptr_t ActorBaseBodyPartDataSlot = 0x180;
    /// Actor::GetDamageLocation(NiNode*), __thiscall, ret 4: the limb of a
    /// bone (walking up its parents), 14 for the weapon or -1
    constexpr uintptr_t ActorGetDamageLocation  = 0x008B3EF0;
    constexpr uintptr_t ActorGetDamageLocationCallSite = 0x009B6AA9;
    /// BaseProcess::GetDamageNode(LIMB_ENUM), virtual, __thiscall, ret 4
    constexpr uintptr_t ProcessDamageNodeSlot   = 0x6D0;
    /// BGSBodyPartData::GetBodyPart(LIMB_ENUM), __thiscall, ret 4
    constexpr uintptr_t BodyPartDataGetPart     = 0x005E50F0;
    constexpr uintptr_t BodyPartDataGetPartCallSite = 0x009B6AFB;
    /// BGSBodyPart::GetPartType, __thiscall without arguments
    constexpr uintptr_t BodyPartGetType         = 0x005E5300;
    constexpr uintptr_t BodyPartGetTypeCallSite = 0x009B6ED7;
    /// CombatFormulas::CalcTargetedLimbDamage(ActorValueOwner*, BGSBodyPart*,
    /// float), __cdecl, float in ST0
    constexpr uintptr_t CalcTargetedLimbDamage  = 0x00647A30;
    constexpr uintptr_t CalcTargetedLimbDamageCallSite = 0x009B6F42;
    /// TESObjectWEAP::GetLimbDamageMult, __thiscall without arguments, float in ST0
    constexpr uintptr_t WeaponGetLimbDamageMult = 0x009B7040;
    constexpr uintptr_t WeaponGetLimbDamageMultCallSite = 0x009B6FC5;
    /// HitData::SetBonusMult(float), __thiscall, ret 4: keeps the largest
    constexpr uintptr_t HitDataSetBonusMult     = 0x009B7840;
    constexpr uintptr_t HitDataSetBonusMultCallSite = 0x009B6FE3;

    /// Actor's ActorValueOwner base
    constexpr uintptr_t ActorValueOwnerBase     = 0xA4;
    /// ActorValueOwner vtable slots, __thiscall, ret 4, float in ST0: the
    /// base value (called with Health by CalcTargetedLimbDamage) and the
    /// current one, next but one in the same table (IDA)
    constexpr uintptr_t AvOwnerBaseFloatSlot    = 0x04;
    constexpr uintptr_t AvOwnerFloatSlot        = 0x0C;
    /// ActorValue::Index of Health
    constexpr uint32_t  AvHealth                = 16;

    /// HitData fields
    constexpr uintptr_t HitAggressor            = 0x00;
    constexpr uintptr_t HitTarget               = 0x04;
    constexpr uintptr_t HitDamageLimb           = 0x10;
    constexpr uintptr_t HitHealthDamage         = 0x14;
    constexpr uintptr_t HitLimbDamage           = 0x20;
    constexpr uintptr_t HitWeapon               = 0x30;
    constexpr uintptr_t HitLocation             = 0x38;
    constexpr uintptr_t HitDirection            = 0x44;
    constexpr uintptr_t HitVatsCommand          = 0x54;
    constexpr uintptr_t HitFlags                = 0x58;

    /// HitData::Flag bits, set by HitData::CheckCriticalStrike (Steam 0x009B7060)
    constexpr uint32_t  HitFlagCritical         = 0x004;
    constexpr uint32_t  HitFlagSneakAttack      = 0x400;

    /// BGSBodyPart::LIMB_ENUM: torso 0 to brain 13, then the weapon
    constexpr int32_t   LimbWeapon              = 14;

    /**
     * Charged (power) attacks, see findings/melee.md §12.
     * PlayerCharacter::CheckUserInputAttacks adds the frame time (the
     * application timer's step after the time multiplier, BSTimer::fDelta at
     * +0x0C, BSTimer::GetSeconds 0x0084D030) to its power timer each frame
     * the attack control stays held after the press, and makes the attack a
     * power attack once the timer exceeds fPowerAttackDelay (Setting, value
     * at SettingValue). Not while overencumbered (PlayerCharacter vtable
     * slot below, __thiscall, returns a bool) nor while swimming
     * (Actor::IsSwimming, __thiscall, returns a bool), called from the
     * site below.
     */
    constexpr uintptr_t PowerAttackDelay        = 0x011CDDA8;
    constexpr uintptr_t TimerDelta              = 0x0C;
    constexpr uintptr_t PlayerOverEncumberedSlot = 0x358;
    constexpr uintptr_t ActorIsSwimming         = 0x005A2030;
    constexpr uintptr_t ActorIsSwimmingCallSite = 0x009495FD;

    /**
     * Action points of a power attack, see findings/melee.md §12.2.
     * PlayerCharacter::DamageActionPoints(float) (__thiscall, ret 4) takes
     * action points away, none in god mode or while a save loads.
     * PlayerCharacter::IsGodMode (__cdecl, returns a bool): tgm or the
     * demigod setting. The weapon's weight is what the inventory counts
     * (InventoryChanges::GetInventoryWeight, calls below):
     * TESObjectWEAP::GetWeight(bool modded) (__thiscall, ret 4, float in
     * ST0) with ItemChange::HasModEffectActive(effect) (__thiscall, ret 4,
     * returns a bool) for the weight mod (effect 4), then Pack Rat: if
     * BGSEntryPoint::HandleEntryPoint (__cdecl, entry point, perk owner,
     * float* result) gives Modify Light Items (0x37) a value above 0, an
     * item no heavier than fPackRatThreshold weighs fPackRatModifier times
     * its weight.
     */
    constexpr uintptr_t PlayerDamageActionPoints = 0x0093AB80;
    constexpr uint8_t   PlayerDamageActionPointsPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0xE8, 0x24, 0x7B, 0x01, 0x00 };
    constexpr uintptr_t PlayerIsGodMode         = 0x009526B0;
    constexpr uint8_t   PlayerIsGodModePrologue[] = { 0x55, 0x8B, 0xEC, 0xB9, 0x94, 0x08, 0x1E, 0x01, 0xE8, 0xA3, 0x66, 0xAB };
    constexpr uintptr_t WeaponGetWeight         = 0x004BE380;
    constexpr uintptr_t WeaponGetWeightCallSite = 0x004D0A43;
    constexpr uintptr_t ItemHasModEffect        = 0x004BDA70;
    constexpr uintptr_t ItemHasModEffectCallSite = 0x004D0A37;
    constexpr uint32_t  WeaponModDecreaseWeight = 4;
    constexpr uintptr_t HandleEntryPoint        = 0x005E58F0;
    constexpr uintptr_t HandleEntryPointCallSite = 0x004D094C;
    constexpr uint32_t  EntryPointModifyLightItems = 0x37;
    constexpr uintptr_t PackRatModifier         = 0x011C64A8;
    constexpr uintptr_t PackRatThreshold        = 0x011C6478;
    /// ActorValue::Index of ActionPoints
    constexpr uint32_t  AvActionPoints          = 12;

    /**
     * Manual reload, see findings/reload.md. Every reload of the player goes
     * through PlayerCharacter::ReloadWeaponNV, reached through this entry
     * of the PlayerCharacter vtable: __thiscall with the weapon, the reload
     * animation (0 none, 1 queued, 2 played at once), whether the extended
     * magazine counts and whether a looping weapon loads the whole
     * inventory (both bools on the stack), ret 0x10, returns a bool in AL.
     * Its callers are told apart by their return address: the reload
     * control (CheckUserInputAttacks) and the automatic reload when a shot
     * empties the clip (Actor::UseAmmo).
     */
    constexpr uintptr_t PlayerReloadWeaponSlot = 0x0108AE28;
    constexpr uintptr_t PlayerReloadWeapon     = 0x0095D3F0;
    constexpr uintptr_t ReloadReturnControl    = 0x00949803;
    constexpr uintptr_t ReloadReturnEmptyClip  = 0x008A8C31;

    /**
     * The clip: the process's virtual GetCurrentAmmo (MiddleHighProcess
     * 0x005F7590 reads +0x118) returns an inventory entry whose count is
     * the rounds in the clip.
     */
    constexpr uintptr_t ProcessCurrentAmmoSlot = 0x14C;
    constexpr uintptr_t InventoryEntryCount    = 0x04;

    /**
     * Jams: when Actor::StartAttack rolls a jam it looks up the jam
     * animation group with Actor::GetAnimGroup (__thiscall with the group,
     * the weapon entry, a hand-to-hand check and the animation, ret 0x10,
     * returns the 16-bit group) at this call, and starts neither the jam
     * nor the attack if the group returned is not the jam group.
     */
    constexpr uintptr_t JamAnimGroupCallSite   = 0x00894040;
    constexpr uintptr_t ActorGetAnimGroup      = 0x00897910;

    /**
     * Before it rolls a jam, Actor::StartAttack reads the clip's count
     * with the inventory entry's GetCount (__thiscall, returns [ecx+4]) at
     * these two calls: with fewer rounds than a shot uses and none at all,
     * the attack does not start and the player hears the weapon's attack
     * fail sound (the dry click) when the attack control was just pressed.
     * See findings/reload.md §5.2.
     */
    constexpr uintptr_t StartAttackClipCallSite    = 0x00893B88;
    constexpr uintptr_t StartAttackClipEmptyCallSite = 0x00893C24;
    constexpr uintptr_t InventoryEntryGetCount     = 0x00726070;

    /**
     * Weapon form: its attack fail sound (TESSound*,
     * TESObjectWEAP::GetAttackFailSound 0x005224C0)
     */
    constexpr uintptr_t WeaponAttackFailSound      = 0x22C;

    /**
     * The message V.A.T.S. shows when the player has too little ammunition,
     * see findings/reload.md §4.1. VATSMenu::DoIdle reads the
     * sVATSMessageNoAmmo game setting (string value at SettingValue, "You
     * don't have enough ammo." in English) at the checked site and hands it
     * to HUDMainMenu::ShowMessage with the sad face (2), no image, no sound,
     * 2 s and not instant. Interface::ShowMessage (__cdecl, same arguments)
     * does the same once the HUD exists.
     */
    constexpr uintptr_t NoAmmoMessageSetting   = 0x011D28C8;
    constexpr uintptr_t NoAmmoMessageReadSite  = 0x007EE86C;
    constexpr uint8_t   NoAmmoMessageRead[] = {
      0xB9, 0xC8, 0x28, 0x1D, 0x01, 0xE8, 0x7A, 0x55, 0xC1, 0xFF,
      0x50, 0x8B, 0x0D, 0xC0, 0x96, 0x1D, 0x01, 0xE8, 0xFE, 0x6A, 0xF8, 0xFF,
    };
    constexpr uintptr_t ShowHudMessage         = 0x007052F0;
    constexpr uint8_t   ShowHudMessagePrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x38, 0xC3, 0xEE, 0x00 };
    constexpr uint32_t  HudMessageSad          = 2;

    /**
     * What V.A.T.S. shows by its action point meter when the player has
     * too few of them, see findings/melee.md §12.5: the sActionPointsShort
     * label ("AP") and the sVATSMessageLowAP text beside it, both read by
     * VATSMenu::Create at the checked sites (string value at SettingValue).
     * The inventory plays UIVATSInsufficientAP when action points or a
     * hotkey are refused.
     */
    constexpr uintptr_t ActionPointsShortSetting  = 0x011D3E8C;
    constexpr uintptr_t ActionPointsShortReadSite = 0x007EA40A;
    constexpr uint8_t   ActionPointsShortRead[]   = { 0xB9, 0x8C, 0x3E, 0x1D, 0x01, 0xE8, 0xDC, 0x99, 0xC1, 0xFF };
    constexpr uintptr_t LowApMessageSetting       = 0x011D1D90;
    constexpr uintptr_t LowApMessageReadSite      = 0x007EA497;
    constexpr uint8_t   LowApMessageRead[]        = { 0xB9, 0x90, 0x1D, 0x1D, 0x01, 0xE8, 0x4F, 0x99, 0xC1, 0xFF };

    /**
     * Weapon form: the data of its name (TESFullName at +0x30) and of its
     * model's path (TESModel at +0x3C). Its clip size is WeaponClipRounds.
     */
    constexpr uintptr_t WeaponFullName         = 0x34;
    constexpr uintptr_t WeaponModelPath        = 0x40;

    /**
     * Whether the weapon reloads one round at a time (TESObjectWEAP::
     * GetIsLoopingReload 0x004938C0 reads this byte).
     */
    constexpr uintptr_t WeaponLoopingReload    = 0x384;

    /**
     * The reload callers ask ItemHasModEffect (above) for the extended
     * magazine's effect on the weapon's inventory entry. ReloadWeapon
     * counts the rounds it can put in the clip with InventoryObjectCount
     * (above) on GetInventoryChanges.
     */
    constexpr uint32_t  ModEffectClipSize      = 2;

    /**
     * The weapon's reload animation, read without playing it, see
     * findings/reload.md §8.2 (GOG database, bytes checked in Ghidra).
     *
     * TESObjectWEAP::GetReloadAnim, __thiscall with the extended magazine
     * bool, ret 4, returns the group (0xB1 + the weapon's reload
     * animation). Actor::GetAnimGroup (above) turns it into the full
     * 16-bit group of an AnimData, whose low byte is that group if the
     * AnimData has it. The AnimData's sequences are a NiTPointerMap from
     * the full group to an AnimSequenceBase, whose GetAt is __thiscall with
     * the group and the result's address, ret 8, returns a bool in AL; the
     * AnimSequenceBase's virtual GetSequenceToPlay (__thiscall with an
     * index, 0 being the first) returns the BSAnimGroupSequence, which is a
     * NiControllerSequence with its TESAnimGroup at +0x74.
     */
    constexpr uintptr_t WeaponGetReloadAnim        = 0x0051E2A0;
    constexpr uintptr_t AnimDataSequences          = 0xDC;
    constexpr uintptr_t AnimDataReloadSpeed        = 0x118;
    constexpr uintptr_t AnimSequenceMapGetAt       = 0x0049C390;
    constexpr uintptr_t AnimSequenceToPlaySlot     = 0x10;
    constexpr uintptr_t AnimSequenceGroup          = 0x74;

    /**
     * NiControllerSequence: an array of tracks (InterpArrayItem, 0x10
     * bytes, its interpolator first) and one of the same size naming
     * their targets (IDTag, 0x14 bytes, the object's name first), and the
     * time range of its keys (GetInterpolatorAt 0x004937A0, GetIDTag
     * 0x007E4FC0, GetBeginKeyTime 0x00759450, GetEndKeyTime 0x00508100).
     * A NiInterpolator's virtual UpdateTransform (__thiscall with the time,
     * the target and a NiQuatTransform to fill, returns a bool in AL) gives
     * the track's translation, rotation (NiQuaternion w, x, y, z) and scale
     * at a time, each -FLT_MAX where the track has none
     * (NiBSplineCompTransformInterpolator 0x00A50F60 at +0x8C of its
     * vtable). IsTransformValueSupported says whether it has one.
     */
    constexpr uintptr_t SequenceArraySize          = 0x0C;
    constexpr uintptr_t SequenceInterpolators      = 0x14;
    constexpr uintptr_t SequenceInterpolatorSize   = 0x10;
    constexpr uintptr_t SequenceIdTags             = 0x18;
    constexpr uintptr_t SequenceIdTagSize          = 0x14;
    constexpr uintptr_t SequenceBeginKeyTime       = 0x2C;
    constexpr uintptr_t SequenceEndKeyTime         = 0x30;
    constexpr uintptr_t InterpolatorUpdateTransformSlot = 0x8C;
    constexpr uintptr_t InterpolatorIsTransformSlot     = 0xB8;

    /**
     * A track's IDTag also names the controller type (+0x08), a NiFixedString
     * like the object's name. A NiInterpolator's virtual UpdateFloat
     * (__thiscall with the time, the target and a float to fill, returns a
     * bool in AL) and IsFloatValueSupported read a float track, such as a
     * particle emitter's birth rate (NiInterpolator_vtbl, GOG database).
     */
    constexpr uintptr_t SequenceIdTagControllerType = 0x08;
    constexpr uintptr_t InterpolatorUpdateFloatSlot = 0x9C;
    constexpr uintptr_t InterpolatorIsFloatSlot     = 0xA8;

    /**
     * Particles, see findings/throw.md §7 (GOG database): a NiObjectNET's
     * controllers are a list (NiTimeController, next at +0x30, target at
     * +0x2C). A NiPSysEmitterCtlr (vtable below) emits the particles of its
     * target NiParticleSystem through its emitter (+0x3C) while an
     * animation drives its birth rate; without the animation it emits
     * nothing. NiPSysEmitterCtlr::EmitParticles (__thiscall, ret 0x18:
     * update time, current and last times, emission start and stop times,
     * birth rate per second) emits floor((min(current, stop) - start) *
     * rate) - floor((last - start) * rate) particles, aged from the start,
     * where the emitter object puts them. NiUpdateData starts with its time.
     */
    constexpr uintptr_t ObjectControllers           = 0x0C;
    constexpr uintptr_t ControllerTarget            = 0x2C;
    constexpr uintptr_t ControllerNext              = 0x30;
    constexpr uintptr_t PSysEmitterCtlrVtable       = 0x010BD594;
    constexpr uintptr_t PSysEmitterCtlrEmit         = 0x00C1C3C0;
    constexpr uint8_t   PSysEmitterCtlrEmitPrologue[] = { 0x51, 0xD9, 0xEE, 0x8B, 0xD1, 0xD9, 0x44, 0x24, 0x1C, 0x89, 0x14, 0x24 };

    /**
     * What becomes of the particles given (GOG database, read by the fuse
     * sparks' diagnostic). NiPSysData::AddParticle (0x00C24EA0) only counts
     * a particle as added; NiParticleSystem::Do_UpdateSystem (0x00C1B260),
     * run by the system's NiPSysUpdateCtlr (vtable below, Update
     * 0x00C271C0) while that controller is active (flags bit 0x8,
     * NiTimeController::DontDoUpdate), makes the added ones active, runs
     * the modifiers and stores its time. NiGeometry +0xB8 model data;
     * NiPSysData +0x08 particles at most (u16), +0x10 bound (radius at
     * +0x1C), +0x50 active (u16), +0x78 added (u16); NiParticleSystem
     * +0xD4 last update time (-FLT_MAX before the first); the controller's
     * flags are ControllerFlags (ControllerActive); NiPSysEmitter +0x48
     * particle lifespan. Do_UpdateSystem is the virtual UpdateSystem(float
     * time) at +0xF0 of NiParticleSystem, NiMeshParticleSystem and
     * BSStripParticleSystem (vtables 0x010BD450, 0x010BDD88, 0x010BE360);
     * the update controller calls it there with its scaled time. The
     * first-person model is updated with a NiUpdateData of time 0 without
     * its controllers (PlaceCamera, 0x00874DD4).
     */
    constexpr uintptr_t GeometryModelData           = 0xB8;
    constexpr uintptr_t PSysDataMaxParticles        = 0x08;
    constexpr uintptr_t PSysDataBoundRadius         = 0x1C;
    constexpr uintptr_t PSysDataActiveParticles     = 0x50;
    constexpr uintptr_t PSysDataAddedParticles      = 0x78;
    constexpr uintptr_t PSysLastUpdateTime          = 0xD4;
    constexpr uintptr_t PSysUpdateCtlrVtable        = 0x010BE6B4;
    constexpr uintptr_t PSysEmitterCtlrEmitter      = 0x3C;
    constexpr uintptr_t PSysEmitterLifeSpan         = 0x48;
    constexpr uintptr_t PSysUpdateSystemSlot        = 0xF0;

    /**
     * TESAnimGroup: the sounds of the animation's "Sound:" text keys,
     * read when it is loaded (GetSound 0x005F3710): their count and an
     * array of AnimGroupSound (0x10 bytes: the time, the volume as a byte
     * out of 255, the pitch, the TESSound).
     */
    constexpr uintptr_t AnimGroupSoundCount        = 0x34;
    constexpr uintptr_t AnimGroupSounds            = 0x38;
    constexpr uintptr_t AnimGroupSoundSize         = 0x10;
    constexpr uintptr_t AnimGroupSoundVolume       = 0x04;
    constexpr uintptr_t AnimGroupSoundPitch        = 0x08;
    constexpr uintptr_t AnimGroupSoundForm         = 0x0C;

    /**
     * How TESAnimGroup::PlaySounds (0x005F2B60) plays them for the player
     * in first person: BSAudio::GetSoundHandleByFilename (__thiscall on the
     * BSAudio singleton with the handle to fill, the sound's file name
     * (TESSound::GetFilename, __thiscall), the flags and the TESSound, ret
     * 0x10), then on the 12-byte BSSoundHandle SetPosition (three floats),
     * SetVolume, SetModFrequency (the pitch times the reload speed) and
     * Play (whether it loops). The flags are the TESSound's loop flag
     * (ConvertLoopFlag, __cdecl, of its flags at +0x48), 2D if its flag
     * 0x40 is set and 3D otherwise, one shot, first person, ignoring the
     * time scale and driven by an animation. Sounds played with no
     * reference (interface animations) are system sounds
     * (BSAnimGroupSequence::PlaySounds, 0x004EF7D1). Sounds driven by an
     * animation are paused while a menu is open
     * (InterfaceManager::PreIdleStuff calls BSAudio::PauseAnimationSounds,
     * 0x0070BBA0); the interface's own sounds are 2D, system and one shot
     * only (InterfaceManager::PlayMenuSound, 0x00717280). A looping sound
     * plays until BSSoundHandle::Stop (__thiscall, no argument, GOG
     * database) is called on a handle holding its ID.
     */
    constexpr uintptr_t AudioSingleton             = 0x011F6D98;
    constexpr uintptr_t AudioGetSoundByFilename    = 0x00AD7480;
    constexpr uintptr_t SoundGetFilename           = 0x00511840;
    constexpr uintptr_t SoundConvertLoopFlag       = 0x005E39B0;
    constexpr uintptr_t SoundHandleSetPosition     = 0x00AD8B60;
    constexpr uintptr_t SoundHandleSetVolume       = 0x00AD89E0;
    constexpr uintptr_t SoundHandleSetFrequency    = 0x00AD8A90;
    constexpr uintptr_t SoundHandlePlay            = 0x00AD8830;
    constexpr uintptr_t SoundHandleStop            = 0x00AD88F0;
    constexpr uintptr_t SoundFormFlags             = 0x48;
    constexpr uint32_t  SoundForm2D                = 0x40;
    constexpr uint32_t  SoundFlag2D                = 0x1;
    constexpr uint32_t  SoundFlag3D                = 0x2;
    constexpr uint32_t  SoundFlagSystem            = 0x20;
    constexpr uint32_t  SoundFlagOneShot           = 0x100;
    constexpr uint32_t  SoundFlagFirstPerson       = 0x40000;
    constexpr uint32_t  SoundFlagIgnoreTimescale   = 0x200000;
    constexpr uint32_t  SoundFlagAnimationDriven   = 0x40000000;

    /**
     * The V.A.T.S. highlight, reproduced for the weapon parts to work, see
     * findings/reload.md §9.1. DrawWorld_ImageSpace (0x00875FD0) calls
     * BSShaderManager::RenderImageSpaceEffects (__cdecl: renderer, target,
     * destination) at ImageSpaceEffectsCallSite. V.A.T.S. keeps its mask in
     * the NiPointer at VatsMaskTexture while it draws one.
     *
     * The mask, a rendered texture of type TEX_INTERFACE_VATS, is borrowed
     * from and returned to the texture manager (GetTextureManager, __cdecl;
     * BorrowRenderedTexture, __thiscall: renderer, type, three zeros;
     * ReturnRenderedTexture, __thiscall: the texture). It is drawn into
     * between StartOffscreen (__cdecl: clear flags, the texture's group from
     * GetGroup, __thiscall) and StopOffscreen (__cdecl), with the renderer's
     * background colour cleared (vtable slots, __thiscall with a NiColorA*),
     * colour writes off for the depth pass (SetColorWriteEnable, __cdecl:
     * mask, 1; RestoreColorWriteEnable, __cdecl: 1), both lighting shaders
     * writing alpha, and the V.A.T.S. manager's accumulator and the shader
     * manager in the constant alpha render mode (__thiscall and __cdecl).
     * The nodes go through the world scene graph's culling process
     * (WorldSceneGraph and SceneGraphCuller above; SetAccumulator,
     * SetCullMode, __thiscall) into AccumulateScene (__cdecl: camera, node,
     * culler) and RenderScene (__cdecl: camera, accumulator, 0).
     *
     * The manager (Interface::GetVATSEffect, __cdecl) sets the interface
     * image space effect up from the mask in PrepareHighlightEffect
     * (__thiscall: 0, mask, 0, 0; ret 0x10), from its own distortion,
     * pulse and burst; ShutdownEffect (__cdecl) turns it off again. The
     * effect's statics the fork sets afterwards are below, and the
     * settings' values are at SettingValue. The texture manager's calls are
     * the interface texture's, above.
     */
    constexpr uintptr_t ImageSpaceEffectsCallSite  = 0x00876136;
    constexpr uintptr_t RenderImageSpaceEffects    = 0x00B55AC0;
    constexpr uintptr_t VatsMaskTexture            = 0x011DEB38;
    constexpr uintptr_t GetVatsEffect              = 0x00705910;
    constexpr uintptr_t VatsPrepareHighlight       = 0x00801BB0;
    constexpr uintptr_t VatsShutdownEffect         = 0x008019D0;
    constexpr uintptr_t VatsEffectAccumulator      = 0x2CC;
    constexpr uint32_t  TextureInterfaceVats       = 0x2F;
    constexpr uintptr_t RenderedTextureGetGroup    = 0x00B6B260;
    constexpr uintptr_t StartOffscreen             = 0x00B6B8D0;
    constexpr uintptr_t StopOffscreen              = 0x00B6B790;
    constexpr uint32_t  ClearAll                   = 7;
    constexpr uintptr_t RendererGetBackgroundSlot  = 0xB4;
    constexpr uintptr_t RendererSetBackgroundSlot  = 0xAC;
    constexpr uintptr_t SetColorWriteEnable        = 0x00B98380;
    constexpr uintptr_t RestoreColorWriteEnable    = 0x004ECED0;
    constexpr uintptr_t Lighting30AlwaysWriteAlpha = 0x01200BF6;
    constexpr uintptr_t ShadowLightAlwaysWriteAlpha = 0x011FD86C;
    constexpr uintptr_t AccumulatorSetRenderMode   = 0x004A1040;
    constexpr uintptr_t ShaderManagerSetRenderMode = 0x00B4F450;
    constexpr uint32_t  RenderModeNormal           = 0;
    constexpr uint32_t  RenderModeConstAlpha       = 8;
    constexpr uintptr_t CullingSetAccumulator      = 0x004A0FD0;
    constexpr uintptr_t CullingSetCullMode         = 0x004FB0B0;
    constexpr uint32_t  CullModeNormal             = 0;
    constexpr uint32_t  CullModeAllPass            = 1;
    constexpr uintptr_t AccumulateScene            = 0x00B6BEE0;
    constexpr uintptr_t RenderScene                = 0x00B6C0D0;
    constexpr uintptr_t InterfaceBlurIntensity     = 0x011AE5EC;
    constexpr uintptr_t InterfaceBlurRadius        = 0x011AE5E8;
    constexpr uintptr_t InterfacePulse             = 0x011AE5FC;
    constexpr uintptr_t InterfaceDistortVertical   = 0x012007F0;
    constexpr uintptr_t InterfaceDistortHorizontal = 0x011AE5F8;
    constexpr uintptr_t InterfaceDistortPosition   = 0x012007F4;
    constexpr uintptr_t SettingBlurIntensityVats   = 0x011DB714;
    constexpr uintptr_t SettingBlurRadiusVats      = 0x011DB720;
    constexpr uintptr_t SettingTargetPulseRateVats = 0x011DB78C;

    /**
     * BSShaderManager::pCamera, which DrawWorld_ImageSpace sets to the world
     * camera just before the redirected call (mov [0x011F917C], eax at
     * 0x00876125, Ghidra) and RenderScene to the camera it draws with.
     */
    constexpr uintptr_t ShaderManagerCamera        = 0x011F917C;

    /**
     * First bytes of the functions above, compared between the GOG database
     * and the Steam executable (2026-10-18) and checked before any is
     * called: PrepareHighlightEffect, ShutdownEffect, AccumulateScene and
     * RenderScene (BorrowRenderedTexture's is above).
     */
    constexpr uint8_t VatsPrepareHighlightPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x89, 0x4D, 0xEC, 0x51, 0xD9, 0xEE };
    constexpr uint8_t VatsShutdownEffectPrologue[]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xFC, 0x6A, 0x00, 0x6A };
    constexpr uint8_t AccumulateScenePrologue[]      = { 0x6A, 0xFF, 0x68, 0x18, 0xAD, 0xF2, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00 };
    constexpr uint8_t RenderScenePrologue[]          = { 0x8B, 0x0D, 0x48, 0x47, 0x1F, 0x01, 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x85 };

    /**
     * Holsters, see findings/quick-menu.md §5. Actor::PlayPickUpSound
     * (__thiscall: item form, pick up, use; ret 0xC) plays the item's pick
     * up or put down sound, in 2D for the player. Actor::GetAnimAction
     * (__thiscall, no argument) returns the player's anim action, -1 when
     * none.
     */
    constexpr uintptr_t PlayPickUpSound            = 0x008ADED0;
    constexpr uint8_t   PlayPickUpSoundPrologue[]  = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x68, 0x51, 0xF1, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t ActorGetAnimAction         = 0x008A7570;
    constexpr uint8_t   ActorGetAnimActionPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC, 0x83, 0x78 };

    /**
     * Hotkeys, see findings/quick-menu.md §1-3. Eight slots, the second of
     * which is the ammunition swap, not a hotkey.
     *
     * - InventoryChanges::GetHotKeyItem (__thiscall, slot, ret 4) returns a
     *   new inventory entry, freed with the entry's deleting destructor
     *   (__thiscall, flags 1).
     * - An entry's name (__thiscall, no argument), icon path relative to
     *   Data\Textures (__thiscall, reference 0, ret 4), whether it is worn
     *   (__thiscall, 0, ret 4, bool) and its weapon's hotkey slot
     *   (__thiscall, -1 if none).
     * - The HUD's hotkey wheel (HUDMainMenu+0x1C8) and the inventory's
     *   (InventoryMenu+0xE8): Select (__thiscall, slot, ret 4) picks a slot,
     *   Use (__thiscall) equips or unequips its item as the gamepad's D-pad
     *   does, Visible (__thiscall, bool) tells whether the wheel is shown.
     */
    constexpr uint32_t  HotKeySlotCount            = 8;
    constexpr uint32_t  HotKeyAmmoSwapSlot         = 1;
    constexpr uintptr_t GetHotKeyItem              = 0x004BF4B0;
    constexpr uint8_t   GetHotKeyItemPrologue[]    = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x38, 0x12, 0xEF, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t InventoryEntryDelete       = 0x004459E0;
    constexpr uint8_t   InventoryEntryDeletePrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8, 0x01 };
    constexpr uintptr_t InventoryEntryName         = 0x004BE2D0;
    constexpr uint8_t   InventoryEntryNamePrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x6A, 0x00, 0x68 };
    constexpr uintptr_t InventoryEntryIcon         = 0x004BE200;
    constexpr uint8_t   InventoryEntryIconPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x89, 0x4D, 0xEC, 0xC7, 0x45, 0xFC };
    constexpr uintptr_t InventoryEntryWorn         = 0x004BDDD0;
    constexpr uint8_t   InventoryEntryWornPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0x8B, 0x45, 0xF4 };
    constexpr uintptr_t InventoryEntryHotKey       = 0x004BD7A0;
    constexpr uint8_t   InventoryEntryHotKeyPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0xC7, 0x45, 0xFC };
    constexpr uintptr_t HotKeyWheelSelect          = 0x00701E00;
    constexpr uint8_t   HotKeyWheelSelectPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0x08 };
    constexpr uintptr_t HotKeyWheelUse             = 0x00701E80;
    constexpr uint8_t   HotKeyWheelUsePrologue[]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38, 0x56, 0x89, 0x4D, 0xE4, 0xA1, 0x3C };
    constexpr uintptr_t HotKeyWheelVisible         = 0x00701740;
    constexpr uint8_t   HotKeyWheelVisiblePrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x68, 0xA3, 0x0F, 0x00, 0x00 };
    constexpr uintptr_t HudHotKeyWheel             = 0x1C8;

    /**
     * The Pip-Boy's inventory, see findings/quick-menu.md §3: whether a menu
     * is the top one (__cdecl, menu id), the InventoryMenu and its hotkey
     * wheel, shown while the gamepad's hotkey button (RB) is held.
     */
    constexpr uintptr_t IsTopMenu                  = 0x00702450;
    constexpr uint8_t   IsTopMenuPrologue[]        = { 0x55, 0x8B, 0xEC, 0x51, 0xE8, 0xB7, 0x4D, 0xDB, 0xFF, 0x85, 0xC0, 0x74 };
    constexpr uint32_t  InventoryMenuId            = 0x3EA;
    constexpr uintptr_t InventoryMenuInstance      = 0x011D9EA4;
    constexpr uintptr_t InventoryMenuHotKeyWheel   = 0xE8;

    /**
     * Ammunition swap, see findings/quick-menu.md §4. A weapon's ammunition
     * (BGSAmmoForm::pAmmo) is a list form (form type 0x55, BSSimpleList of
     * forms at +0x18: form, next) when it takes several types. A swap
     * gives the process a new clip entry (the game's allocator, __cdecl
     * size; the entry's constructor, __thiscall form and count, ret 8;
     * process vtable SetCurrentAmmo) and tells the weapon's hotkey which
     * ammunition it uses (__thiscall on the player, slot and ammunition,
     * ret 8). PlayerCharacter::AmmoSwapHelper (__thiscall, animate, only if
     * empty, ret 8) is the game's own swap to the next type held.
     */
    constexpr uint8_t   FormTypeList               = 0x55;
    constexpr uintptr_t WeaponAmmo                 = 0xA8;
    constexpr uintptr_t ListFormItems              = 0x18;
    constexpr uintptr_t ProcessSetCurrentAmmoSlot  = 0x168;
    constexpr uintptr_t GameAllocate               = 0x00401000;
    constexpr uint8_t   GameAllocatePrologue[]     = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0x14, 0x00, 0x00, 0x00 };
    constexpr uintptr_t InventoryEntryConstruct    = 0x004BC550;
    constexpr uint8_t   InventoryEntryConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x4B, 0xD6, 0xEE, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t SetAmmoForHotKey           = 0x004BFB70;
    constexpr uint8_t   SetAmmoForHotKeyPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8D, 0x45, 0x0C, 0x50, 0x8B };
    constexpr uintptr_t AmmoSwapHelper             = 0x009462C0;
    constexpr uint8_t   AmmoSwapHelperPrologue[]   = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x2B, 0x7F, 0xF1, 0x00, 0x64, 0xA1 };

    /**
     * The HUD's colour, the uHUDColor:Interface setting (value at +4,
     * 0xRRGGBBAA)
     */
    constexpr uintptr_t HudColorSetting            = 0x011D8AD4;

    /**
     * Physics grab with the hands, see findings/grab.md §8. The VR springs
     * are worked from the call to PlayerCharacter::HandlePhysicsGrab
     * (__thiscall, frame time, ret 4) in PlayerCharacter::Update, on the
     * main thread before the AI threads step Havok.
     */
    constexpr uintptr_t PhysicsGrabCallSite        = 0x0094363C;
    constexpr uintptr_t HandlePhysicsGrab          = 0x0095F6C0;

    /**
     * Havok ray picks as PlayerCharacter::CreateMouseSpring casts them:
     * bhkPickData (0xB0 bytes, 16-aligned, no destructor) built by its
     * constructor and setters (__thiscall; positions in game units, the
     * filter by value), cast by bhkWorld::PickObject (vtable slot, returns
     * a bool). GethkRigidBody returns the body hit or null; the hit fraction
     * is a float in the pick. The ray's filter is the item pick layer with
     * the player's collision group (MobileObject::GetCollisionFilter,
     * __thiscall, writes the filter to its argument), so it skips the
     * player and what the player holds. NI2HK scales game units to Havok
     * units, set when the game starts.
     */
    constexpr uintptr_t PickDataConstruct          = 0x004A3C20;
    constexpr uint8_t   PickDataConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8, 0x91 };
    constexpr uintptr_t PickDataSetFilter          = 0x004A3F70;
    constexpr uintptr_t PickDataSetFrom            = 0x004A3DA0;
    constexpr uintptr_t PickDataSetLength          = 0x007F68A0;
    constexpr uintptr_t PickDataGetRigidBody       = 0x00C66FD0;
    constexpr uintptr_t PickDataHitFraction        = 0x40;
    constexpr size_t    PickDataSize               = 0xB0;
    constexpr uintptr_t WorldPickObjectSlot        = 0xC8;
    constexpr uintptr_t GetCollisionFilter         = 0x00931ED0;
    constexpr uint32_t  LayerItemPick              = 0x24;
    constexpr uint32_t  FilterGroupShift           = 16;
    constexpr uint32_t  FilterKeptBits             = 0x7FFF;
    constexpr uintptr_t HavokScale                 = 0x011C582C;

    /**
     * From a body to its reference: bhkCollisionObject::Getbhk (__cdecl,
     * the hkpRigidBody), whose scene object is at NiCollisionObject+0x08,
     * then TESObjectREFR::FindReferenceFor3D (__cdecl). A reference's cell
     * (TESObjectREFR::GetParentCell, __thiscall) and the cell's bhkWorld
     * (TESObjectCELL::GetbhkWorld, __thiscall); a bhkRefObject's Havok
     * object is at +0x08. TESObjectREFR::GetObjectReference (__thiscall)
     * is compared with the two ash pile forms, which the game never grabs.
     */
    constexpr uintptr_t CollisionObjectForBody     = 0x004B5A20;
    constexpr uintptr_t CollisionSceneObject       = 0x08;
    constexpr uintptr_t FindReferenceFor3D         = 0x0056F930;
    constexpr uint8_t   FindReferenceFor3DPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC };
    constexpr uintptr_t RefGetParentCell           = 0x008D6F30;
    constexpr uintptr_t CellGetWorld               = 0x004543C0;
    constexpr uintptr_t RefObjectHavok             = 0x08;
    constexpr uintptr_t RefGetBaseForm             = 0x007AF430;

    /**
     * From a reference to its first body, as HandlePhysicsGrab finds the
     * body of the activation target: the reference's 3D (RefGet3DSlot),
     * FindFirstCollisionObject (__cdecl, the scene object, walks the
     * children) and bhkCollisionObject::GetRigidBody (__thiscall, the
     * bhkRigidBody, whose hkpRigidBody is at RefObjectHavok).
     */
    constexpr uintptr_t FindFirstCollisionObject   = 0x004B5260;
    constexpr uint8_t   FindFirstCollisionObjectPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00 };
    constexpr uintptr_t CollisionObjectGetBody     = 0x006FA820;
    constexpr uintptr_t AshPileForm1               = 0x011CA27C;
    constexpr uintptr_t AshPileForm2               = 0x011CA280;

    /**
     * Havok 7.1 bodies (hkpRigidBody): the world (null outside of one), the
     * collision filter, the motion type (4 keyframed, 5 fixed), the
     * inverse mass, the rotation's three columns, the position and the
     * centre of mass (the swept transform's m_centerOfMass1), all
     * hkVector4 in Havok units. An action's world is at +0x08 too.
     * hkpEntity::activate (__thiscall) wakes the body;
     * hkpWorld::updateCollisionFilterOnEntity (__thiscall, entity, update
     * mode 0, shape collections 1, ret 0xC) applies a new filter.
     */
    constexpr uintptr_t HavokObjectWorld           = 0x08;
    constexpr uintptr_t BodyFilter                 = 0x2C;
    constexpr uintptr_t BodyMotionType             = 0xE8;
    constexpr uint8_t   MotionKeyframed            = 4;
    constexpr uint8_t   MotionFixed                = 5;
    constexpr uintptr_t BodyInverseMass            = 0x1AC;
    constexpr uintptr_t BodyRotation               = 0xF0;
    constexpr uintptr_t BodyPosition               = 0x120;
    constexpr uintptr_t BodyCentreOfMass           = 0x140;
    constexpr uintptr_t EntityActivate             = 0x00C9C1D0;
    constexpr uintptr_t WorldUpdateFilter          = 0x00C946E0;
    constexpr uint8_t   WorldUpdateFilterPrologue[] = { 0x81, 0xEC, 0x30, 0x08, 0x00, 0x00, 0x53, 0x8B, 0xD9, 0x83, 0xBB, 0x94 };

    /**
     * A mouse spring as CreateMouseSpring makes one: the info's constructor
     * (__thiscall, 0x40 bytes, 16-aligned: +0x04 body, +0x10 point in the
     * body's frame and +0x20 target, Havok units, +0x30 damping, +0x34
     * elasticity, +0x38 maximum relative force, +0x3C object damping), the
     * game's NiNew (__cdecl size), bhkMouseSpringAction's constructor
     * (__thiscall, the info) and NiPointer assignment (__thiscall on the
     * pointer). bhkUnaryAction::Add (vtable, the bhkWorld) and
     * bhkAction::Remove (vtable, nothing when out of a world).
     * SetMousePosition (__thiscall, a point in game units) moves the
     * target. The Z grab's settings (values at SettingValue) give the
     * spring's constants and the heaviest body a hand lifts.
     */
    constexpr uintptr_t SpringInfoConstruct        = 0x004B4F40;
    constexpr uint8_t   SpringInfoConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8, 0x61 };
    constexpr size_t    SpringInfoSize             = 0x40;
    constexpr uintptr_t SpringInfoBody             = 0x04;
    constexpr uintptr_t SpringInfoLocalPoint       = 0x10;
    constexpr uintptr_t SpringInfoTarget           = 0x20;
    constexpr uintptr_t SpringInfoDamping          = 0x30;
    constexpr uintptr_t SpringInfoElasticity       = 0x34;
    constexpr uintptr_t SpringInfoMaxForce         = 0x38;
    constexpr uintptr_t SpringInfoObjectDamping    = 0x3C;
    constexpr uintptr_t NiNew                      = 0x00AA13E0;
    constexpr uint8_t   NiNewPrologue[]            = { 0x51, 0x8B, 0x44, 0x24, 0x08, 0x85, 0xC0, 0x75, 0x05, 0xB8, 0x01, 0x00 };
    constexpr size_t    SpringSize                 = 0x10;
    constexpr uintptr_t SpringConstruct            = 0x004B5040;
    constexpr uint8_t   SpringConstructPrologue[]  = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xF8, 0xAA, 0xF1, 0x00, 0x64, 0xA1 };
    constexpr uintptr_t SpringAssign               = 0x0066B0D0;

    /**
     * The hkpMouseSpringAction (a bhkMouseSpringAction's Havok object):
     * applyAction (0x00CBB1E0, vtable 0x010C8E94) clamps each step's
     * impulse to this float times the body's mass and the step's time.
     * The hkpWorld's gravity, an hkVector4 in Havok units per second
     * squared.
     */
    constexpr uintptr_t SpringActionMaxForce       = 0x48;
    constexpr uintptr_t WorldGravity               = 0x10;

    /**
     * Turning a held body (findings/grab.md §13): the action's body, its
     * hkArray of callbacks (+0x54 data, +0x58 size, +0x5C capacity with
     * bit 31 "storage not the array's to free"), called by applyAction
     * after the impulse as __cdecl (action, hkStepInfo, impulse); the
     * loop is checked so that a different build is not trusted. The step's
     * time at hkStepInfo +0x08. A body's linear and angular velocities
     * (hkpMotion +0xD0 and +0xE0) and the game's setters (__thiscall, an
     * hkVector4, ret 4: wake the body and call the motion's virtual
     * setter unless the velocity is unchanged).
     */
    constexpr uintptr_t SpringActionBody           = 0x18;
    constexpr uintptr_t SpringActionCallbacks      = 0x54;
    constexpr uint32_t  HavokArrayForeignStorage   = 0x80000000u;
    constexpr uintptr_t SpringCallbackLoop         = 0x00CBB4B0;
    constexpr uint8_t   SpringCallbackLoopBytes[]  = { 0x8B, 0x4E, 0x54, 0x8B, 0x04, 0xB9, 0x8D, 0x54, 0x24, 0x20, 0x52, 0x53, 0x56, 0xFF, 0xD0 };
    constexpr uintptr_t StepInfoDeltaTime          = 0x08;
    constexpr uintptr_t BodyLinearVelocity         = 0x1B0;
    constexpr uintptr_t BodyAngularVelocity        = 0x1C0;
    constexpr uintptr_t BodySetLinearVelocity      = 0x005616D0;
    constexpr uintptr_t BodySetAngularVelocity     = 0x00561800;
    constexpr uint8_t   BodySetVelocityPrologue[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xFC, 0x51, 0xD9, 0x05 };
    constexpr uintptr_t SpringAddSlot              = 0x9C;
    constexpr uintptr_t SpringRemoveSlot           = 0xA0;
    constexpr uintptr_t SpringSetTarget            = 0x008C5D90;
    constexpr uint8_t   SpringSetTargetPrologue[]  = { 0x53, 0x8B, 0xDC, 0x51, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04, 0x55, 0x8B };
    constexpr uintptr_t SpringDampingSetting       = 0x011D0358;
    constexpr uintptr_t SpringElasticitySetting    = 0x011CFEEC;
    constexpr uintptr_t SpringObjectDampingSetting = 0x011D08C8;
    constexpr uintptr_t SpringMaxForceSetting      = 0x011D0FFC;
    constexpr uintptr_t GrabMaxWeightSetting       = 0x011D1230;

    /**
     * What the game does around its own grab: the OnGrab and OnRelease
     * script blocks (Script::SetActionFlag, __cdecl with no form, the
     * reference's extra data at +0x44 and the flag),
     * TESObjectREFR::AddChange (vtable, 4 so the new position is saved),
     * ExtraDataList::NotifyGuardsAboutActivation (__thiscall, the
     * reference and the player) and RemoveExtra (__thiscall, type 0x7C).
     */
    constexpr uintptr_t SetActionFlag              = 0x005AC750;

    /**
     * Putting a held object in the inventory, see findings/grab.md §17:
     * TESObjectREFR::Activate (__thiscall: action reference, bool, bound
     * object, count; returns bool in AL, ret 0x10), the game's activation
     * with its scripts, ownership and pick-up of items. The reference's base
     * object is at +0x20; the form types that go into an inventory.
     */
    constexpr uintptr_t RefActivate                = 0x00573170;
    constexpr uint8_t   RefActivatePrologue[]      = {
      0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x38, 0x01, 0x00, 0x00, 0xA1, 0xBC, 0x16, 0x1C, 0x01, 0x33, 0xC5,
    };
    constexpr uintptr_t RefBaseForm                = 0x20;
    constexpr uint8_t   InventoryFormTypes[]       = {
      0x18, 0x19, 0x1A, 0x1D, 0x1E, 0x1F, 0x28, 0x29, 0x2E, 0x2F, 0x31, 0x67, 0x6C, 0x73, 0x74,
    };
    constexpr uintptr_t RefExtraData               = 0x44;
    constexpr uint32_t  ActionOnGrab               = 0x80000;
    constexpr uint32_t  ActionOnRelease            = 0x100000;
    constexpr uintptr_t RefAddChangeSlot           = 0x48;
    constexpr uint32_t  ChangeMoved                = 4;
    constexpr uintptr_t NotifyGuards               = 0x004213C0;
    constexpr uintptr_t RemoveExtra                = 0x00410140;
    constexpr uint8_t   ExtraGuarded               = 0x7C;

    /**
     * A keyframed sphere per hand that pushes objects, made as
     * Explosion::Init3D and PlayerCharacter::SetParentCell make bodies of
     * their own (findings/grab.md §18): bhkSphereShape's constructor
     * (__thiscall: radius, whether it is in game units; 0x14 bytes from
     * NiNew) and its material (MT_SKIN), bhkRigidBodyCinfo's constructor
     * (__thiscall, 0xE0 bytes, 16-aligned) with the filter at +0x00 and
     * again at +0x20 (the hkpRigidBodyCinfo), SetShape (__thiscall, the
     * bhkShape), the position (hkVector4, Havok units) and the motion
     * type, its destructor (__thiscall), and bhkRigidBody's constructor
     * (__thiscall, the info; 0x1C bytes from NiNew), whose vtable is
     * checked. bhkRigidBody's vtable: Add (the bhkWorld), Remove and
     * SetPosition (an hkVector4, Havok units). NiPointer assignment
     * (SpringAssign) is the same for every NiRefObject.
     */
    constexpr uintptr_t SphereShapeConstruct       = 0x0056E950;
    constexpr uint8_t   SphereShapeConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x58, 0x73, 0xEF, 0x00, 0x64, 0xA1 };
    constexpr size_t    SphereShapeSize            = 0x14;
    constexpr uintptr_t ShapeMaterial              = 0x10;
    constexpr uint32_t  MaterialSkin               = 7;
    constexpr uintptr_t BodyInfoConstruct          = 0x00C8F510;
    constexpr uint8_t   BodyInfoConstructPrologue[] = { 0x6A, 0xFF, 0x68, 0xD8, 0x24, 0xF3, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00 };
    constexpr uintptr_t BodyInfoDestruct           = 0x0056D730;
    constexpr uint8_t   BodyInfoDestructPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0xFC, 0xE8, 0x41 };
    constexpr uintptr_t BodyInfoSetShape           = 0x0056F110;
    constexpr uint8_t   BodyInfoSetShapePrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x4D, 0x08, 0xE8, 0x31 };
    constexpr size_t    BodyInfoSize               = 0xE0;
    constexpr uintptr_t BodyInfoFilter             = 0x00;
    constexpr uintptr_t BodyInfoHavokFilter        = 0x20;
    constexpr uintptr_t BodyInfoPosition           = 0x30;
    constexpr uintptr_t BodyInfoMotionType         = 0xD4;
    constexpr uintptr_t RigidBodyConstruct         = 0x0056D380;
    constexpr uint8_t   RigidBodyConstructPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x63, 0x71, 0xEF, 0x00, 0x64, 0xA1 };
    constexpr size_t    RigidBodySize              = 0x1C;
    constexpr uintptr_t RigidBodyVtable            = 0x010301B4;
    constexpr uintptr_t RigidBodyAddSlot           = 0x9C;
    constexpr uintptr_t RigidBodyRemoveSlot        = 0xA0;
    constexpr uintptr_t RigidBodySetPositionSlot   = 0xDC;

    /**
     * bhkCollisionFilter (findings/grab.md §18): layer L_DEBRIS_SMALL,
     * which meets static, clutter, props, debris and corpses but not
     * projectiles, triggers, water, the item and line of sight picks or
     * character controllers; and the filter's "no collision" bit, tested
     * before anything else.
     */
    constexpr uint32_t  LayerDebrisSmall           = 19;
    constexpr uint32_t  FilterNoCollision          = 0x4000;

    /**
     * PlayerCharacter::Update calls ForceGrenadeHold (__thiscall) on every
     * frame the player is not in control, where it does not reach
     * HandlePhysicsGrab.
     */
    constexpr uintptr_t NotInControlCallSite       = 0x0093F954;
    constexpr uintptr_t ForceGrenadeHold           = 0x009481D0;

    /**
     * bhkWorld::IsBatchingAdds (__thiscall, findings/grab.md §18.5): while
     * a cell loads, the world queues the bodies added to it and adds them
     * later, so a body just added is not in the world yet.
     */
    constexpr uintptr_t WorldIsBatchingAdds        = 0x00930C50;
    constexpr uint8_t   WorldIsBatchingAddsBytes[] = {
      0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC, 0x33, 0xC9, 0x3B, 0x48, 0x18 };

    /**
     * PlayerCharacter::SetParentCell(TESObjectCELL*) (__thiscall, virtual):
     * the player moves to another cell, possibly with another bhkWorld; the
     * game moves its camera sphere there (findings/grab.md §18.5).
     */
    constexpr uintptr_t PlayerSetParentCellSlot    = 0x0108AC64;
    constexpr uintptr_t PlayerSetParentCell        = 0x0094CAE0;

    /**
     * Message box on the main menu (findings/laa.md "In-game prompt"):
     * - StartMenu::DoIdle calls StartMenu::MainMenuIdle (__thiscall, returns
     *   false in the pause menu) on every frame at MainMenuIdleCallSite;
     * - StartMenu flags (StartMenuFlags): the initial load's pre-fade, the
     *   fade-in of the main menu and the "loading content" message;
     * - MessageMenuGlobal: MessageMenu*, null while no message box is open;
     * - ShowMessageBox (above): __cdecl (text, icon, title, callback, value
     *   of the first button, type, alpha, width, buttons..., null). A button
     *   click stores the first button's value plus the button's index, then
     *   calls the callback (__cdecl, no argument);
     * - GetMessageMenuResult: __cdecl, returns that value and clears it.
     */
    constexpr uintptr_t MainMenuIdleCallSite         = 0x007CFA8C;
    constexpr uintptr_t MainMenuIdle                 = 0x007D5830;
    constexpr uint32_t  StartMenuFadeIn              = 0x10;
    constexpr uint32_t  StartMenuContentMessage      = 0x400;
    constexpr uint32_t  StartMenuPreFade             = 0x80000;
    constexpr uintptr_t MessageMenuGlobal            = 0x011DA4F0;
    constexpr uint32_t  MessageBoxStartMenuType      = 0x12;
    constexpr uintptr_t GetMessageMenuResult         = 0x00703FA0;
    constexpr uint8_t   GetMessageMenuResultPrologue[] = {
      0x55, 0x8B, 0xEC, 0x51, 0xE8, 0x67, 0x32, 0xDB, 0xFF, 0x8A, 0x80, 0xE4, 0x00, 0x00, 0x00 };
    constexpr uint8_t   ShowMessageBoxPrologue[]     = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xE8, 0x85, 0x33, 0xDB, 0xFF };

    /**
     * A living actor's limb held by a hand (findings/grab.md §19): the
     * actor's bhkRagdollController (Actor +0xAC) and its hit reaction
     * ("ragdoll feedback"), which Actor::HitMe starts with
     * SetRagdollFeedbackActive(true) (__thiscall, one byte argument): the
     * bones of the race's feedback list become dynamic bodies driven
     * towards the animation, for fFeedbackTimeMS (3000) from the last call.
     * HitMe only starts it when the ragdoll's animation (+0x41) and the
     * feedback (+0x28D) are enabled, the actor is conscious and its
     * process's sit state is 0. +0x28E tells whether it runs.
     */
    constexpr uintptr_t ActorRagdollController       = 0xAC;
    constexpr uintptr_t RagdollAnimEnabled           = 0x41;
    constexpr uintptr_t RagdollFeedbackEnabled       = 0x28D;
    constexpr uintptr_t RagdollFeedbackActive        = 0x28E;
    constexpr uintptr_t RagdollSetFeedbackActive     = 0x00C7B6A0;
    constexpr uintptr_t RagdollSetFeedbackActiveCallSite = 0x0089CB0B;
    constexpr uintptr_t ActorIsUnconscious           = 0x00437BD0;
    constexpr uintptr_t ActorIsUnconsciousCallSite   = 0x0089CA82;
    /// BaseProcess::GetSitSleepState, virtual, __thiscall
    constexpr uintptr_t ProcessSitSleepStateSlot     = 0x4BC;
    constexpr uint8_t   MotionDynamic                = 1;

    /**
     * The reaction's work while it runs (findings/grab.md §19.4):
     * bhkRagdollController::DoRagdollAnim calls DoRagdollFeedback
     * (__thiscall, no argument; it slerps the drawn pose of the feedback
     * bones towards the ragdoll's and ends the reaction once its timer
     * expires) only when the INI setting bRagdollFeedback:RagdollAnim is
     * set (Setting object, bool at SettingValue), which Fallout_default.ini
     * turns off. The call right after it, UpdateDebugLines(2) (__thiscall,
     * one argument), always runs, before SetSGPoseLocalSpace.
     */
    constexpr uintptr_t RagdollDoFeedback            = 0x00C7C870;
    constexpr uintptr_t RagdollDoFeedbackCallSite    = 0x00C7D7CE;
    constexpr uintptr_t RagdollUpdateDebugLines      = 0x00C7CB50;
    constexpr uintptr_t RagdollFeedbackPoseCallSite  = 0x00C7D7D7;
    constexpr uintptr_t RagdollFeedbackSetting       = 0x01267C54;

    /**
     * NPC reactions to the player (findings/grab.md §19.3):
     * ProcessLists::CommentOnPlayerAction (0x00977CB0) makes the actor the
     * player aims at say the topic TESTopic::GetTopic(DT_TOPIC, 8) (__cdecl)
     * through Actor::StartGreetingPlayer(topic) (__thiscall), after
     * Actor::CanSpeak, IsInCombat, ShouldSkipFallOutBehavior(4) and
     * IsPlayerTeammate (__thiscall), and sets the shared comment timer
     * (ProcessLists +0x154) to fCommentOnPlayerActionsTimer. The call sites
     * are those of CommentOnPlayerAction.
     */
    constexpr uintptr_t TopicGetTopic                = 0x0061A2D0;
    constexpr uintptr_t TopicGetTopicCallSite        = 0x00978137;
    constexpr uintptr_t ActorStartGreetingPlayer     = 0x008BC3D0;
    constexpr uintptr_t ActorStartGreetingPlayerCallSite = 0x00978149;
    constexpr uintptr_t ActorCanSpeak                = 0x00884480;
    constexpr uintptr_t ActorCanSpeakCallSite        = 0x00977D4B;
    constexpr uintptr_t ActorIsInCombat              = 0x00493BB0;
    constexpr uintptr_t ActorIsInCombatCallSite      = 0x00977D73;
    constexpr uintptr_t ActorSkipsBehavior           = 0x008A78F0;
    constexpr uintptr_t ActorSkipsBehaviorCallSite   = 0x00977D84;
    constexpr uintptr_t ActorIsPlayerTeammate        = 0x00566950;
    constexpr uintptr_t ActorIsPlayerTeammateCallSite = 0x00977D93;
    constexpr uintptr_t ProcessListsSingleton        = 0x011E0E80;
    constexpr uintptr_t ProcessListsCommentTimer     = 0x154;
    constexpr uintptr_t CommentOnPlayerActionsTimerSetting = 0x011D0174;
    constexpr uint32_t  DialogueTypeTopic            = 0;
    constexpr uint32_t  TopicPlayerAiming            = 8;
    constexpr uint32_t  BehaviorReactToPlayerActions = 4;
    constexpr uint32_t  SitSleepSleeping             = 9;

    /**
     * Going into combat with the player as the script command StartCombat
     * does (Script::StartCombatFunction, 0x005C1750): Actor::IsGuard
     * (virtual) gives the aggressor flag, then BaseProcess::EnterCombat
     * (virtual, __thiscall, 13 stack arguments: actor, target, then the
     * flags 1, 1, 0, 0, 0, aggressor, 0, 1, 0, 1, 0). Aggression and
     * Confidence are actor values 0 and 1 (ACTOR_AGGRESSION,
     * ACTOR_CONFIDENCE: Aggressive 1, Foolhardy 4).
     */
    constexpr uintptr_t ActorIsGuardSlot             = 0x304;
    constexpr uintptr_t ProcessEnterCombatSlot       = 0x33C;
    constexpr uint32_t  AvAggression                 = 0;
    constexpr uint32_t  AvConfidence                 = 1;

    /**
     * Melee hits on objects (findings/melee.md §14): Actor::ObjectHit(bool
     * power) (__thiscall, ret 4), which MeleeAttack calls for the player
     * when no actor is in reach, casts a shape from Actor::GetEyeVector
     * (call at ObjectHitEyeCallSite: __thiscall, position, direction,
     * third person, ret 0xC) and pushes the body hit along the weapon's
     * motion: from BaseProcess::GetWeaponLastPos (virtual, no argument)
     * to Actor::GetWeaponPosition(NiPoint3* out, bool) (call at
     * ObjectHitWeaponCallSite, __thiscall, ret 8, returns out), both in the
     * frame of the actor's root node. Then impact sound, decal, particles
     * and the OnHit / OnHitWith script blocks.
     */
    constexpr uintptr_t ActorObjectHit               = 0x008AE660;
    constexpr uint8_t   ActorObjectHitPrologue[]     = {
      0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04, 0x55, 0x8B, 0x6B, 0x04 };
    constexpr uintptr_t ActorGetEyeVector            = 0x008AFF80;
    constexpr uintptr_t ObjectHitEyeCallSite         = 0x008AE7E2;
    constexpr uintptr_t ActorGetWeaponPosition       = 0x008A5340;
    constexpr uintptr_t ObjectHitWeaponCallSite      = 0x008AEBCD;
    constexpr uintptr_t ProcessWeaponLastPosSlot     = 0x460;

  }

}
