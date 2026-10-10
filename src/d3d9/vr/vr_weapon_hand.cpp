#include <algorithm>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_weapon_hand.h"

namespace dxvk {

  namespace {

    // NiAVObject::Update is __thiscall with one stack argument
    using ObjectUpdateFn = void (__fastcall*)(uint8_t* object, void* unused, void* updateData);

    // Projectile::Create is __cdecl with 16 arguments and returns the projectile
    using ProjectileCreateFn = void* (__cdecl*)(
      void* projectile, void* shooter, void* target, void* weapon,
      float x, float y, float z, float heading, float pitch,
      uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
      float spreadHeading, float spreadPitch, uint32_t a15);

    // Process virtual function, __thiscall without arguments, returns a
    // bool in AL
    using IsWeaponOutFn = uint32_t (__fastcall*)(uint8_t* process, void* unused);

    // NiObject::GetAsNiGeometry, __thiscall without arguments
    using GetAsGeometryFn = uint8_t* (__fastcall*)(uint8_t* object, void* unused);

    // The weapon node is a few levels below the skeleton's root, and the
    // muzzle a few levels below the weapon
    constexpr uint32_t MaxModelDepth = 16;

    // How close the other controller must come to where the game holds the
    // weapon with that hand for its grip to take the weapon
    constexpr float TwoHandedGripDistance = 0.15f * VrGameUnitsPerMetre;

    VrWeaponHand* g_weaponHand = nullptr;

    template<typename T>
    T& field(uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<T*>(base + offset);
    }

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    VrVector3 readVector(const uint8_t* object, uintptr_t offset) {
      VrVector3 v;
      std::memcpy(&v, object + offset, sizeof(v));
      return v;
    }

    void writeVector(uint8_t* object, uintptr_t offset, const VrVector3& v) {
      std::memcpy(object + offset, &v, sizeof(v));
    }

    VrGameRotation readRotation(const uint8_t* object, uintptr_t offset) {
      VrGameRotation r;
      std::memcpy(r.m, object + offset, sizeof(r.m));
      return r;
    }

    void writeRotation(uint8_t* object, uintptr_t offset, const VrGameRotation& r) {
      std::memcpy(object + offset, r.m, sizeof(r.m));
    }

    /**
     * \brief World transform of a node's parent, identity without a parent
     *
     * A node's world transform is its parent's applied to its local one:
     * rotation P.R * L.R, translation P.t + P.s * P.R * L.t.
     */
    struct ParentFrame {
      VrGameRotation rotate;
      VrVector3      translate;
      float          scale = 1.0f;

      VrVector3 toWorld(const VrVector3& local) const {
        return translate + (rotate * local) * scale;
      }

      VrVector3 toLocal(const VrVector3& world) const {
        return (vrTranspose(rotate) * (world - translate)) * (1.0f / scale);
      }
    };

    ParentFrame parentFrame(uint8_t* object) {
      ParentFrame frame;
      uint8_t* parent = field<uint8_t*>(object, VrGame::ObjectParent);

      if (parent) {
        frame.rotate    = readRotation(parent, VrGame::ObjectWorldRotation);
        frame.translate = readVector(parent, VrGame::ObjectWorldTranslation);
        frame.scale     = field<float>(parent, VrGame::ObjectWorldScale);
      }

      return frame;
    }

    void updateObject(uint8_t* object, void* updateData) {
      reinterpret_cast<ObjectUpdateFn>(VrGame::ObjectUpdate)(object, nullptr, updateData);
    }

    uint8_t* findObject(uint8_t* root, const char* name) {
      return reinterpret_cast<uint8_t*>(
        vrFindGameObject(reinterpret_cast<uintptr_t>(root), name, MaxModelDepth));
    }

    bool isAncestor(const uint8_t* ancestor, uint8_t* object) {
      for (; object; object = field<uint8_t*>(object, VrGame::ObjectParent)) {
        if (object == ancestor)
          return true;
      }

      return false;
    }

    /**
     * \brief Top bone of the arm holding a hand
     *
     * The highest ancestor of the hand that is not also an ancestor of the
     * other hand: the clavicle in the game's skeleton.
     */
    uint8_t* armBone(uint8_t* hand, uint8_t* otherHand) {
      uint8_t* bone = hand;

      while (uint8_t* parent = field<uint8_t*>(bone, VrGame::ObjectParent)) {
        if (isAncestor(parent, otherHand))
          break;

        bone = parent;
      }

      return bone;
    }

    /**
     * \brief Bones of one arm above the forearm
     *
     * The hand's meshes are skinned to the hand, the fingers and the
     * forearm. Only the meshes of the sleeves and the upper arms also use
     * the bones between the forearm and the clavicle.
     */
    struct UpperArm {
      uint8_t* top     = nullptr;
      uint8_t* forearm = nullptr;

      bool contains(uint8_t* bone) const {
        return top != forearm && isAncestor(top, bone) && !isAncestor(forearm, bone);
      }
    };

    uint8_t* asGeometry(uint8_t* object) {
      auto vtable = readField<const uint8_t*>(object, 0);
      auto getAsGeometry = reinterpret_cast<GetAsGeometryFn>(readField<uintptr_t>(vtable, VrGame::ObjectGetAsGeometrySlot));
      return getAsGeometry(object, nullptr);
    }

    bool isArmMesh(uint8_t* geometry, const UpperArm (&arms)[2]) {
      auto skin = field<uint8_t*>(geometry, VrGame::GeometrySkinInstance);

      if (!skin)
        return false;

      auto data  = field<uint8_t*>(skin, VrGame::SkinInstanceData);
      auto bones = field<uint8_t**>(skin, VrGame::SkinInstanceBones);

      if (!data || !bones)
        return false;

      uint32_t count = field<uint32_t>(data, VrGame::SkinDataBoneCount);

      for (uint32_t i = 0; i < count; i++) {
        if (bones[i] && (arms[0].contains(bones[i]) || arms[1].contains(bones[i])))
          return true;
      }

      return false;
    }

    template<typename Fn>
    void forEachObject(uint8_t* object, uint32_t depth, const Fn& fn) {
      fn(object);

      uintptr_t node = vrGameAsNode(reinterpret_cast<uintptr_t>(object));

      if (!node || !depth)
        return;

      auto children = readField<uint8_t* const*>(reinterpret_cast<const uint8_t*>(node), VrGame::NodeChildren);
      auto count    = readField<uint16_t>(reinterpret_cast<const uint8_t*>(node), VrGame::NodeChildCount);

      for (uint32_t i = 0; children && i < count; i++) {
        if (children[i])
          forEachObject(children[i], depth - 1, fn);
      }
    }

  }


  VrWeaponHand::~VrWeaponHand() {
    // The patched calls stay in place and fall through to the game's functions
    g_weaponHand = nullptr;
  }


  std::unique_ptr<VrWeaponHand> VrWeaponHand::install() {
    if (g_weaponHand)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::FirstPersonZeroSite,
        VrGame::FirstPersonZero, sizeof(VrGame::FirstPersonZero))
      && VrGameMemory::matches(VrGame::FirstPersonCameraSite,
        VrGame::FirstPersonCamera, sizeof(VrGame::FirstPersonCamera))
      && VrGameMemory::callsTo(VrGame::ProjectileCreateCallSite, VrGame::ProjectileCreate);

    found = found && VrGameMemory::redirectCall(VrGame::FirstPersonUpdateCallSite,
      VrGame::ObjectUpdate, reinterpret_cast<const void*>(&VrWeaponHand::updateHook));

    // If this one fails, the weapon is drawn in the hand but shoots from
    // the game's muzzle, along the head
    if (found && !VrGameMemory::redirectCall(VrGame::ProjectileCreateCallSite,
        VrGame::ProjectileCreate, reinterpret_cast<const void*>(&VrWeaponHand::createProjectileHook)))
      Logger::warn("VR: The projectile call was not found, the weapon shoots along the head");

    if (!found) {
      Logger::info("VR: The first-person model was not found, the weapon stays in front of the head");
      return nullptr;
    }

    std::unique_ptr<VrWeaponHand> weaponHand(new VrWeaponHand());
    g_weaponHand = weaponHand.get();
    return weaponHand;
  }


  void VrWeaponHand::beginFrame(
    const VrGameCameraPose&     gameCamera,
    const VrGameCameraPose*     hand,
    const VrGameCameraPose*     otherHand,
          bool                  grip) {
    m_gameCamera   = gameCamera;
    m_hasHand      = hand != nullptr;
    m_hasOtherHand = otherHand != nullptr;
    m_grip         = grip;

    if (hand)
      m_hand = *hand;
    else
      m_hasAim = false;

    if (otherHand)
      m_otherHand = *otherHand;
  }


  void VrWeaponHand::beforePlaceCamera(
          void*                 main,
    const VrVector3&            eyePosition) {
    m_main        = static_cast<uint8_t*>(main);
    m_eyePosition = eyePosition;

    uint8_t* object = player();
    uint8_t* root   = object ? field<uint8_t*>(object, VrGame::PlayerFirstPersonRoot) : nullptr;

    if (!root)
      return;

    // PlaceCamera reads the root's world position to place its camera and
    // the lighting of the model. The previous eye left it where the model
    // was drawn.
    m_rootPosition = parentFrame(root).toWorld(readVector(root, VrGame::NodeLocalTranslate));
    writeVector(root, VrGame::ObjectWorldTranslation, m_rootPosition);
  }


  void __fastcall VrWeaponHand::updateHook(
          uint8_t*              object,
          void*                 unused,
          void*                 updateData) {
    updateObject(object, updateData);

    if (g_weaponHand && g_weaponHand->m_main)
      g_weaponHand->placeModel(object, updateData);
  }


  void* __cdecl VrWeaponHand::createProjectileHook(
          void* projectile, void* shooter, void* target, void* weapon,
          float x, float y, float z, float heading, float pitch,
          uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
          float spreadHeading, float spreadPitch, uint32_t a15) {
    uint8_t* object = player();

    if (g_weaponHand && g_weaponHand->m_hasAim && object && shooter == object
     && !*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState)) {
      if (uint8_t* root = field<uint8_t*>(object, VrGame::PlayerFirstPersonRoot)) {
        VrVector3 muzzle = parentFrame(root).toWorld(readVector(root, VrGame::NodeLocalTranslate))
                         + g_weaponHand->m_muzzleOffset;
        VrHeadAngles aim = vrComputeAimAngles(g_weaponHand->m_aimDirection);

        x = muzzle.x;
        y = muzzle.y;
        z = muzzle.z;
        heading = aim.yaw + spreadHeading;
        pitch   = aim.pitch + spreadPitch;
      }
    }

    return reinterpret_cast<ProjectileCreateFn>(VrGame::ProjectileCreate)(
      projectile, shooter, target, weapon, x, y, z, heading, pitch,
      a9, a10, a11, a12, spreadHeading, spreadPitch, a15);
  }


  void VrWeaponHand::placeModel(uint8_t* root, void* updateData) {
    uint8_t* main = m_main;
    m_main = nullptr;

    uint8_t* object = player();

    if (!object || root != field<uint8_t*>(object, VrGame::PlayerFirstPersonRoot))
      return;

    // The game draws the model with the root's translation at zero, so
    // that its camera and lights sit near the origin. Positions in the
    // world are brought to that space by the same shift.
    ParentFrame parent = parentFrame(root);
    VrVector3 drawnRoot = readVector(root, VrGame::ObjectWorldTranslation);
    VrVector3 shift     = m_rootPosition - drawnRoot;

    if (!m_loggedFrame) {
      auto parentObject = field<uint8_t*>(root, VrGame::ObjectParent);
      auto parentName   = parentObject ? field<const char*>(parentObject, VrGame::ObjectName) : nullptr;

      Logger::info(str::format("VR: First-person model parent '", parentName ? parentName : "", "' at ",
        parent.translate.x, ", ", parent.translate.y, ", ", parent.translate.z, " scale ", parent.scale,
        ", drawn ", vrLength(shift), " units from the player"));
      m_loggedFrame = true;
    }

    // The game places its camera at its own eye
    if (uint8_t* camera = field<uint8_t*>(main, VrGame::MainFirstPersonCamera))
      writeVector(camera, VrGame::CameraWorldTranslation, m_eyePosition - shift);

    uint8_t* weapon = m_hasHand && holdsWeapon(object)
      ? findObject(root, VrGame::WeaponNodeName) : nullptr;

    if (!weapon) {
      m_hasAim    = false;
      m_twoHanded = false;
      showArms(root);
      return;
    }

    // Where the game animates the weapon and the other hand, before the
    // model is moved
    uint8_t* otherHand = findObject(root, VrGame::LeftHandNodeName);
    uint8_t* forearm   = otherHand ? field<uint8_t*>(otherHand, VrGame::ObjectParent) : nullptr;

    VrVector3 drawnWeapon = readVector(weapon, VrGame::ObjectWorldTranslation);
    VrVector3 drawnHand   = otherHand ? readVector(otherHand, VrGame::ObjectWorldTranslation) : drawnWeapon;

    VrGameTransform drawnForearm;

    if (forearm) {
      drawnForearm.rotate    = readRotation(forearm, VrGame::ObjectWorldRotation);
      drawnForearm.translate = readVector(forearm, VrGame::ObjectWorldTranslation);
    }

    VrGameCameraPose hand = m_hand;
    hand.position = hand.position - shift;

    VrGameTransform rig = vrComputeRigToHand(m_gameCamera, drawnWeapon, hand);

    // With both hands on the weapon, it points along the line between them
    VrVector3 support = rig.rotate * (drawnHand - drawnWeapon);
    updateTwoHanded(forearm != nullptr, support);

    if (m_twoHanded) {
      hand = vrComputeTwoHandedPose(hand, support, m_otherHand.position - m_hand.position);
      rig  = vrComputeRigToHand(m_gameCamera, drawnWeapon, hand);
    }

    // The new world transform of the root, brought back to its parent's frame
    VrGameRotation worldRotation = rig.rotate * readRotation(root, VrGame::ObjectWorldRotation);
    VrVector3 worldTranslation   = rig.rotate * drawnRoot + rig.translate;

    VrGameRotation rotation = readRotation(root, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(root, VrGame::NodeLocalTranslate);

    writeRotation(root, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * worldRotation);
    writeVector(root, VrGame::NodeLocalTranslate, parent.toLocal(worldTranslation));
    updateObject(root, updateData);

    // PlaceCamera restores the translation itself
    writeRotation(root, VrGame::ObjectLocalRotation, rotation);
    writeVector(root, VrGame::NodeLocalTranslate, translation);

    if (forearm && m_hasOtherHand && !m_twoHanded)
      placeOtherHand(forearm, drawnForearm, drawnHand, shift, updateData);

    hideArms(root);

    uint8_t* muzzle = findObject(weapon, VrGame::ProjectileNodeName);

    if (!muzzle)
      muzzle = findObject(weapon, VrGame::ProjectileNodeAltName);

    // Relative to the root, which the game places at the player
    m_muzzleOffset = readVector(muzzle ? muzzle : weapon, VrGame::ObjectWorldTranslation) - drawnRoot;
    m_aimDirection = hand.forward;
    m_hasAim       = true;

    if (!m_loggedActive) {
      Logger::info("VR: The weapon is in the right hand");
      m_loggedActive = true;
    }
  }


  void VrWeaponHand::updateTwoHanded(bool canHold, const VrVector3& support) {
    if (!canHold || !m_hasOtherHand || !m_grip) {
      m_twoHanded = false;
      return;
    }

    // The grip takes the weapon when the controller comes near where the
    // game holds it, and keeps it until it is released
    if (!m_twoHanded)
      m_twoHanded = vrLength(m_hand.position + support - m_otherHand.position) <= TwoHandedGripDistance;

    if (m_twoHanded && !m_loggedTwoHanded) {
      Logger::info("VR: The weapon is held with both hands");
      m_loggedTwoHanded = true;
    }
  }


  void VrWeaponHand::placeOtherHand(
          uint8_t*              forearm,
    const VrGameTransform&      drawnForearm,
    const VrVector3&            drawnHand,
    const VrVector3&            shift,
          void*                 updateData) {
    // Like the whole model for the weapon hand: what the game animates
    // relative to its camera is kept relative to the controller, and the
    // hand bone lands on it. The forearm carries the hand, and the hand's
    // meshes only use bones below the forearm.
    VrGameCameraPose hand = m_otherHand;
    hand.position = hand.position - shift;

    VrGameTransform rig = vrComputeRigToHand(m_gameCamera, drawnHand, hand);

    VrGameRotation worldRotation = rig.rotate * drawnForearm.rotate;
    VrVector3 worldTranslation   = rig.rotate * drawnForearm.translate + rig.translate;

    ParentFrame parent = parentFrame(forearm);

    VrGameRotation rotation = readRotation(forearm, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(forearm, VrGame::NodeLocalTranslate);

    writeRotation(forearm, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * worldRotation);
    writeVector(forearm, VrGame::NodeLocalTranslate, parent.toLocal(worldTranslation));
    updateObject(forearm, updateData);

    // The next eye's update starts from the game's pose again
    writeRotation(forearm, VrGame::ObjectLocalRotation, rotation);
    writeVector(forearm, VrGame::NodeLocalTranslate, translation);

    if (!m_loggedOtherHand) {
      auto name = field<const char*>(forearm, VrGame::ObjectName);
      Logger::info(str::format("VR: The left hand follows its controller, moved by '", name ? name : "", "'"));
      m_loggedOtherHand = true;
    }
  }


  void VrWeaponHand::hideArms(uint8_t* root) {
    uint8_t* hands[2] = {
      findObject(root, VrGame::LeftHandNodeName),
      findObject(root, VrGame::RightHandNodeName) };

    if (!hands[0] || !hands[1]) {
      if (!m_loggedArms)
        Logger::warn("VR: The hand bones were not found, the arms stay visible");

      m_loggedArms = true;
      return;
    }

    UpperArm arms[2];

    for (uint32_t i = 0; i < 2; i++) {
      arms[i].top     = armBone(hands[i], hands[1 - i]);
      arms[i].forearm = arms[i].top != hands[i] ? field<uint8_t*>(hands[i], VrGame::ObjectParent) : hands[i];

      if (!m_loggedArms) {
        auto top     = field<const char*>(arms[i].top, VrGame::ObjectName);
        auto forearm = field<const char*>(arms[i].forearm, VrGame::ObjectName);
        Logger::info(str::format("VR: Hiding the meshes skinned between '", top ? top : "",
          "' and '", forearm ? forearm : "", "'"));
      }
    }

    // The meshes are culled with the first-person camera after this update
    forEachObject(root, MaxModelDepth, [this, &arms] (uint8_t* object) {
      uint8_t* geometry = asGeometry(object);

      if (!geometry || !isArmMesh(geometry, arms))
        return;

      field<uint32_t>(geometry, VrGame::ObjectFlags) |= VrGame::ObjectFlagAppCulled;

      if (std::find(m_hiddenMeshes.begin(), m_hiddenMeshes.end(), geometry) != m_hiddenMeshes.end())
        return;

      m_hiddenMeshes.push_back(geometry);

      if (!m_loggedArms) {
        auto name = field<const char*>(geometry, VrGame::ObjectName);
        Logger::info(str::format("VR: Hiding the arm mesh '", name ? name : "", "'"));
      }
    });

    if (!m_loggedArms && m_hiddenMeshes.empty())
      Logger::warn("VR: No arm mesh was found, the arms stay visible");

    m_loggedArms = true;
  }


  void VrWeaponHand::showArms(uint8_t* root) {
    if (m_hiddenMeshes.empty())
      return;

    // Meshes no longer in the model may have been freed, so only the ones
    // still found in it are touched
    forEachObject(root, MaxModelDepth, [this] (uint8_t* object) {
      if (std::find(m_hiddenMeshes.begin(), m_hiddenMeshes.end(), object) != m_hiddenMeshes.end())
        field<uint32_t>(object, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;
    });

    m_hiddenMeshes.clear();
  }


  bool VrWeaponHand::holdsWeapon(const uint8_t* player) const {
    if (readField<uint8_t>(player, VrGame::PlayerThirdPerson))
      return false;

    if (*reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState))
      return false;

    auto process = readField<uint8_t*>(player, VrGame::ActorProcess);

    if (!process)
      return false;

    auto vtable = readField<const uint8_t*>(process, 0);
    auto isWeaponOut = reinterpret_cast<IsWeaponOutFn>(readField<uintptr_t>(vtable, VrGame::ProcessIsWeaponOutSlot));
    return (isWeaponOut(process, nullptr) & 0xFF) != 0;
  }

}
