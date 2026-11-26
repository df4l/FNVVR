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

    return std::unique_ptr<VrConsumables>(new VrConsumables(texts));
  }


  VrConsumables::VrConsumables(const VrTexts& texts)
  : m_texts(texts) {

  }


  VrConsumables::~VrConsumables() {
    // The models are left to the game, which may be shutting down
  }


  bool VrConsumables::begin(uint32_t slot, const VrHotKeyItem& item) {
    if (busy() || !item.present || !item.form || item.formType != VrGame::FormTypeAlchemy)
      return false;

    // A poison goes on the weapon
    if (reinterpret_cast<CanBePoisonFn>(VrGame::EffectItemListCanBePoison)(
        item.form + VrGame::AlchemyEffectList, nullptr) & 0xFF)
      return false;

    const VrConsumableAnimation* animation = findAnimation(item.form);

    if (!animation || animation->gesture == VrConsumeGesture::Instant)
      return false;

    m_state       = State::Holstering;
    m_slot        = slot;
    m_item        = item;
    m_itemSound   = read<const uint8_t*>(item.form, VrGame::AlchemyConsumeSound);
    m_animation   = animation;
    m_inHand      = { animation->inHand.rotate, animation->inHand.translate };
    m_holstered   = false;
    m_waited      = 0.0f;

    Logger::info(str::format("VR: '", item.name, "' comes into the left hand to be consumed"));
    return true;
  }


  VrConsumablesEvent VrConsumables::update(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand) {
    playPending(input.dt);

    if (m_state == State::Idle)
      return VrConsumablesEvent::None;

    if (!input.inGame) {
      Logger::info(str::format("VR: '", m_item.name, "' goes back to the inventory, a menu opened"));
      finish(drawn, hand);
      return VrConsumablesEvent::Cancelled;
    }

    if (m_state == State::Holstering) {
      if (drawn && !drawn->handsFree()) {
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

      if (!attachProp(hand)) {
        Logger::warn(str::format("VR: The model of '", m_item.name, "' was not attached, it is used at once"));
        finish(drawn, hand);
        VrGameHotKeys::use(m_slot);
        return VrConsumablesEvent::Consumed;
      }

      m_gesture.start(m_animation->gesture, m_cap != nullptr, m_animation->useSeconds());
      m_clockPlayed  = 0.0f;
      m_nextUseSound = 0;
      queueSounds(m_animation->drawSounds, 0.0f);
      poseParts(m_animation->useStart);
      m_state = State::Holding;
      return VrConsumablesEvent::Started;
    }

    // The item's bound, and the cap where it is on the item
    VrConsumeInput gesture;
    gesture.head      = input.head;
    gesture.otherHand = input.otherHand;
    gesture.grip      = input.grip;
    gesture.otherGrip = input.otherGrip;
    gesture.dt        = input.dt;

    if (const float* bound = m_prop && m_prop->root() ? field<const float*>(m_prop->root(), VrGame::ObjectWorldBound) : nullptr) {
      gesture.centre = { bound[0], bound[1], bound[2] };
      gesture.radius = bound[3];
    }

    if (m_cap)
      gesture.cap = field<VrVector3>(m_cap, VrGame::ObjectWorldTranslation);

    VrConsumeEvent event = m_gesture.update(gesture, m_zones);

    switch (event) {
      case VrConsumeEvent::CapTaken:
        takeCap();
        queueSounds(m_animation->capSounds, 0.0f);
        return VrConsumablesEvent::CapTaken;

      case VrConsumeEvent::CapDropped:
        dropCap();
        break;

      case VrConsumeEvent::Cancelled:
        Logger::info(str::format("VR: '", m_item.name, "' was let go, it goes back to the inventory"));
        finish(drawn, hand);
        return VrConsumablesEvent::Cancelled;

      default:
        break;
    }

    // The animation's sounds and parts follow the use clock
    float clock = m_gesture.clock();
    const std::vector<VrConsumableSound>& sounds = m_animation->useSounds;

    while (m_nextUseSound < sounds.size() && sounds[m_nextUseSound].time <= clock)
      playSound(sounds[m_nextUseSound++]);

    poseParts(m_animation->useTime(clock));

    if (event != VrConsumeEvent::Consumed)
      return VrConsumablesEvent::None;

    // The sounds after the use play on
    for (; m_nextUseSound < sounds.size(); m_nextUseSound++) {
      VrConsumableSound late = sounds[m_nextUseSound];
      late.time -= clock;
      queueSounds({ late }, 0.0f);
    }

    consume();
    finish(drawn, hand);
    return VrConsumablesEvent::Consumed;
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
    void* leftHand = skeletonNode(VrGame::LeftHandNodeName);

    if (!leftHand)
      return false;

    std::string model = m_animation->prop;

    if (model.empty()) {
      const char* own = read<const char*>(m_item.form, VrGame::AlchemyModelPath);

      if (!own || !*own)
        return false;

      model = own;
    }

    std::string path = "Meshes\\" + model;
    std::unique_ptr<VrSceneModel>& prop = m_props[path];

    if (!prop)
      prop = VrSceneModel::create(path.c_str());

    if (!prop || !prop->attach(leftHand, m_animation->inHand))
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
    }

    if (hand)
      hand->holdInLeftHand(&m_inHand, &m_animation->fingers);

    Logger::info(str::format("VR: '", m_item.name, "' is in the left hand (", path, ")",
      m_animation->hasCap && !m_cap ? ", its cap was not found in the model" : ""));
    return true;
  }


  void VrConsumables::detachProp(VrWeaponHand* hand) {
    if (hand)
      hand->holdInLeftHand(nullptr, nullptr);

    if (m_capHeld)
      dropCap();

    if (m_prop)
      m_prop->hide();

    m_prop   = nullptr;
    m_cap    = nullptr;
    m_parts.clear();
  }


  void VrConsumables::takeCap() {
    void* rightHand = skeletonNode(VrGame::RightHandNodeName);

    if (!m_cap || !m_capParent || !rightHand)
      return;

    // The cap keeps its place in the world, now below the right hand
    VrGameRotation handRotation = worldRotation(rightHand);
    VrVector3 handPosition      = field<VrVector3>(rightHand, VrGame::ObjectWorldTranslation);
    float handScale             = field<float>(rightHand, VrGame::ObjectWorldScale);

    VrSequencePose local;
    local.rotate    = vrTranspose(handRotation) * worldRotation(m_cap);
    local.translate = vrTranspose(handRotation) * (field<VrVector3>(m_cap, VrGame::ObjectWorldTranslation) - handPosition);
    local.translate = local.translate * (handScale > 0.0f ? 1.0f / handScale : 1.0f);
    local.scale     = handScale > 0.0f ? field<float>(m_cap, VrGame::ObjectWorldScale) / handScale : 1.0f;

    // Held while it moves from one parent to the other
    reinterpret_cast<AssignFn>(VrGame::NiPointerAssign)(&m_capHold, m_cap);
    method<DetachChildFn>(m_capParent, VrGame::NodeDetachChildSlot)(m_capParent, m_cap);

    setLocal(m_cap, local);
    method<AttachChildFn>(rightHand, VrGame::NodeAttachChildSlot)(rightHand, m_cap, 1);
    updateObject(m_cap);

    m_capHand = rightHand;
    m_capHeld = true;
    Logger::info(str::format("VR: The right hand took the cap of '", m_item.name, "' off"));
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


  void VrConsumables::poseParts(float time) {
    for (const auto& part : m_parts)
      setLocal(part.first, VrKfAnimation::sample(part.second->track, time));
  }


  void VrConsumables::consume() {
    // The item must still be on its hotkey
    std::array<VrHotKeyItem, VrGame::HotKeySlotCount> items;
    VrGameHotKeys::read(items);

    if (m_slot >= items.size() || items[m_slot].form != m_item.form) {
      Logger::warn(str::format("VR: '", m_item.name, "' left its hotkey, it is not consumed"));
      return;
    }

    // The animation's sounds were played instead of the item's
    auto form = const_cast<uint8_t*>(m_item.form);
    const uint8_t* sound = read<const uint8_t*>(form, VrGame::AlchemyConsumeSound);
    field<const uint8_t*>(form, VrGame::AlchemyConsumeSound) = nullptr;

    bool used = VrGameHotKeys::use(m_slot);

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

    if (m_holstered && drawn)
      drawn->draw();

    m_holstered = false;
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


  void VrConsumables::playSound(const VrConsumableSound& sound) {
    if (sound.path.empty()) {
      if (m_itemSound)
        vrPlayGameSound(m_itemSound, VrGame::SoundFlag2D | VrGame::SoundFlagSystem | VrGame::SoundFlagOneShot, VrVector3(), 1.0f, 1.0f);
    } else {
      vrPlaySoundFile(sound.path.c_str());
    }
  }

}
