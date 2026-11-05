#include <algorithm>
#include <cfloat>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_ammo.h"
#include "vr_game_memory.h"
#include "vr_game_sound.h"
#include "vr_manual_reload.h"
#include "vr_melee.h"
#include "vr_sound_keys.h"

namespace dxvk {

  namespace {

    // PlayerCharacter::ReloadWeaponNV, __thiscall with four stack
    // arguments, returns a bool in AL
    using ReloadWeaponFn = uint32_t (__fastcall*)(uint8_t* player, void* unused,
      uint8_t* weapon, uint32_t animation, uint32_t useMod, uint32_t fromInventory);

    // Actor::GetAnimGroup, __thiscall with four stack arguments, returns
    // the 16-bit group in AX
    using GetAnimGroupFn = uint32_t (__fastcall*)(uint8_t* actor, void* unused,
      uint32_t group, void* weaponEntry, uint32_t checkHandToHand, void* animation);

    // Process virtual function, __thiscall without arguments, returns the
    // clip's inventory entry
    using GetCurrentAmmoFn = uint8_t* (__fastcall*)(uint8_t* process, void* unused);

    // Process virtual function, __thiscall without arguments, returns the
    // equipped weapon's inventory entry
    using GetWeaponInfoFn = uint8_t* (__fastcall*)(uint8_t* process, void* unused);

    // InventoryChanges::GetInventoryChanges, __cdecl with the reference
    using GetInventoryChangesFn = uint8_t* (__cdecl*)(uint8_t* reference);

    // InventoryChanges::GetObjectCount, __thiscall with the item
    using InventoryObjectCountFn = int32_t (__fastcall*)(uint8_t* changes, void* unused, const uint8_t* item);

    // ItemChange::HasModEffectActive, __thiscall with the effect, returns a
    // bool in AL
    using HasModEffectFn = uint32_t (__fastcall*)(uint8_t* entry, void* unused, uint32_t effect);

    // Interface::ShowMessage, __cdecl with the text, the face, the image,
    // the sound, the time and whether it is shown at once
    using ShowHudMessageFn = uint32_t (__cdecl*)(const char* text, uint32_t emotion, const char* image,
      const char* sound, float time, uint32_t instant);

    // Frames during which the clip of a reload is watched: the equipment
    // queue fills it a frame or so after the reload
    constexpr uint32_t ClipWatchFrames = 4;

    // Clip count logged when the player has no clip
    constexpr int32_t NoClip = -1;

    // Animation group StartAttack does not take for the jam's, so that it
    // plays no jam animation
    constexpr uint32_t NoAnimGroup = 0xFFFF;

    // Part of its travel the rack part of a jammed weapon rests back by
    constexpr float JamRest = 0.3f;

    // How long the message that no rounds are left is shown, in seconds,
    // as V.A.T.S. shows it; none is queued again meanwhile
    constexpr float NoAmmoMessageTime = 2.0f;

    // The sound the game plays when a lock or a terminal needs more skill
    // than the player has (LockPickMenu::Create, BGSTerminal::Activate)
    constexpr const char* NoAmmoSound = "UIPopUpMessageGeneral";

    VrManualReload* g_manualReload = nullptr;

    // What the player's reload entry held: xNVSE's hook when it is installed,
    // which then still sees every reload
    uintptr_t g_previousReload = VrGame::PlayerReloadWeapon;

    // StartAttack's clip reads were redirected: jams can be held
    bool g_holdsJams = false;

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    uint8_t* currentAmmo(uint8_t* actor) {
      uint8_t* process = actor ? readField<uint8_t*>(actor, VrGame::ActorProcess) : nullptr;

      if (!process)
        return nullptr;

      auto vtable  = readField<const uint8_t*>(process, 0);
      auto getAmmo = reinterpret_cast<GetCurrentAmmoFn>(readField<uintptr_t>(vtable, VrGame::ProcessCurrentAmmoSlot));
      return getAmmo(process, nullptr);
    }

    uint8_t* weaponEntry(uint8_t* actor) {
      uint8_t* process = actor ? readField<uint8_t*>(actor, VrGame::ActorProcess) : nullptr;

      if (!process)
        return nullptr;

      auto vtable = readField<const uint8_t*>(process, 0);
      auto getWeaponInfo = reinterpret_cast<GetWeaponInfoFn>(readField<uintptr_t>(vtable, VrGame::ProcessWeaponInfoSlot));
      return getWeaponInfo(process, nullptr);
    }

    int32_t clipCount(uint8_t* actor) {
      uint8_t* entry = currentAmmo(actor);
      return entry ? readField<int32_t>(entry, VrGame::InventoryEntryCount) : NoClip;
    }

    /**
     * \brief Whether the inventory holds rounds of the clip's ammunition
     *
     * The rounds in the clip are counted in the inventory too, so a weapon
     * loaded round by round needs more than its clip. Without a clip
     * entry, the game's reload decides.
     *
     * \param [in] beyondClip Count only the rounds that are not in the clip
     */
    bool holdsRounds(uint8_t* actor, bool beyondClip) {
      uint8_t* entry = currentAmmo(actor);
      auto ammo = entry ? readField<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;

      uint8_t* changes = ammo ? reinterpret_cast<GetInventoryChangesFn>(VrGame::GetInventoryChanges)(actor) : nullptr;

      if (!changes)
        return true;

      int32_t held = reinterpret_cast<InventoryObjectCountFn>(VrGame::InventoryObjectCount)(changes, nullptr, ammo);
      return held > (beyondClip ? readField<int32_t>(entry, VrGame::InventoryEntryCount) : 0);
    }

    /**
     * \brief The ammunition a hand takes behind the back
     *
     * The clip's type while rounds of it are left, otherwise the first
     * type of the weapon's list with rounds left, see findings/quick-menu.md
     * §6, decision 3.
     *
     * \param [in] beyondClip Count only the rounds that are not in the clip
     * \returns The ammunition, or \c nullptr if no type has rounds left
     */
    const uint8_t* ammoToTake(uint8_t* actor, bool beyondClip) {
      uint8_t* entry = currentAmmo(actor);
      auto current = entry ? readField<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;
      int32_t clip = entry ? readField<int32_t>(entry, VrGame::InventoryEntryCount) : 0;

      if (!VrGameAmmo::available())
        return holdsRounds(actor, beyondClip) ? current : nullptr;

      std::vector<VrAmmoType> types = VrGameAmmo::types();

      for (const VrAmmoType& type : types) {
        if (type.form == current && type.held > (beyondClip ? clip : 0))
          return current;
      }

      for (const VrAmmoType& type : types) {
        if (type.form != current && type.held > 0)
          return type.form;
      }

      // A weapon whose ammunition the list does not give
      return types.empty() && holdsRounds(actor, beyondClip) ? current : nullptr;
    }

    const char* callerName(uintptr_t returnAddress) {
      switch (returnAddress) {
        case VrGame::ReloadReturnControl:   return "reload control";
        case VrGame::ReloadReturnEmptyClip: return "empty clip";
        default:                            return "game";
      }
    }

    /**
     * \brief How fast the player's reload animations play
     */
    float reloadSpeed(uint8_t* actor) {
      auto animation = actor ? readField<const uint8_t*>(actor, VrGame::PlayerFirstPersonAnimData) : nullptr;
      return animation ? readField<float>(animation, VrGame::AnimDataReloadSpeed) : 1.0f;
    }


    bool inVats() {
      return *reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState) != 0;
    }

    const char* weaponName(const uint8_t* weapon) {
      const char* name = weapon ? readField<const char*>(weapon, VrGame::WeaponFullName) : nullptr;
      return name ? name : "";
    }

  }


  VrManualReload::~VrManualReload() {
    // The table entry and the call stay in place and fall through to the
    // game's functions
    g_manualReload = nullptr;
  }


  std::unique_ptr<VrManualReload> VrManualReload::install() {
    if (g_manualReload)
      return nullptr;

    if (!VrGameMemory::chainVirtual(VrGame::PlayerReloadWeaponSlot, VrGame::PlayerReloadWeapon,
        reinterpret_cast<const void*>(&VrManualReload::reloadHook), &g_previousReload))
      return nullptr;

    if (!VrGameMemory::redirectCall(VrGame::JamAnimGroupCallSite, VrGame::ActorGetAnimGroup,
        reinterpret_cast<const void*>(&VrManualReload::jamGroupHook)))
      Logger::warn("VR: The jam animation call was not found, the player's jams are not logged");

    // Both reads of the clip, or the attack would start after the first
    g_holdsJams = VrGameMemory::redirectCall(VrGame::StartAttackClipCallSite, VrGame::InventoryEntryGetCount,
        reinterpret_cast<const void*>(&VrManualReload::clipCountHook))
      && VrGameMemory::redirectCall(VrGame::StartAttackClipEmptyCallSite, VrGame::InventoryEntryGetCount,
        reinterpret_cast<const void*>(&VrManualReload::clipCountHook));

    if (!g_holdsJams)
      Logger::warn("VR: The clip reads of the attack were not found, jams are left to the game");

    std::unique_ptr<VrManualReload> reload(new VrManualReload());
    reload->m_showsNoAmmo = VrGameMemory::matches(VrGame::NoAmmoMessageReadSite,
        VrGame::NoAmmoMessageRead, sizeof(VrGame::NoAmmoMessageRead))
      && VrGameMemory::matches(VrGame::ShowHudMessage,
        VrGame::ShowHudMessagePrologue, sizeof(VrGame::ShowHudMessagePrologue));

    if (!reload->m_showsNoAmmo)
      Logger::warn("VR: The game's no ammo message was not found, running out of rounds is only felt");

    g_manualReload = reload.get();
    return reload;
  }


  void VrManualReload::update() {
    int32_t clip = clipCount(player());

    std::lock_guard<dxvk::mutex> lock(m_mutex);

    if (!m_watchFrames)
      return;

    m_watchFrames -= 1;

    if (clip != m_watchedClip) {
      Logger::info(str::format("VR: Clip after the reload: ", clip, ", ",
        ClipWatchFrames - 1 - m_watchFrames, " frame(s) later"));
      m_watchedClip = clip;
    }
  }


  void VrManualReload::setEnabled(bool enabled) {
    if (enabled != m_enabled) {
      Logger::info(enabled
        ? "VR: Weapons with a detachable magazine, those loaded round by round, break actions, revolvers and launchers are reloaded with the hands"
        : "VR: The weapons are reloaded as in the game");
    }

    m_enabled = enabled;
  }


  VrReloadEvents VrManualReload::updateHands(const VrManualReloadInput& input) {
    uint8_t* character = player();
    uint8_t* entry     = weaponEntry(character);
    uint8_t* weapon    = entry ? readField<uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;
    int32_t  clip      = clipCount(character);

    if (weapon != m_lastWeapon) {
      m_magazine.reset();
      m_rounds.reset();
      m_rack.reset();
      m_action.reset();
      m_flick.reset();
      m_lock.reset();
      m_pendingFill = false;
      m_fillFrames  = 0;
      m_jammed      = false;
      m_unchambered = false;
      m_refusedClip = -1;
      m_heldAmmo    = nullptr;
      m_lastWeapon  = weapon;
      m_lastClip    = clip;
    }

    // A weapon is reloaded by hand once its magazine, or the round its
    // reload carries in, was found in the hand, and until another weapon
    // is equipped, even in V.A.T.S. or while it is not drawn in the hand
    if (weapon && input.well)
      m_wellWeapon = weapon;

    bool handled = m_enabled && weapon && weapon == m_wellWeapon;
    bool looping = handled && readField<uint8_t>(weapon, VrGame::WeaponLoopingReload);
    bool hinged  = handled && !looping && input.barrel;
    bool swung   = handled && input.roll;
    bool opening = hinged || swung;

    m_handWeapon = handled ? weapon : nullptr;

    // A jam is cleared by racking, or by taking the magazine out: a weapon
    // loaded round by round with nothing to rack keeps the game's jam
    m_holdsJam = handled && (input.rack || !looping) && !input.fired;

    // How fast the right hand turns comes from its last frame
    VrGameCameraPose lastRight = m_lastRight;
    bool hasLastRight = m_hasLastRight;
    m_hasLastRight = input.rightHand != nullptr;

    if (input.rightHand)
      m_lastRight = *input.rightHand;

    m_loadsRounds = looping;
    m_hasRackPart = handled && input.rack;
    m_hasLockPart = handled && input.lock;
    m_kind        = !handled ? VrReloadKind::None
      : looping ? VrReloadKind::Rounds
      : hinged  ? VrReloadKind::BreakAction
      : swung   ? VrReloadKind::Cylinder
      : VrReloadKind::Magazine;

    if (!handled) {
      m_magazine.reset();
      m_rounds.reset();
      m_rack.reset();
      m_action.reset();
      m_flick.reset();
      m_lock.reset();
      m_actionOpen  = false;
      m_pendingFill = false;
      m_lockedBack  = false;
      m_jammed      = false;
      m_unchambered = false;
      m_jamStarted  = false;
      m_rackRest    = 0.0f;
      m_lastClip    = clip;
      return VrReloadEvents();
    }

    // The game filled the clip itself: equipping, V.A.T.S., an ammunition
    // swap, a pickup... The magazine is back in the weapon, loaded. The
    // clip of a weapon loaded round by round grows with each round, and a
    // round in the hand stays there.
    if (!looping && clip > m_lastClip && (m_magazine.state() != VrMagazineState::InWeapon || m_pendingFill)) {
      Logger::info(str::format("VR: The game filled the clip of '", weaponName(weapon),
        "', its magazine is back in the weapon"));
      m_magazine.reset();
      m_action.reset();
      m_flick.reset();
      m_lock.reset();
      m_pendingFill = false;
      clearJam(weapon, "the game reloaded it");
    }

    // A launcher's round left with the shot: its tube is empty
    if (input.fired && clip == 0 && m_magazine.state() == VrMagazineState::InWeapon) {
      m_magazine.empty();
      Logger::info(str::format("VR: The tube of '", weaponName(weapon), "' is empty"));
    }

    VrMagazineInput magazine;
    magazine.active = input.head && input.rightHand && input.leftHand && input.well && !input.leftOnWeapon;
    magazine.grip   = input.grip;
    magazine.closed = opening && !actionOpen();

    // The rack part is taken by a hand that holds no magazine nor rounds
    bool handEmpty = looping ? m_rounds.state() == VrRoundState::Empty
      : (m_magazine.state() == VrMagazineState::InWeapon || m_magazine.state() == VrMagazineState::Out);

    VrRackInput rack;
    rack.active = magazine.active && input.rack && handEmpty;
    rack.grip       = input.grip;
    rack.rest       = m_rackRest;

    if (magazine.active) {
      const VrGameCameraPose& right = *input.rightHand;

      magazine.hand       = input.leftHand->position;
      magazine.held       = vrPointFromHand(*input.leftHand, input.held);
      magazine.well       = vrPointFromHand(right, *input.well);
      magazine.axis       = right.forward * input.axis.x + right.up * input.axis.y + right.right * input.axis.z;
      magazine.head       = *input.head;
      magazine.roundsHeld = m_magazine.state() != VrMagazineState::Out || ammoToTake(character, false);
    }

    if (rack.active) {
      const VrGameCameraPose& right = *input.rightHand;

      rack.hand   = input.leftHand->position;
      rack.rack   = vrPointFromHand(right, *input.rack);
      rack.axis   = right.forward * input.rackAxis.x + right.up * input.rackAxis.y + right.right * input.rackAxis.z;
      rack.travel = input.rackTravel;
    }

    // The lock over the magazine is turned by a hand that holds nothing,
    // with a magazine in the weapon: without one it stays open
    VrLockInput lock;
    lock.active = magazine.active && input.lock && !looping;
    lock.grip   = input.grip;

    if (lock.active) {
      const VrGameCameraPose& right = *input.rightHand;

      lock.hand  = input.leftHand->position;
      lock.pivot = vrPointFromHand(right, *input.lock);
      lock.axis  = right.forward * input.lockAxis.x + right.up * input.lockAxis.y + right.right * input.lockAxis.z;
      lock.arm   = right.forward * input.lockArm.x + right.up * input.lockArm.y + right.right * input.lockArm.z;
      lock.angle = input.lockAngle;
    }

    bool locked = input.lock && !m_lock.open();

    // A press takes the nearest of the magazine, the rack part and the lock
    float toEject = magazine.active && !looping && !locked && m_magazine.state() == VrMagazineState::InWeapon
      ? m_magazine.ejectDistance(magazine) : FLT_MAX;
    float toRack  = rack.active ? m_rack.grabDistance(rack) : FLT_MAX;
    float toLock  = lock.active ? m_lock.grabDistance(lock) : FLT_MAX;

    // Nothing is taken out of a break action or a revolver: opening it
    // empties it. Nothing goes out or in under a shut lock.
    bool rackNearer  = toRack < toEject;
    magazine.blocked = opening || input.fired || rackNearer || locked || toLock < toEject;
    magazine.closed  = magazine.closed || locked;
    rack.blocked     = !(opening || input.fired || rackNearer);
    lock.blocked     = m_magazine.state() != VrMagazineState::InWeapon || toEject <= toLock;

    VrBreakInput action;
    action.active  = magazine.active && hinged;
    action.grip    = input.grip;
    action.blocked = m_magazine.state() == VrMagazineState::InHand || m_magazine.state() == VrMagazineState::Seating;

    if (action.active) {
      const VrGameCameraPose& right = *input.rightHand;

      action.hand   = input.leftHand->position;
      action.breech = magazine.well;
      action.barrel = right.forward * input.barrel->x + right.up * input.barrel->y + right.right * input.barrel->z;
    }

    VrReloadEvents events;
    events.jammed = m_jamStarted.exchange(false);

    // The trigger was pulled and nothing fired: the dry click
    if (events.jammed && input.rightHand) {
      VrAnimationSound click;
      click.sound = readField<const uint8_t*>(weapon, VrGame::WeaponAttackFailSound);
      vrPlayReloadSound(click, rack.active ? rack.rack : input.rightHand->position);
    }

    // A revolver is flicked open about its barrel
    VrFlickInput flick;
    flick.active  = magazine.active && swung;
    flick.blocked = m_magazine.state() == VrMagazineState::Seating || m_rounds.state() == VrRoundState::Loading;
    flick.dt      = input.frameTime;

    if (flick.active && hasLastRight) {
      const VrGameCameraPose& right = *input.rightHand;
      VrVector3 axis = right.forward * input.roll->x + right.up * input.roll->y + right.right * input.roll->z;
      flick.roll = vrTurnRate(lastRight, right, axis, input.frameTime);
    }

    events.rack   = m_rack.update(rack);
    events.action = m_action.update(action);

    VrBreakEvent flicked = m_flick.update(flick);

    if (flicked != VrBreakEvent::None)
      events.action = flicked;

    // What opens: a revolver's cylinder or loading gate, or the action
    const char* opened = swung ? (looping ? "loading gate" : "cylinder") : "action";
    VrVector3 breech = action.active ? action.breech : magazine.well;

    switch (events.action) {
      case VrBreakEvent::Opened:
        if (looping) {
          if (input.animation)
            vrPlayReloadSound(input.animation->hinge.openSound, breech);

          Logger::info(str::format("VR: The loading gate of '", weaponName(weapon), "' is opened, clip ",
            clipCount(character)));
          break;
        }

        openAction(character, weapon, input.animation ? &input.animation->hinge.openSound : nullptr, breech, swung);
        break;

      case VrBreakEvent::Closed:
        if (input.animation)
          vrPlayReloadSound(input.animation->hinge.closeSound, breech);

        Logger::info(str::format("VR: The ", opened, " of '", weaponName(weapon), "' is closed, clip ", clipCount(character)));
        break;

      case VrBreakEvent::None:
        break;
    }

    events.lock = m_lock.update(lock);

    switch (events.lock) {
      case VrLockEvent::Opened:
      case VrLockEvent::Shut: {
        bool open = events.lock == VrLockEvent::Opened;

        if (input.animation)
          vrPlayReloadSound(open ? input.animation->lock.openSound : input.animation->lock.closeSound, lock.pivot);

        Logger::info(str::format("VR: The lock over the magazine of '", weaponName(weapon),
          open ? "' is open" : "' is shut", ", clip ", clipCount(character)));
      } break;

      case VrLockEvent::Grabbed:
      case VrLockEvent::None:
        break;
    }

    m_actionOpen = actionOpen() || (input.lock && m_lock.open());

    // The hand on the lock does nothing with magazines
    if (m_lock.held() || events.lock != VrLockEvent::None)
      magazine.active = false;

    // The hand on the rack part does nothing with magazines
    if (m_rack.held() || events.rack != VrRackEvent::None)
      magazine.active = false;

    // A weapon loaded round by round takes nothing out: its rounds go in
    // from a handful
    if (looping)
      events.round = updateRounds(input, character, weapon, magazine.active, magazine.closed);
    else
      events.magazine = m_magazine.update(magazine);

    // What the hand loads the weapon with
    const char* load = hinged ? "shells" : (swung ? "speed loader" : (input.fired ? "round" : "magazine"));

    switch (events.magazine) {
      case VrMagazineEvent::Ejected:
        // The magazine taken out keeps the type it was loaded with
        m_heldAmmo = nullptr;
        ejectMagazine(character, weapon, input.animation ? &input.animation->out : nullptr, magazine.well,
          input.rack && clip > 0);

        // Without a rack part, changing the magazine clears a jam
        if (!input.rack)
          clearJam(weapon, "its magazine was taken out");
        break;

      case VrMagazineEvent::Inserted:
        loadHeldAmmo();
        insertMagazine(character, weapon, input.animation ? &input.animation->in : nullptr, magazine.well,
          !input.rack || clipCount(character) > 0);
        m_heldAmmo = nullptr;
        break;

      case VrMagazineEvent::Cleared:
        Logger::info(str::format("VR: The ", load, " of '", weaponName(weapon),
          hinged ? "' are out of the breech" : (swung ? "' is out of the cylinder"
            : (input.fired ? "' is out of the tube" : "' is out of its well"))));
        break;

      case VrMagazineEvent::Engaged:
        Logger::info(str::format(hinged ? "VR: Shells enter the breech of '"
          : (swung ? "VR: A speed loader enters the cylinder of '"
            : (input.fired ? "VR: A round enters the tube of '" : "VR: A magazine enters the well of '")),
          weaponName(weapon), "'"));
        break;

      case VrMagazineEvent::Taken:
        m_heldAmmo = ammoToTake(character, false);
        Logger::info(str::format(hinged ? "VR: New shells for '" : str::format("VR: A new ", load, " for '"),
          weaponName(weapon), hinged ? "' are taken behind the back" : "' is taken behind the back"));
        break;

      case VrMagazineEvent::NoRounds:
        Logger::info(str::format("VR: No rounds left for '", weaponName(weapon), "'",
          hinged ? ", no shells are taken" : str::format(", no ", load, " is taken")));
        break;

      case VrMagazineEvent::Dropped:
        m_heldAmmo = nullptr;
        Logger::info(str::format("VR: The ", load, " of '", weaponName(weapon), hinged ? "' are let go of" : "' is let go of"));
        break;

      case VrMagazineEvent::None:
        break;
    }

    switch (events.rack) {
      case VrRackEvent::Grabbed:
        Logger::info(str::format("VR: The rack part of '", weaponName(weapon), "' is taken",
          m_lockedBack ? ", locked back" : (m_jammed ? ", jammed" : "")));
        break;

      case VrRackEvent::Released:
        Logger::info(str::format("VR: The rack part of '", weaponName(weapon), "' is let go of before it was all the way back"));
        break;

      case VrRackEvent::Racked: {
        const VrAnimationSound* sound = input.animation && input.animation->hasRack ? &input.animation->rack.sound : nullptr;

        if (looping)
          cycleAction(character, weapon, sound, rack.rack);
        else
          rackWeapon(character, weapon, sound, rack.rack);

        clearJam(weapon, "it was racked");
      } break;

      case VrRackEvent::None:
        break;
    }

    if (m_fillFrames)
      m_fillFrames -= 1;

    // The shot that empties a lever-action weapon leaves nothing
    // chambered: the next round must be cycled in by hand
    int32_t lastClip = m_lastClip;
    m_lastClip = clipCount(character);

    if (looping && input.rack && lastClip > 0 && m_lastClip == 0 && !m_unchambered.exchange(true)) {
      Logger::info(str::format("VR: '", weaponName(weapon), "' fired its last round, its action must be worked "
        "once rounds are loaded"));
    }

    // Only a weapon with a magazine is drawn locked back
    m_lockedBack = input.rack && !looping && m_lastClip == 0 && !m_fillFrames;

    if (!input.rack)
      m_rackRest = 0.0f;
    else if (m_lockedBack)
      m_rackRest = input.rackTravel;
    else
      m_rackRest = m_jammed ? JamRest * input.rackTravel : 0.0f;

    m_noAmmoTime = std::max(m_noAmmoTime - input.frameTime, 0.0f);

    if (events.magazine == VrMagazineEvent::NoRounds || events.round == VrRoundEvent::NoRounds)
      showNoAmmo();

    return events;
  }


  bool VrManualReload::holdsAmmo() const {
    VrMagazineState magazine = m_magazine.state();

    // Rounds going into the port are no longer changed
    bool held = m_loadsRounds
      ? m_rounds.state() == VrRoundState::Holding
      : magazine == VrMagazineState::InHand || magazine == VrMagazineState::Seating;

    // A magazine taken out of the weapon is not one taken behind the back
    return held && m_heldAmmo;
  }


  bool VrManualReload::cycleAmmo() {
    if (!holdsAmmo() || !VrGameAmmo::available())
      return false;

    std::vector<VrAmmoType> types = VrGameAmmo::types();
    size_t count = types.size();
    size_t index = 0;

    while (index < count && types[index].form != m_heldAmmo)
      index += 1;

    // The next type with rounds left, after the one held
    for (size_t step = 1; step < count; step++) {
      const VrAmmoType& next = types[(index + step) % count];

      if (next.held > 0 && next.form != m_heldAmmo) {
        m_heldAmmo = next.form;
        Logger::info(str::format("VR: The left hand now holds '", next.name, "'"));
        return true;
      }
    }

    return false;
  }


  void VrManualReload::loadHeldAmmo() {
    if (!m_heldAmmo || !VrGameAmmo::available() || m_heldAmmo == VrGameAmmo::current())
      return;

    VrGameAmmo::select(m_heldAmmo);
  }


  void VrManualReload::showNoAmmo() {
    if (m_noAmmoTime > 0.0f)
      return;

    m_noAmmoTime = NoAmmoMessageTime;

    if (!vrPlayInterfaceSound(NoAmmoSound) && !m_loggedNoAmmoSound) {
      Logger::warn(str::format("VR: The sound '", NoAmmoSound, "' was not found"));
      m_loggedNoAmmoSound = true;
    }

    if (!m_showsNoAmmo)
      return;

    auto setting = reinterpret_cast<const uint8_t*>(VrGame::NoAmmoMessageSetting);
    auto text    = readField<const char*>(setting, VrGame::SettingValue);

    if (!text || !*text)
      return;

    reinterpret_cast<ShowHudMessageFn>(VrGame::ShowHudMessage)(text, VrGame::HudMessageSad,
      nullptr, nullptr, NoAmmoMessageTime, 0);
  }


  VrReloadSituation VrManualReload::situation() const {
    VrReloadSituation situation;
    situation.kind        = m_kind;
    situation.hasRack     = m_hasRackPart;
    situation.magazine    = m_magazine.state();
    situation.rounds      = m_rounds.state();
    situation.clip        = m_lastClip;
    situation.full        = m_refusedClip >= 0 && m_lastClip >= m_refusedClip;
    situation.pendingFill = m_pendingFill;
    situation.jammed      = m_jammed;
    situation.unchambered = m_unchambered;
    situation.actionOpen  = actionOpen();
    situation.hasLock     = m_hasLockPart;
    situation.lockOpen    = m_lock.open();
    return situation;
  }


  void VrManualReload::clearJam(const uint8_t* weapon, const char* how) {
    if (m_jammed.exchange(false))
      Logger::info(str::format("VR: '", weaponName(weapon), "' is no longer jammed: ", how));
  }


  void VrManualReload::ejectMagazine(uint8_t* player, const uint8_t* weapon, const VrAnimationSound* sound,
      const VrVector3& well, bool chamber) {
    // The rounds of the clip are still counted in the inventory: emptying
    // it loses none (findings/reload.md §1). A chambered round stays.
    uint8_t* clip = currentAmmo(player);
    int32_t  rounds = clip ? readField<int32_t>(clip, VrGame::InventoryEntryCount) : NoClip;

    if (clip)
      *reinterpret_cast<int32_t*>(clip + VrGame::InventoryEntryCount) = chamber ? 1 : 0;

    m_pendingFill = false;

    if (sound)
      vrPlayReloadSound(*sound, well);

    Logger::info(str::format("VR: The magazine of '", weaponName(weapon), "' is taken out with ",
      rounds, " round(s), they stay in the inventory", chamber ? "; one stays chambered" : ""));
  }


  void VrManualReload::openAction(uint8_t* player, const uint8_t* weapon, const VrAnimationSound* sound,
      const VrVector3& breech, bool cylinder) {
    // The shells or cases are thrown out, spent or not: the rounds of the
    // clip are still counted in the inventory, so none is lost
    // (findings/reload.md §1)
    uint8_t* clip = currentAmmo(player);
    int32_t  rounds = clip ? readField<int32_t>(clip, VrGame::InventoryEntryCount) : NoClip;

    if (clip)
      *reinterpret_cast<int32_t*>(clip + VrGame::InventoryEntryCount) = 0;

    // A speed loader the hand took while the cylinder was closed stays in
    // the hand
    if (m_magazine.state() != VrMagazineState::InHand)
      m_magazine.empty();

    if (sound)
      vrPlayReloadSound(*sound, breech);

    Logger::info(str::format(cylinder ? "VR: The cylinder of '" : "VR: The action of '", weaponName(weapon),
      cylinder ? "' is swung out, its cases are thrown out, clip " : "' is broken open, its shells are thrown out, clip ",
      rounds, " before: the rounds stay in the inventory"));

    clearJam(weapon, cylinder ? "its cylinder was swung out" : "its action was opened");
  }


  void VrManualReload::insertMagazine(uint8_t* player, uint8_t* weapon, const VrAnimationSound* sound,
      const VrVector3& well, bool chambered) {
    if (!chambered) {
      // The clip is filled when the weapon is racked
      m_pendingFill = true;

      if (sound)
        vrPlayReloadSound(*sound, well);

      Logger::info(str::format("VR: A magazine is pushed into '", weaponName(weapon),
        "' with nothing chambered, the weapon must be racked"));
      return;
    }

    if (fillClip(player, weapon) && sound)
      vrPlayReloadSound(*sound, well);
  }


  void VrManualReload::rackWeapon(uint8_t* player, uint8_t* weapon, const VrAnimationSound* sound, const VrVector3& rack) {
    if (sound)
      vrPlayReloadSound(*sound, rack);

    int32_t clip = clipCount(player);

    if (m_magazine.state() == VrMagazineState::InWeapon && m_pendingFill) {
      m_pendingFill = false;
      Logger::info(str::format("VR: '", weaponName(weapon), "' is racked, a round is chambered"));

      if (fillClip(player, weapon))
        m_fillFrames = ClipWatchFrames;
    } else if (m_magazine.state() == VrMagazineState::InWeapon) {
      Logger::info(str::format("VR: '", weaponName(weapon), "' is racked, clip ", clip,
        clip > 0 ? ": a round was already chambered, none is lost" : ": the magazine is empty"));
    } else if (clip > 0) {
      // The chambered round comes out, it stays in the inventory
      if (uint8_t* entry = currentAmmo(player))
        *reinterpret_cast<int32_t*>(entry + VrGame::InventoryEntryCount) = 0;

      Logger::info(str::format("VR: '", weaponName(weapon), "' is racked with no magazine, the chambered round is taken out"));
    } else {
      Logger::info(str::format("VR: '", weaponName(weapon), "' is racked with no magazine and nothing chambered"));
    }
  }


  VrRoundEvent VrManualReload::updateRounds(const VrManualReloadInput& input, uint8_t* player, uint8_t* weapon,
      bool active, bool closed) {
    const VrRoundLoop* loop = input.animation && input.animation->hasLoop ? &input.animation->loop : nullptr;

    VrRoundInput rounds;
    rounds.active = active && loop && input.port;
    rounds.grip   = input.grip;
    rounds.closed = closed;

    if (rounds.active) {
      rounds.hand       = input.leftHand->position;
      rounds.round      = vrPointFromHand(*input.leftHand, input.held);
      rounds.port       = vrPointFromHand(*input.rightHand, *input.port);
      rounds.head       = *input.head;
      rounds.roundsHeld = ammoToTake(player, true) != nullptr;
      rounds.dt         = input.frameTime * reloadSpeed(player);
    }

    VrRoundEvent event = m_rounds.update(rounds, loop ? loop->timing : VrRoundLoopTiming());

    // The loop's sounds, as the game plays them while it loops
    if (loop && m_rounds.state() == VrRoundState::Loading)
      playLoopSounds(*loop, rounds.port);

    switch (event) {
      case VrRoundEvent::Taken:
        m_heldAmmo = ammoToTake(player, true);
        Logger::info(str::format("VR: A handful of rounds for '", weaponName(weapon), "' is taken behind the back"));
        break;

      case VrRoundEvent::NoRounds:
        Logger::info(str::format("VR: No rounds left for '", weaponName(weapon), "' beyond those loaded, none are taken"));
        break;

      case VrRoundEvent::Started:
        Logger::info(str::format("VR: The rounds reach the loading port of '", weaponName(weapon),
          "', they go in one per loop of the game's loading animation, clip ", clipCount(player)));
        break;

      case VrRoundEvent::Placed:
        if (!loadRound(player, weapon)) {
          m_rounds.refuse();
          event = VrRoundEvent::Full;
        }
        break;

      case VrRoundEvent::Stopped:
        Logger::info(str::format("VR: The left hand leaves the loading port of '", weaponName(weapon),
          closed ? "', which closed" : "'", ", it keeps its rounds, clip ", clipCount(player)));
        break;

      case VrRoundEvent::Dropped:
        m_heldAmmo = nullptr;
        Logger::info(str::format("VR: The rounds of '", weaponName(weapon), "' are let go of, clip ", clipCount(player)));
        break;

      case VrRoundEvent::Full:
      case VrRoundEvent::None:
        break;
    }

    return event;
  }


  void VrManualReload::playLoopSounds(const VrRoundLoop& loop, const VrVector3& port) const {
    for (const VrRoundLoopSound& sound : loop.sounds) {
      if (vrSoundKeyCrossed(m_rounds.lastTime(), m_rounds.time(), sound.time))
        vrPlayReloadSound(sound.sound, port);
    }
  }


  bool VrManualReload::loadRound(uint8_t* player, uint8_t* weapon) {
    loadHeldAmmo();

    int32_t before = clipCount(player);

    // The game's own looping reload with no animation adds one round
    if (!fillClip(player, weapon)) {
      m_refusedClip = before;
      Logger::info(str::format("VR: '", weaponName(weapon), "' takes no more rounds, clip ", before,
        ": the loading loop stops, the hand keeps its rounds"));
      return false;
    }

    Logger::info(str::format("VR: A round is loaded into '", weaponName(weapon), "', clip ", before, " before",
      m_unchambered ? ", nothing is chambered" : ""));
    return true;
  }


  void VrManualReload::cycleAction(uint8_t* player, uint8_t* weapon, const VrAnimationSound* sound, const VrVector3& rack) {
    if (sound)
      vrPlayReloadSound(*sound, rack);

    int32_t clip = clipCount(player);

    if (clip <= 0) {
      Logger::info(str::format("VR: The action of '", weaponName(weapon), "' is worked with no round loaded"));
    } else if (m_unchambered.exchange(false)) {
      Logger::info(str::format("VR: The action of '", weaponName(weapon), "' is worked, a round is chambered, clip ", clip));
    } else {
      Logger::info(str::format("VR: The action of '", weaponName(weapon), "' is worked, clip ", clip,
        ": a round was already chambered, none is lost"));
    }
  }


  bool VrManualReload::fillClip(uint8_t* player, uint8_t* weapon) {
    // As the game's callers do, the extended magazine counts if it is installed
    uint8_t* entry = weaponEntry(player);
    uint32_t extended = entry
      ? reinterpret_cast<HasModEffectFn>(VrGame::ItemHasModEffect)(entry, nullptr, VrGame::ModEffectClipSize) & 0xFF
      : 0;

    // No animation: the clip is filled through the equipment queue at the
    // next frame, and the action points are spent as in the game
    uint32_t result = reinterpret_cast<ReloadWeaponFn>(g_previousReload)(
      player, nullptr, weapon, 0, extended, 0);

    Logger::info(str::format("VR: The clip of '", weaponName(weapon), "' is filled (extended magazine ",
      extended, "): ", (result & 0xFF) ? "reload started" : "refused, the clip is full or no rounds are left"));

    watchClip(clipCount(player));
    return result & 0xFF;
  }


  void VrManualReload::watchClip(int32_t clip) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    m_watchFrames = ClipWatchFrames;
    m_watchedClip = clip;
  }


  uint32_t __fastcall VrManualReload::reloadHook(
          uint8_t*              player,
          void*                 unused,
          uint8_t*              weapon,
          uint32_t              animation,
          uint32_t              useMod,
          uint32_t              fromInventory) {
    uintptr_t caller = VR_RETURN_ADDRESS();
    int32_t   before = clipCount(player);

    // The weapons reloaded by hand are not reloaded by the control or when
    // a shot empties the clip: an empty clip clicks. V.A.T.S. plays its
    // queued shots on its own and waits for the game's reload, so there
    // the game reloads as it does without the hands.
    bool byHand = g_manualReload && weapon && weapon == g_manualReload->m_handWeapon.load()
      && (caller == VrGame::ReloadReturnControl || caller == VrGame::ReloadReturnEmptyClip)
      && !inVats();

    if (byHand) {
      Logger::info(str::format("VR: Reload of '", weaponName(weapon), "' from the ", callerName(caller),
        " refused, the weapon is reloaded with the hands, clip ", before));
      return 0;
    }

    uint32_t result = reinterpret_cast<ReloadWeaponFn>(g_previousReload)(
      player, nullptr, weapon, animation, useMod, fromInventory);

    int32_t after = clipCount(player);

    // The game's own reload (equipping, V.A.T.S...) leaves a round chambered
    if (g_manualReload && (result & 0xFF) && weapon == g_manualReload->m_handWeapon.load())
      g_manualReload->m_unchambered = false;

    Logger::info(str::format("VR: Reload of '", weaponName(weapon), "' from the ", callerName(caller),
      " (return address 0x", std::hex, caller, std::dec, "), animation ", animation,
      ", mod ", useMod & 0xFF, ", from inventory ", fromInventory & 0xFF,
      ": ", (result & 0xFF) ? "started" : "refused", ", clip ", before, " before, ", after, " after"));

    if (g_manualReload)
      g_manualReload->watchClip(after);

    return result;
  }


  int32_t __fastcall VrManualReload::clipCountHook(
          uint8_t*              entry,
          void*                 unused) {
    // A jammed weapon, or an open action, does not fire: the attack
    // sees an empty clip and clicks. The game's own jam goes on in V.A.T.S.
    if (g_manualReload && (g_manualReload->m_jammed.load() || g_manualReload->m_unchambered.load()
        || g_manualReload->m_actionOpen.load())
     && !inVats() && entry == currentAmmo(player()))
      return 0;

    return readField<int32_t>(entry, VrGame::InventoryEntryCount);
  }


  uint32_t __fastcall VrManualReload::jamGroupHook(
          uint8_t*              actor,
          void*                 unused,
          uint32_t              group,
          void*                 entry,
          uint32_t              checkHandToHand,
          void*                 animation) {
    uint32_t found = reinterpret_cast<GetAnimGroupFn>(VrGame::ActorGetAnimGroup)(
      actor, nullptr, group, entry, checkHandToHand, animation);

    if (actor != player())
      return found;

    uint8_t* weapon = weaponEntry(actor);
    weapon = weapon ? readField<uint8_t*>(weapon, VrGame::InventoryEntryForm) : nullptr;

    // A weapon reloaded by hand stays jammed until it is racked: no jam
    // animation, and no attack
    bool byHand = g_manualReload && g_holdsJams && weapon
      && weapon == g_manualReload->m_handWeapon.load() && g_manualReload->m_holdsJam.load() && !inVats();

    Logger::info(str::format("VR: The player's weapon jammed (group 0x", std::hex, group,
      ", animation group 0x", found & 0xFFFF, std::dec, "), clip ", clipCount(actor),
      byHand ? ": it must be racked, the game's jam animation is not played" : ""));

    if (!byHand)
      return found;

    g_manualReload->m_jammed     = true;
    g_manualReload->m_jamStarted = true;
    return NoAnimGroup;
  }

}
