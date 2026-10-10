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

    // The weapon node is a few levels below the skeleton's root, and the
    // muzzle a few levels below the weapon
    constexpr uint32_t MaxModelDepth = 16;

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
     * \brief Folds a subtree into one point, except the hand below it
     *
     * Skinned vertices follow the world transforms of their bones when they
     * are drawn (0x00E6FE30), so with the scale near zero the arm's vertices
     * end up on the wrist, and the hand, whose world transform was already
     * computed, keeps its shape. The next update rebuilds the transforms.
     */
    void foldBones(uint8_t* object, const uint8_t* hand, const VrVector3& wrist, uint32_t depth) {
      // Not zero, the game inverts some world transforms
      constexpr float FoldedScale = 1.0e-4f;

      if (object == hand)
        return;

      writeVector(object, VrGame::ObjectWorldTranslation, wrist);
      field<float>(object, VrGame::ObjectWorldScale) = FoldedScale;

      uintptr_t node = vrGameAsNode(reinterpret_cast<uintptr_t>(object));

      if (!node || !depth)
        return;

      auto children = readField<uint8_t* const*>(reinterpret_cast<const uint8_t*>(node), VrGame::NodeChildren);
      auto count    = readField<uint16_t>(reinterpret_cast<const uint8_t*>(node), VrGame::NodeChildCount);

      for (uint32_t i = 0; children && i < count; i++) {
        if (children[i])
          foldBones(children[i], hand, wrist, depth - 1);
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
    const VrGameCameraPose*     hand) {
    m_gameCamera = gameCamera;
    m_hasHand    = hand != nullptr;

    if (hand)
      m_hand = *hand;
    else
      m_hasAim = false;
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
      m_hasAim = false;
      return;
    }

    VrGameCameraPose hand = m_hand;
    hand.position = hand.position - shift;

    VrGameTransform rig = vrComputeRigToHand(m_gameCamera,
      readVector(weapon, VrGame::ObjectWorldTranslation), hand);

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

    hideArms(root);

    uint8_t* muzzle = findObject(weapon, VrGame::ProjectileNodeName);

    if (!muzzle)
      muzzle = findObject(weapon, VrGame::ProjectileNodeAltName);

    // Relative to the root, which the game places at the player
    m_muzzleOffset = readVector(muzzle ? muzzle : weapon, VrGame::ObjectWorldTranslation) - drawnRoot;
    m_aimDirection = m_hand.forward;
    m_hasAim       = true;

    if (!m_loggedActive) {
      Logger::info("VR: The weapon is in the right hand");
      m_loggedActive = true;
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

    for (uint32_t i = 0; i < 2; i++) {
      uint8_t* hand = hands[i];
      uint8_t* arm  = armBone(hand, hands[1 - i]);

      if (arm == hand)
        continue;

      if (!m_loggedArms) {
        auto name = field<const char*>(arm, VrGame::ObjectName);
        Logger::info(str::format("VR: Hiding the arm from '", name ? name : "", "'"));
      }

      foldBones(arm, hand, readVector(hand, VrGame::ObjectWorldTranslation), MaxModelDepth);
    }

    m_loggedArms = true;
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
