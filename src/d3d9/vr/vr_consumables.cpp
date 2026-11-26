#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_consumables.h"
#include "vr_game_addresses.h"
#include "vr_game_file.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_game_sound.h"
#include "vr_math.h"
#include "vr_module_path.h"
#include "vr_texts.h"
#include "vr_weapon_drawn.h"
#include "vr_weapon_hand.h"

namespace dxvk {

  namespace {

    // TESDataHandler::GetCompiledFile, __thiscall with the load order index
    using GetCompiledFileFn = uint8_t* (__fastcall*)(void* dataHandler, void* unused, uint32_t index);

    // EffectItemList::CanBePoison, __thiscall, returns a bool in AL
    using CanBePoisonFn = uint32_t (__fastcall*)(const uint8_t* effects, void* unused);

    // Interface::ShowMessage, __cdecl with the text, the face, the image,
    // the sound, the time and whether it is shown at once
    using ShowHudMessageFn = uint32_t (__cdecl*)(const char* text, uint32_t emotion, const char* image,
      const char* sound, float time, uint32_t instant);

    // Actor::QueueEquipObject: item, count, extra list, queue, force,
    // play sound
    using QueueEquipFn = void (__thiscall*)(void* actor, const void* item, int32_t count, void* extra,
      bool queue, bool force, bool sound);

    // BGSDefaultObjectManager::GetDefaultObject, __cdecl with the index
    using DefaultObjectFn = const uint8_t* (__cdecl*)(uint32_t index);

    // ActorValueOwner, __thiscall with the actor value, int in EAX
    using ActorValueFn = int32_t (__thiscall*)(void* owner, uint32_t value);

    // TESObjectREFR::Get3D, virtual
    using Get3DFn = uint8_t* (__thiscall*)(void* ref);

    using AttachChildFn = void (__thiscall*)(void* node, void* child, uint32_t firstFree);
    using DetachChildFn = void (__thiscall*)(void* node, void* child);
    using UpdateFn      = void (__thiscall*)(void* object, void* data);
    using AssignFn      = void* (__thiscall*)(void* pointer, void* object);

    // B42 Inject's first-person animations without power armour
    constexpr char AnimationFolder[] = "Data\\Meshes\\Characters\\_Male\\IdleAnims\\B42Inject\\0\\1\\";

    // The first-person skeleton is a few dozen levels deep at most
    constexpr uint32_t SkeletonDepth = 48;

    // How long the weapon may take to be holstered
    constexpr float HolsterWait = 3.0f;

    // How long the HUD says what was consumed
    constexpr float MessageSeconds = 2.0f;

    // How long the player's update may take to pick a held item up
    constexpr float PickUpWait = 0.5f;

    // How long the prop slides from where the held object was to where
    // the animation holds it
    constexpr float SlideSeconds = 0.25f;

    // A needle held from the world goes into the body below the chest,
    // where letting go of an item puts it in the inventory (vrGrabAtChest:
    // down to 50 cm below the head)
    constexpr float WorldBodyBelowMin = 0.5f;

    // A spat cork: metres per second ahead and up, gravity, radians per
    // second of tumble, seconds before it is hidden
    constexpr float SpitSpeedAhead    = 2.5f;
    constexpr float SpitSpeedUp       = 1.0f;
    constexpr float SpitGravity       = 9.81f;
    constexpr float SpitSpin          = 15.0f;
    constexpr float SpitFlightSeconds = 0.6f;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename T>
    T read(const uint8_t* object, uintptr_t offset) {
      T value;
      std::memcpy(&value, object + offset, sizeof(T));
      return value;
    }

    template<typename Fn>
    Fn method(void* object, uintptr_t slot) {
      return *reinterpret_cast<Fn*>(field<uint8_t*>(object, 0) + slot);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    void* skeletonNode(const char* name) {
      uint8_t* actor = player();
      uint8_t* root  = actor ? field<uint8_t*>(actor, VrGame::PlayerFirstPersonRoot) : nullptr;

      return root ? reinterpret_cast<void*>(vrFindGameObject(reinterpret_cast<uintptr_t>(root), name, SkeletonDepth)) : nullptr;
    }

    VrGameRotation worldRotation(void* object) {
      VrGameRotation r;
      std::memcpy(r.m, static_cast<uint8_t*>(object) + VrGame::ObjectWorldRotation, sizeof(r.m));
      return r;
    }

    // Brings the nodes below a hand bone, as the game's update left them,
    // to where the hand is drawn
    VrGameTransform toDrawnHand(void* bone, const VrGameTransform& drawn) {
      VrGameTransform transform;
      transform.rotate    = drawn.rotate * vrTranspose(worldRotation(bone));
      transform.translate = drawn.translate - transform.rotate * field<VrVector3>(bone, VrGame::ObjectWorldTranslation);
      return transform;
    }

    const char* handName(VrHand hand) {
      return hand == VrHand::Left ? "left" : "right";
    }

    const char* handNode(VrHand hand) {
      return hand == VrHand::Left ? VrGame::LeftHandNodeName : VrGame::RightHandNodeName;
    }

    VrHand otherHand(VrHand hand) {
      return VrHand(uint32_t(hand) ^ 1u);
    }

    void setLocal(void* object, const VrSequencePose& pose) {
      std::memcpy(static_cast<uint8_t*>(object) + VrGame::ObjectLocalRotation, pose.rotate.m, sizeof(pose.rotate.m));
      field<VrVector3>(object, VrGame::ObjectLocalTranslation) = pose.translate;
      field<float>(object, VrGame::ObjectLocalScale) = pose.scale;
    }

    void setCulled(void* object, bool culled) {
      uint32_t& flags = field<uint32_t>(object, VrGame::ObjectFlags);
      flags = culled ? (flags | VrGame::ObjectFlagAppCulled) : (flags & ~VrGame::ObjectFlagAppCulled);
    }

    void updateObject(void* object) {
      uint8_t data[VrGame::UpdateDataSize] = { };
      reinterpret_cast<UpdateFn>(VrGame::ObjectUpdate)(object, data);
    }

    std::string fileName(const std::string& path) {
      size_t slash = path.find_last_of("\\/");
      std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
      size_t dot = name.find_last_of('.');
      return dot == std::string::npos ? name : name.substr(0, dot);
    }

    // The animation files in a folder of the game's, which holds d3d9.dll:
    // B42 Inject's files are loose, the game's file system cannot list them
    std::vector<std::string> animationFiles(const std::string& folder) {
      std::vector<std::string> files;
      std::wstring pattern = vrModuleDirectory();

      if (pattern.empty())
        return files;

      for (char c : folder + "*.kf")
        pattern += wchar_t(uint8_t(c));

      WIN32_FIND_DATAW found;
      HANDLE search = FindFirstFileW(pattern.c_str(), &found);

      if (search == INVALID_HANDLE_VALUE)
        return files;

      do {
        std::string name;

        for (const wchar_t* c = found.cFileName; *c; c++)
          name += *c < 0x80 ? char(*c) : '?';

        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name.find('?') == std::string::npos)
          files.push_back(name);
      } while (FindNextFileW(search, &found));

      FindClose(search);
      std::sort(files.begin(), files.end());
      return files;
    }

  }


  std::unique_ptr<VrConsumables> VrConsumables::install(const VrTexts& texts) {
    if (!VrGameHotKeys::available()
     || !VrGameMemory::matches(VrGame::DataHandlerGetCompiledFile,
          VrGame::DataHandlerGetCompiledFilePrologue, sizeof(VrGame::DataHandlerGetCompiledFilePrologue))
     || !VrGameMemory::matches(VrGame::EffectItemListCanBePoison,
          VrGame::EffectItemListCanBePoisonPrologue, sizeof(VrGame::EffectItemListCanBePoisonPrologue))
     || !VrGameMemory::matches(VrGame::NiPointerAssign,
          VrGame::NiPointerAssignPrologue, sizeof(VrGame::NiPointerAssignPrologue))
     || !VrGameMemory::matches(VrGame::ShowHudMessage,
          VrGame::ShowHudMessagePrologue, sizeof(VrGame::ShowHudMessagePrologue))) {
      Logger::warn("VR: The game's item functions were not found, items of the quick wheel are used at once");
      return nullptr;
    }

    auto consumables = std::unique_ptr<VrConsumables>(new VrConsumables(texts));

    consumables->m_worldItems = VrGameMemory::matches(VrGame::ActorQueueEquipObject,
        VrGame::ActorQueueEquipObjectPrologue, sizeof(VrGame::ActorQueueEquipObjectPrologue))
      && VrGameMemory::matches(VrGame::DefaultObjectGet,
        VrGame::DefaultObjectGetPrologue, sizeof(VrGame::DefaultObjectGetPrologue));

    if (!consumables->m_worldItems)
      Logger::warn("VR: The game's equip function was not found, items held from the world are not consumed by hand");

    return consumables;
  }


  VrConsumables::VrConsumables(const VrTexts& texts)
  : m_texts(texts) {

  }


  VrConsumables::~VrConsumables() {
    // The models are left to the game, which may be shutting down
  }


  bool VrConsumables::begin(uint32_t slot, const VrHotKeyItem& item) {
    // Not into a hand that holds an object of the world
    if (busy() || m_leftHolds || !item.present || !item.form)
      return false;

    const VrConsumableAnimation* animation = consumableAnimation(item.form);

    if (!animation)
      return false;

    // The left hand consumes it alone beside a weapon in the right one
    // when nothing has to take a cap off it but the teeth
    bool leftAlone = !animation->hasCap || !animation->spitSounds.empty();

    m_state       = State::Holstering;
    m_hand        = VrHand::Left;
    m_fromWheel   = true;
    m_slot        = slot;
    m_item        = item;
    m_itemSound   = read<const uint8_t*>(item.form, VrGame::AlchemyConsumeSound);
    m_animation   = animation;
    m_holstered   = false;
    m_keepWeapon  = m_armed && leftAlone;
    m_hasSlide    = false;
    m_capNow      = false;
    m_waited      = 0.0f;
    m_otherDown   = false;

    Logger::info(str::format("VR: '", item.name, "' comes into the left hand to be consumed",
      m_keepWeapon ? ", the weapon stays drawn" : ""));
    return true;
  }


  VrConsumablesEvent VrConsumables::update(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand,
      VrPhysicsGrab* grab) {
    playPending(input.dt);
    m_armed     = input.armed;
    m_leftHolds = grab && grab->holds(VrHand::Left);

    if (!input.inGame) {
      m_watches = { };

      if (m_state == State::Idle)
        return VrConsumablesEvent::None;

      // A pick up asked for still ends in the player's update
      if (m_state == State::PickingUp) {
        m_state     = State::Idle;
        m_animation = nullptr;
        return VrConsumablesEvent::None;
      }

      Logger::info(str::format("VR: '", m_item.name, "' goes back to the inventory, a menu opened"));
      finish(drawn, hand);
      return VrConsumablesEvent::Cancelled;
    }

    switch (m_state) {
      case State::Idle:
        return watchWorld(input, hand, grab);

      case State::PickingUp:
        return endPickUp(input, drawn, hand, grab);

      case State::Holstering:
        if (!m_keepWeapon && drawn && !drawn->handsFree()) {
          if (!m_holstered) {
            drawn->holster(true);
            m_holstered = true;
          }

          if ((m_waited += input.dt) < HolsterWait)
            return VrConsumablesEvent::None;

          Logger::warn(str::format("VR: The weapon was not holstered, '", m_item.name, "' goes back to the inventory"));
          finish(drawn, hand);
          return VrConsumablesEvent::Cancelled;
        }

        if (!startHolding(hand)) {
          Logger::warn(str::format("VR: The model of '", m_item.name, "' was not attached, it is used at once"));
          finish(drawn, hand);
          VrGameHotKeys::use(m_slot);
          return VrConsumablesEvent::Consumed;
        }

        queueSounds(m_animation->drawSounds, 0.0f);
        return VrConsumablesEvent::Started;

      case State::Holding:
        return hold(input, drawn, hand);
    }

    return VrConsumablesEvent::None;
  }


  const VrConsumableAnimation* VrConsumables::consumableAnimation(const uint8_t* form) {
    if (!form || read<uint8_t>(form, VrGame::FormType) != VrGame::FormTypeAlchemy)
      return nullptr;

    // A poison goes on the weapon
    if (reinterpret_cast<CanBePoisonFn>(VrGame::EffectItemListCanBePoison)(
        form + VrGame::AlchemyEffectList, nullptr) & 0xFF)
      return nullptr;

    const VrConsumableAnimation* animation = findAnimation(form);
    return animation && animation->gesture != VrConsumeGesture::Instant ? animation : nullptr;
  }


  VrConsumablesEvent VrConsumables::watchWorld(const VrConsumablesInput& input, VrWeaponHand* hand, VrPhysicsGrab* grab) {
    for (uint32_t i = 0; i < VrHandCount; i++) {
      Watch& watch = m_watches[i];
      VrHeldItem held;

      if (!m_worldItems || !grab || !grab->heldItem(VrHand(i), held)) {
        watch = Watch();
        continue;
      }

      // An object taken anew is looked at once
      if (watch.ref != held.ref || watch.base != held.base) {
        watch = Watch();
        watch.ref       = held.ref;
        watch.base      = held.base;
        watch.otherDown = input.grips[i ^ 1u];
        watch.animation = consumableAnimation(held.base);

        if (!watch.animation)
          continue;

        if (!watch.animation->hasCap)
          watch.gesture.start(watch.animation->gesture, false, false, watch.animation->useSeconds());

        const char* name = read<const char*>(held.base, VrGame::AlchemyFullName);
        Logger::info(str::format("VR: The ", handName(VrHand(i)), " hand holds '", name ? name : "",
          "', which may be consumed in the hand", watch.animation->hasCap ? " once its cap is off" : ""));
      }

      if (!watch.animation || watch.refused)
        continue;

      VrConsumablesEvent event = watchHand(input, hand, grab, VrHand(i), held);

      if (event != VrConsumablesEvent::None || busy())
        return event;
    }

    return VrConsumablesEvent::None;
  }


  VrConsumablesEvent VrConsumables::watchHand(const VrConsumablesInput& input, VrWeaponHand* hand, VrPhysicsGrab* grab,
      VrHand side, const VrHeldItem& held) {
    constexpr float Unit = VrGameUnitsPerMetre;

    Watch& watch = m_watches[uint32_t(side)];
    const VrConsumableAnimation& animation = *watch.animation;
    VrHand other = otherHand(side);

    bool otherDown = input.grips[uint32_t(other)];
    bool otherPressed = otherDown && !watch.otherDown;
    watch.otherDown = otherDown;

    // The held object where it is drawn: the game moves it with its body
    uint8_t* object = method<Get3DFn>(held.ref, VrGame::RefGet3DSlot)(held.ref);
    const float* bound = object ? field<const float*>(object, VrGame::ObjectWorldBound) : nullptr;

    if (!bound || bound[3] <= 0.0f)
      return VrConsumablesEvent::None;

    VrVector3 centre = { bound[0], bound[1], bound[2] };
    float radius = std::min(bound[3], m_zones.largestRadius * Unit);

    // The other hand takes the cap unless it holds the weapon or an object
    bool otherFree = !(side == VrHand::Left && input.armed) && !grab->holds(other);

    if (animation.hasCap) {
      if (otherPressed && otherFree && vrLength(input.hands[uint32_t(other)] - centre) <= radius + m_zones.capReach * Unit) {
        startPickUp(hand, grab, side, object, PickUp::CapInHand);
      } else if (!animation.spitSounds.empty() && vrIsWhereConsumed(VrConsumeGesture::Mouth, input.head,
          centre, radius, input.hands[uint32_t(other)], 0.0f, m_zones)) {
        startPickUp(hand, grab, side, object, PickUp::CapAtMouth);
      }

      return VrConsumablesEvent::None;
    }

    // Without a cap the held object itself is consumed, with its sounds
    m_itemSound = read<const uint8_t*>(watch.base, VrGame::AlchemyConsumeSound);

    VrConsumeZones zones = m_zones;
    zones.bodyBelowMin = WorldBodyBelowMin;

    VrConsumeInput gesture;
    gesture.head      = input.head;
    gesture.centre    = centre;
    gesture.radius    = bound[3];
    gesture.otherHand = input.hands[uint32_t(other)];
    gesture.otherGrip = otherDown && otherFree;
    gesture.dt        = input.dt;

    VrConsumeEvent event = watch.gesture.update(gesture, zones);
    const char* name = read<const char*>(watch.base, VrGame::AlchemyFullName);

    if (event == VrConsumeEvent::Arrived && refusesNow(watch.base)) {
      watch.refused = true;
      watch.gesture.reset();
      return VrConsumablesEvent::None;
    }

    if (event == VrConsumeEvent::Arrived || event == VrConsumeEvent::Left) {
      Logger::info(str::format("VR: '", name ? name : "", event == VrConsumeEvent::Arrived
        ? "', held from the world, is where it is consumed, the use clock runs from "
        : "', held from the world, left where it is consumed, the use clock waits at ",
        watch.gesture.clock(), " s"));
    }

    float clock = watch.gesture.clock();
    playUseSounds(animation, clock, watch.nextUseSound);

    if (event != VrConsumeEvent::Consumed)
      return VrConsumablesEvent::None;

    // The sounds after the use play on
    for (; watch.nextUseSound < animation.useSounds.size(); watch.nextUseSound++) {
      VrConsumableSound late = animation.useSounds[watch.nextUseSound];
      late.time -= clock;
      queueSounds({ late }, 0.0f);
    }

    startPickUp(hand, grab, side, object, PickUp::Consume);
    return VrConsumablesEvent::None;
  }


  void VrConsumables::startPickUp(VrWeaponHand* hand, VrPhysicsGrab* grab, VrHand side, uint8_t* object, PickUp kind) {
    const Watch& watch = m_watches[uint32_t(side)];
    const char* name = read<const char*>(watch.base, VrGame::AlchemyFullName);

    m_state       = State::PickingUp;
    m_hand        = side;
    m_fromWheel   = false;
    m_pickUp      = kind;
    m_item        = VrHotKeyItem();
    m_item.present  = true;
    m_item.name     = name ? name : "";
    m_item.form     = watch.base;
    m_item.formType = VrGame::FormTypeAlchemy;
    m_itemSound   = read<const uint8_t*>(watch.base, VrGame::AlchemyConsumeSound);
    m_animation   = watch.animation;
    m_holstered   = false;
    m_keepWeapon  = false;
    m_hasSlide    = false;
    m_capNow      = kind == PickUp::CapInHand;
    m_waited      = 0.0f;
    m_otherDown   = false;

    // The prop starts where the object is in the drawn hand
    void* bone = skeletonNode(handNode(side));
    VrGameTransform drawn;

    if (kind != PickUp::Consume && bone && hand && hand->freeHandBone(side, drawn)) {
      float handScale = field<float>(bone, VrGame::ObjectWorldScale);
      float inverse   = handScale > 0.0f ? 1.0f / handScale : 1.0f;

      m_slideFrom.rotate    = vrTranspose(drawn.rotate) * worldRotation(object);
      m_slideFrom.translate = vrTranspose(drawn.rotate)
        * (field<VrVector3>(object, VrGame::ObjectWorldTranslation) - drawn.translate) * inverse;
      m_slideFrom.scale     = field<float>(object, VrGame::ObjectWorldScale) * inverse;
      m_hasSlide  = true;
      m_slideTime = 0.0f;
    }

    grab->pickUp(side, watch.base);

    Logger::info(str::format("VR: '", m_item.name, "' is picked up from the ", handName(side), " hand ",
      kind == PickUp::Consume ? "to be used" : kind == PickUp::CapInHand ? "as the other hand takes its cap"
        : "as its cork comes to the mouth"));
  }


  VrConsumablesEvent VrConsumables::endPickUp(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand,
      VrPhysicsGrab* grab) {
    bool taken = false;

    if (!grab || !grab->takePickUp(m_hand, taken)) {
      if ((m_waited += input.dt) < PickUpWait)
        return VrConsumablesEvent::None;

      Logger::warn(str::format("VR: '", m_item.name, "' was not picked up in time"));
      m_state     = State::Idle;
      m_animation = nullptr;
      return VrConsumablesEvent::None;
    }

    m_watches[uint32_t(m_hand)] = Watch();

    if (!taken) {
      Logger::info(str::format("VR: The game did not let '", m_item.name, "' be picked up"));
      m_state     = State::Idle;
      m_animation = nullptr;
      return VrConsumablesEvent::Cancelled;
    }

    if (m_pickUp == PickUp::Consume) {
      consume();
      m_state     = State::Idle;
      m_animation = nullptr;
      return VrConsumablesEvent::Consumed;
    }

    // In the hand now: fists are put away, a weapon stays beside a left
    // hand that consumes alone (only the teeth take a cork off then)
    m_keepWeapon = input.armed && m_hand == VrHand::Left && m_pickUp == PickUp::CapAtMouth;

    if (!m_keepWeapon && drawn && !drawn->handsFree()) {
      drawn->holster(true);
      m_holstered = true;
    }

    if (!startHolding(hand)) {
      Logger::warn(str::format("VR: The model of '", m_item.name, "' was not attached, it stays in the inventory"));
      finish(drawn, hand);
      return VrConsumablesEvent::Cancelled;
    }

    return VrConsumablesEvent::Started;
  }


  bool VrConsumables::startHolding(VrWeaponHand* hand) {
    if (!attachProp(hand))
      return false;

    m_gesture.start(m_animation->gesture, m_cap != nullptr, !m_animation->spitSounds.empty(), m_animation->useSeconds());
    m_spitting     = false;
    m_nextUseSound = 0;
    poseParts(m_animation->useStart);
    m_state = State::Holding;
    return true;
  }


  VrConsumablesEvent VrConsumables::hold(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand) {
    VrHand other = otherHand(m_hand);
    slideProp(input.dt);

    // The game updates the arms in its own pose, the hands reach their
    // controllers only where they are drawn: the item is read where its
    // hand is drawn
    VrGameTransform toWorld;
    VrGameTransform boneDrawn;
    void* bone = skeletonNode(handNode(m_hand));

    if (hand && bone && hand->freeHandBone(m_hand, boneDrawn))
      toWorld = toDrawnHand(bone, boneDrawn);

    // The item's bound, and the cap where it is on the item. A hand that
    // holds the weapon takes no cap.
    VrConsumeInput gesture;
    gesture.head      = input.head;
    gesture.otherHand = input.hands[uint32_t(other)];
    gesture.otherGrip = input.grips[uint32_t(other)] && !m_keepWeapon;
    gesture.dt        = input.dt;

    void* propRoot = m_prop ? m_prop->root() : nullptr;

    if (propRoot && m_spitting) {
      VrVector3 centre = worldRotation(propRoot) * m_spitBound * field<float>(propRoot, VrGame::ObjectWorldScale)
        + field<VrVector3>(propRoot, VrGame::ObjectWorldTranslation);
      gesture.centre = toWorld.rotate * centre + toWorld.translate;
      gesture.radius = m_spitRadius;
    } else if (const float* bound = propRoot ? field<const float*>(propRoot, VrGame::ObjectWorldBound) : nullptr) {
      gesture.centre = toWorld.rotate * VrVector3 { bound[0], bound[1], bound[2] } + toWorld.translate;
      gesture.radius = bound[3];
    }

    // The cap's origin is the item's: its mesh sits away from it, at the
    // neck, where its bound is
    if (m_cap) {
      const float* capBound = field<const float*>(m_cap, VrGame::ObjectWorldBound);
      VrVector3 cap = capBound && capBound[3] > 0.0f
        ? VrVector3 { capBound[0], capBound[1], capBound[2] }
        : field<VrVector3>(m_cap, VrGame::ObjectWorldTranslation);
      gesture.cap = toWorld.rotate * cap + toWorld.translate;
    }

    // The grip that had the game pick a held item up has its cap already
    if (m_capNow) {
      m_capNow = false;

      if (m_cap && m_gesture.capOn() && gesture.otherGrip) {
        m_gesture.takeCapInHand();
        m_otherDown = true;

        if (!takeCap(hand, toWorld))
          setCulled(m_cap, true);

        queueSounds(m_animation->capSounds, 0.0f);
        return VrConsumablesEvent::CapTaken;
      }
    }

    bool otherPressed = gesture.otherGrip && !m_otherDown;
    m_otherDown = gesture.otherGrip;

    VrConsumeEvent event = m_gesture.update(gesture, m_zones);

    if (otherPressed && m_cap && m_gesture.capOn()) {
      Logger::info(str::format("VR: The ", handName(other), " grip closed ",
        int(vrLength(gesture.otherHand - gesture.cap) * 100.0f / VrGameUnitsPerMetre), " cm from the cap of '",
        m_item.name, "', it takes it within ", int(m_zones.capReach * 100.0f), " cm"));
    }

    switch (event) {
      case VrConsumeEvent::CapTaken:
        // The cap has to leave the item even if it cannot follow the hand
        if (!takeCap(hand, toWorld))
          setCulled(m_cap, true);

        queueSounds(m_animation->capSounds, 0.0f);
        return VrConsumablesEvent::CapTaken;

      case VrConsumeEvent::Arrived:
      case VrConsumeEvent::Left:
        Logger::info(str::format("VR: '", m_item.name, event == VrConsumeEvent::Arrived
          ? "' is where it is consumed, the use clock runs from " : "' left where it is consumed, the use clock waits at ",
          m_gesture.clock(), " s"));
        break;

      case VrConsumeEvent::CapBitten:
        biteCap();
        return VrConsumablesEvent::CapBitten;

      case VrConsumeEvent::CapDropped:
        dropCap();
        break;

      default:
        break;
    }

    if (m_spitting)
      flySpitCap(input.head, input.dt, toWorld);

    // The animation's sounds and parts follow the use clock
    float clock = m_gesture.clock();
    playUseSounds(*m_animation, clock, m_nextUseSound);
    poseParts(m_animation->useTime(clock));

    if (event != VrConsumeEvent::Consumed)
      return VrConsumablesEvent::None;

    // The sounds after the use play on
    const std::vector<VrConsumableSound>& sounds = m_animation->useSounds;

    for (; m_nextUseSound < sounds.size(); m_nextUseSound++) {
      VrConsumableSound late = sounds[m_nextUseSound];
      late.time -= clock;
      queueSounds({ late }, 0.0f);
    }

    consume();
    finish(drawn, hand);
    return VrConsumablesEvent::Consumed;
  }


  bool VrConsumables::refusesNow(const uint8_t* form) const {
    // The game's own refusal of the quick wheel's stimpak at full health
    uint8_t* actor = player();

    if (!actor || form != reinterpret_cast<DefaultObjectFn>(VrGame::DefaultObjectGet)(VrGame::DefaultObjectStimpak))
      return false;

    void* owner = actor + VrGame::ActorValueOwnerBase;

    if (method<ActorValueFn>(owner, VrGame::AvOwnerIntSlot)(owner, VrGame::AvHealth)
     != method<ActorValueFn>(owner, VrGame::AvOwnerPermanentIntSlot)(owner, VrGame::AvHealth))
      return false;

    const char* text = *reinterpret_cast<const char* const*>(VrGame::SettingFullHealth + VrGame::SettingValue);
    reinterpret_cast<ShowHudMessageFn>(VrGame::ShowHudMessage)(text ? text : "", 0, nullptr, nullptr, MessageSeconds, 0);
    Logger::info("VR: A stimpak held from the world is refused at full health");
    return true;
  }


  const VrConsumableAnimation* VrConsumables::findAnimation(const uint8_t* form) {
    auto dataHandler = *reinterpret_cast<void* const*>(VrGame::DataHandler);

    if (!dataHandler)
      return nullptr;

    uint32_t formId = read<uint32_t>(form, VrGame::FormId);
    uint8_t* file   = reinterpret_cast<GetCompiledFileFn>(VrGame::DataHandlerGetCompiledFile)(dataHandler, nullptr, formId >> 24);
    const char* editorId = file
      ? vrConsumableEditorId(reinterpret_cast<const char*>(file + VrGame::TesFileName), formId)
      : nullptr;

    if (!editorId)
      return nullptr;

    std::string folder = std::string(AnimationFolder) + editorId + "\\";
    std::vector<std::string> files = animationFiles(folder);

    if (files.empty())
      return nullptr;

    // Several animations for one item are played in turn
    std::string path = folder + files[m_picks++ % files.size()];
    LoadedAnimation& loaded = m_animations[path];

    if (!loaded.valid && loaded.animation.useEnd == 0.0f) {
      std::vector<uint8_t> data;
      VrKfAnimation kf;

      loaded.valid = VrGameFile::read(path.c_str(), VrGame::ArchiveMeshes, data)
        && kf.read(data.data(), data.size())
        && vrReadConsumableAnimation(kf, fileName(path), loaded.animation);

      if (!loaded.valid) {
        loaded.animation.useEnd = -1.0f;
        Logger::warn(str::format("VR: ", path, " is not an animation of B42 Inject, the item is used at once"));
        return nullptr;
      }

      const VrConsumableAnimation& a = loaded.animation;
      Logger::info(str::format("VR: Read ", path, ": ",
        a.prop.empty() ? std::string("the item's own model") : a.prop,
        a.hasCap ? str::format(", cap '", a.cap.node, "'") : std::string(),
        ", ", a.parts.size(), " moving part(s), ", a.useSeconds(), " s of use, ",
        a.drawSounds.size() + a.capSounds.size() + a.useSounds.size(), " sound(s)"));
    }

    return loaded.valid ? &loaded.animation : nullptr;
  }


  bool VrConsumables::attachProp(VrWeaponHand* hand) {
    void* bone = skeletonNode(handNode(m_hand));

    if (!bone)
      return false;

    std::string model = m_animation->prop;

    if (model.empty()) {
      const char* own = read<const char*>(m_item.form, VrGame::AlchemyModelPath);

      if (!own || !*own)
        return false;

      model = own;
    }

    // The animation holds it in the left hand, the right one mirrors it
    m_propPose = m_animation->inHand;
    m_fingers  = &m_animation->fingers;

    if (m_hand == VrHand::Right) {
      auto mirrored = m_mirrored.find(m_animation);

      if (mirrored == m_mirrored.end())
        mirrored = m_mirrored.emplace(m_animation, vrMirrorConsumableHold(m_animation->inHand, m_animation->fingers)).first;

      m_propPose = mirrored->second.inHand;
      m_fingers  = &mirrored->second.fingers;
    }

    m_inHand = { m_propPose.rotate, m_propPose.translate };

    std::string path = "Meshes\\" + model;
    std::unique_ptr<VrSceneModel>& prop = m_props[path];

    if (!prop)
      prop = VrSceneModel::create(path.c_str());

    // A held object's prop starts where the object was
    if (!prop || !prop->attach(bone, m_hasSlide ? m_slideFrom : m_propPose))
      return false;

    m_prop   = prop.get();
    m_cap    = m_animation->hasCap ? m_prop->find(m_animation->cap.node.c_str()) : nullptr;
    m_parts.clear();

    for (const VrConsumablePart& part : m_animation->parts) {
      if (void* node = m_prop->find(part.node.c_str()))
        m_parts.push_back({ node, &part });
    }

    if (m_cap) {
      m_capParent = field<void*>(m_cap, VrGame::ObjectParent);
      setLocal(m_cap, m_animation->capOn);
      setCulled(m_cap, false);
      updateObject(m_prop->root());
    }

    if (hand)
      hand->holdInHand(m_hand, &m_inHand, m_fingers);

    Logger::info(str::format("VR: '", m_item.name, "' is in the ", handName(m_hand), " hand (", path, ")",
      m_hasSlide ? ", sliding from where the held object was" : "",
      m_animation->hasCap && !m_cap ? ", its cap was not found in the model" : ""));
    return true;
  }


  void VrConsumables::detachProp(VrWeaponHand* hand) {
    if (hand)
      hand->holdInHand(m_hand, nullptr, nullptr);

    if (m_capHeld)
      dropCap();

    if (m_prop)
      m_prop->hide();

    m_prop     = nullptr;
    m_cap      = nullptr;
    m_fingers  = nullptr;
    m_hasSlide = false;
    m_parts.clear();
  }


  bool VrConsumables::takeCap(VrWeaponHand* hand, const VrGameTransform& toWorld) {
    VrHand other = otherHand(m_hand);
    void* otherBone = skeletonNode(handNode(other));
    VrGameTransform drawn;

    if (!m_cap || !m_capParent || !otherBone || !hand || !hand->freeHandBone(other, drawn)) {
      Logger::warn(str::format("VR: The cap of '", m_item.name, "' cannot follow the ", handName(other), " hand: ",
        !m_cap ? "no cap" : !m_capParent ? "the cap has no parent" : !otherBone ? "the hand bone was not found"
          : "the hand is not drawn at its controller", ", it is hidden"));
      return false;
    }

    // The cap keeps its place where both hands are drawn, now below the
    // other hand
    float handScale = field<float>(otherBone, VrGame::ObjectWorldScale);
    VrVector3 cap   = toWorld.rotate * field<VrVector3>(m_cap, VrGame::ObjectWorldTranslation) + toWorld.translate;

    VrSequencePose local;
    local.rotate    = vrTranspose(drawn.rotate) * toWorld.rotate * worldRotation(m_cap);
    local.translate = vrTranspose(drawn.rotate) * (cap - drawn.translate);
    local.translate = local.translate * (handScale > 0.0f ? 1.0f / handScale : 1.0f);
    local.scale     = handScale > 0.0f ? field<float>(m_cap, VrGame::ObjectWorldScale) / handScale : 1.0f;

    // Held while it moves from one parent to the other
    reinterpret_cast<AssignFn>(VrGame::NiPointerAssign)(&m_capHold, m_cap);
    method<DetachChildFn>(m_capParent, VrGame::NodeDetachChildSlot)(m_capParent, m_cap);

    setLocal(m_cap, local);
    method<AttachChildFn>(otherBone, VrGame::NodeAttachChildSlot)(otherBone, m_cap, 1);
    updateObject(m_cap);

    m_capHand = otherBone;
    m_capHeld = true;
    Logger::info(str::format("VR: The ", handName(other), " hand took the cap of '", m_item.name, "' off"));
    return true;
  }


  void VrConsumables::dropCap() {
    if (!m_capHeld)
      return;

    // Back on the item, hidden, for the next one
    method<DetachChildFn>(m_capHand, VrGame::NodeDetachChildSlot)(m_capHand, m_cap);
    setLocal(m_cap, m_animation->capOn);
    setCulled(m_cap, true);
    method<AttachChildFn>(m_capParent, VrGame::NodeAttachChildSlot)(m_capParent, m_cap, 1);
    reinterpret_cast<AssignFn>(VrGame::NiPointerAssign)(&m_capHold, nullptr);

    m_capHand = nullptr;
    m_capHeld = false;
  }


  void VrConsumables::biteCap() {
    // The cork's mesh sits away from its origin, at the neck: its centre in
    // the cork's own frame is what flies
    const float* bound = field<const float*>(m_cap, VrGame::ObjectWorldBound);
    float scale        = field<float>(m_cap, VrGame::ObjectWorldScale);
    m_capMesh = bound && bound[3] > 0.0f && scale > 0.0f
      ? vrTranspose(worldRotation(m_cap)) * (VrVector3 { bound[0], bound[1], bound[2] }
          - field<VrVector3>(m_cap, VrGame::ObjectWorldTranslation)) * (1.0f / scale)
      : VrVector3();

    // The item's bound keeps its place in the item while the cork flies
    void* root = m_prop->root();
    const float* itemBound = root ? field<const float*>(root, VrGame::ObjectWorldBound) : nullptr;
    float rootScale = root ? field<float>(root, VrGame::ObjectWorldScale) : 0.0f;

    m_spitBound  = VrVector3();
    m_spitRadius = 0.0f;

    if (itemBound && rootScale > 0.0f) {
      m_spitBound  = vrTranspose(worldRotation(root)) * (VrVector3 { itemBound[0], itemBound[1], itemBound[2] }
        - field<VrVector3>(root, VrGame::ObjectWorldTranslation)) * (1.0f / rootScale);
      m_spitRadius = itemBound[3];
    }

    setCulled(m_cap, true);
    queueSounds(m_animation->capSounds, 0.0f);
    queueSounds(m_animation->spitSounds, 0.0f);

    m_spitting   = true;
    m_spitFlying = false;
    m_spitTime   = 0.0f;
    m_spitDelay  = m_animation->spitSounds.front().time;
    Logger::info(str::format("VR: The teeth took the cap of '", m_item.name, "' off, it is spat out after ",
      m_spitDelay, " s"));
  }


  void VrConsumables::flySpitCap(const VrGameCameraPose& head, float dt, const VrGameTransform& toWorld) {
    constexpr float Unit = VrGameUnitsPerMetre;
    m_spitTime += dt;

    if (m_spitTime < m_spitDelay)
      return;

    float time = m_spitTime - m_spitDelay;

    // Off the lips, ahead and a little up, as the spit sound plays
    if (!m_spitFlying) {
      m_spitFrom     = vrMouthPosition(head, m_zones);
      m_spitVelocity = head.forward * (SpitSpeedAhead * Unit) + VrVector3 { 0.0f, 0.0f, SpitSpeedUp * Unit };
      m_spitFlying   = true;
      setCulled(m_cap, false);
    }

    // Hidden back on the item, out of its bound's way
    if (time > SpitFlightSeconds) {
      setLocal(m_cap, m_animation->capOn);
      setCulled(m_cap, true);
      m_spitting = false;
      return;
    }

    VrVector3 at = m_spitFrom + m_spitVelocity * time
      - VrVector3 { 0.0f, 0.0f, 0.5f * SpitGravity * Unit * time * time };

    // The cork stays below the item, whose parent is read where the left
    // hand is drawn; it tumbles as it flies
    VrGameRotation parentRotation = toWorld.rotate * worldRotation(m_capParent);
    VrVector3 parentPosition      = toWorld.rotate * field<VrVector3>(m_capParent, VrGame::ObjectWorldTranslation) + toWorld.translate;
    float parentScale             = field<float>(m_capParent, VrGame::ObjectWorldScale);

    if (parentScale <= 0.0f)
      parentScale = 1.0f;

    VrSequencePose local = m_animation->capOn;
    local.rotate = vrTranspose(parentRotation)
      * vrGameRotationFromQuaternion(vrQuaternionFromAxisAngle(head.right, -SpitSpin * time))
      * parentRotation * local.rotate;

    VrVector3 origin = at - parentRotation * (local.rotate * m_capMesh) * (parentScale * local.scale);
    local.translate  = vrTranspose(parentRotation) * (origin - parentPosition) * (1.0f / parentScale);
    setLocal(m_cap, local);
  }


  void VrConsumables::poseParts(float time) {
    for (const auto& part : m_parts)
      setLocal(part.first, VrKfAnimation::sample(part.second->track, time));
  }


  void VrConsumables::slideProp(float dt) {
    void* root = m_prop ? m_prop->root() : nullptr;

    if (!m_hasSlide || !root)
      return;

    m_slideTime += dt;
    float t = std::min(m_slideTime / SlideSeconds, 1.0f);
    t = t * t * (3.0f - 2.0f * t);

    VrSequencePose pose;
    pose.rotate    = vrGameRotationFromQuaternion(vrSlerp(
      vrQuaternionFromGameRotation(m_slideFrom.rotate), vrQuaternionFromGameRotation(m_propPose.rotate), t));
    pose.translate = m_slideFrom.translate + (m_propPose.translate - m_slideFrom.translate) * t;
    pose.scale     = m_slideFrom.scale + (m_propPose.scale - m_slideFrom.scale) * t;
    setLocal(root, pose);

    if (t >= 1.0f)
      m_hasSlide = false;
  }


  void VrConsumables::consume() {
    auto form = const_cast<uint8_t*>(m_item.form);
    uint8_t* actor = player();

    if (!actor)
      return;

    // The item must still be on its hotkey
    if (m_fromWheel) {
      std::array<VrHotKeyItem, VrGame::HotKeySlotCount> items;
      VrGameHotKeys::read(items);

      if (m_slot >= items.size() || items[m_slot].form != m_item.form) {
        Logger::warn(str::format("VR: '", m_item.name, "' left its hotkey, it is not consumed"));
        return;
      }
    }

    // The animation's sounds were played instead of the item's
    const uint8_t* sound = read<const uint8_t*>(form, VrGame::AlchemyConsumeSound);
    field<const uint8_t*>(form, VrGame::AlchemyConsumeSound) = nullptr;

    // An item picked up from the world is used as the quick wheel uses one
    bool used = true;

    if (m_fromWheel)
      used = VrGameHotKeys::use(m_slot);
    else
      reinterpret_cast<QueueEquipFn>(VrGame::ActorQueueEquipObject)(actor, form, 1, nullptr, false, false, true);

    field<const uint8_t*>(form, VrGame::AlchemyConsumeSound) = sound;

    if (!used) {
      Logger::warn(str::format("VR: '", m_item.name, "' could not be consumed"));
      return;
    }

    const char* prefix = m_animation->gesture == VrConsumeGesture::Read
      ? m_texts.text("consumable.read", "Read:")
      : m_texts.text("consumable.used", "Consumed:");

    std::string text = str::format(prefix, " ", m_item.name);
    reinterpret_cast<ShowHudMessageFn>(VrGame::ShowHudMessage)(text.c_str(), 0, nullptr, nullptr, MessageSeconds, 0);
    Logger::info(str::format("VR: '", m_item.name, "' was consumed by hand"));
  }


  void VrConsumables::finish(VrWeaponDrawn* drawn, VrWeaponHand* hand) {
    detachProp(hand);
    m_gesture.reset();
    m_spitting = false;
    m_capNow   = false;

    if (m_holstered && drawn)
      drawn->draw();

    m_holstered  = false;
    m_keepWeapon = false;
    m_animation = nullptr;
    m_state     = State::Idle;
  }


  void VrConsumables::queueSounds(const std::vector<VrConsumableSound>& sounds, float offset) {
    for (const VrConsumableSound& sound : sounds)
      m_pending.push_back({ offset + sound.time, sound.path, sound.path.empty() ? m_itemSound : nullptr });
  }


  void VrConsumables::playPending(float dt) {
    for (auto i = m_pending.begin(); i != m_pending.end(); ) {
      i->delay -= dt;

      if (i->delay > 0.0f) {
        ++i;
        continue;
      }

      if (i->sound)
        vrPlayGameSound(i->sound, VrGame::SoundFlag2D | VrGame::SoundFlagSystem | VrGame::SoundFlagOneShot, VrVector3(), 1.0f, 1.0f);
      else if (!i->path.empty())
        vrPlaySoundFile(i->path.c_str());

      i = m_pending.erase(i);
    }
  }


  void VrConsumables::playUseSounds(const VrConsumableAnimation& animation, float clock, size_t& next) {
    const std::vector<VrConsumableSound>& sounds = animation.useSounds;

    while (next < sounds.size() && sounds[next].time <= clock)
      playSound(sounds[next++]);
  }


  void VrConsumables::playSound(const VrConsumableSound& sound) {
    if (sound.path.empty()) {
      if (m_itemSound)
        vrPlayGameSound(m_itemSound, VrGame::SoundFlag2D | VrGame::SoundFlagSystem | VrGame::SoundFlagOneShot, VrVector3(), 1.0f, 1.0f);
    } else {
      vrPlaySoundFile(sound.path.c_str());
    }
  }

}
