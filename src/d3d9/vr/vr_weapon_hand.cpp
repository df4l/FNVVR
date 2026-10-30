#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_hand_fingers.h"
#include "vr_melee.h"
#include "vr_throw.h"
#include "vr_weapon_hand.h"
#include "vr_weapon_parts.h"

namespace dxvk {

  namespace {

    // NiAVObject::Update is __thiscall with one stack argument
    using ObjectUpdateFn = void (__fastcall*)(uint8_t* object, void* unused, void* updateData);

    // NiPSysEmitterCtlr::EmitParticles, __thiscall with six stack arguments
    using EmitParticlesFn = void (__fastcall*)(uint8_t* controller, void* unused,
      float time, float current, float last, float start, float stop, float rate);

    // NiParticleSystem::UpdateSystem, virtual, __thiscall with the time
    using UpdateSystemFn = void (__fastcall*)(uint8_t* system, void* unused, float time);

    // Particles per second of a lit fuse whose animation gives none
    constexpr float DefaultSparkRate = 30.0f;

    // Projectile::Create is __cdecl with 16 arguments and returns the projectile
    using ProjectileCreateFn = void* (__cdecl*)(
      void* projectile, void* shooter, void* target, void* weapon,
      float x, float y, float z, float heading, float pitch,
      uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
      float spreadHeading, float spreadPitch, uint32_t a15);

    // Projectile::GetSpeed, __thiscall without arguments, returns a float
    using ProjectileGetSpeedFn = float (__fastcall*)(uint8_t* projectile, void* unused);

    // Process3D of a flame or beam projectile is __thiscall without arguments
    using ProjectileProcess3DFn = void (__fastcall*)(uint8_t* projectile, void* unused);

    // UpdateProjectile of a flame or beam is __thiscall with the frame time
    using ProjectileUpdateFn = void (__fastcall*)(uint8_t* projectile, void* unused, float dt);

    // TESObjectREFR::GetProjectileNode, __thiscall without arguments
    using GetProjectileNodeFn = uint8_t* (__fastcall*)(uint8_t* reference, void* unused);

    // PlayerCharacter::ApplyAutoAimActor is __thiscall with 7 stack arguments
    using ApplyAutoAimFn = void (__fastcall*)(uint8_t* player, void* unused,
      void* projectile, void* releaseNode, float* heading, float* pitch,
      void* origin, float startHeading, float startPitch);

    // Process virtual function, __thiscall without arguments, returns a
    // bool in AL
    using IsWeaponOutFn = uint32_t (__fastcall*)(uint8_t* process, void* unused);

    // NiObject::GetAsNiGeometry, __thiscall without arguments
    using GetAsGeometryFn = uint8_t* (__fastcall*)(uint8_t* object, void* unused);

    // Process virtual function, __thiscall without arguments, returns the
    // equipped weapon's inventory entry
    using GetWeaponInfoFn = uint8_t* (__fastcall*)(uint8_t* process, void* unused);

    // AnimData::MorphOrBlendToSequence, __thiscall with three stack
    // arguments, returns the sequence
    using PlaySequenceFn = void* (__fastcall*)(uint8_t* animData, void* unused,
      void* sequence, uint32_t groupId, int32_t sequenceType);

    // The weapon node is a few levels below the skeleton's root, and the
    // muzzle a few levels below the weapon
    constexpr uint32_t MaxModelDepth = 16;

    // How close the other controller must come to where the game holds the
    // weapon with that hand for its grip to take the weapon
    constexpr float TwoHandedGripDistance = 0.15f * VrGameUnitsPerMetre;

    // How close the controllers must be for the other hand to support a
    // one-handed weapon
    constexpr float BracedGripDistance = 0.2f * VrGameUnitsPerMetre;

    // Animation types of the game's weapons, see findings/weapon.md
    constexpr int8_t WeaponTypeNone         = -1;
    constexpr int8_t WeaponTypeHandToHand   = 0;
    constexpr int8_t WeaponTypeOneHandMelee = 1;
    constexpr int8_t WeaponTypeTwoHandMelee = 2;
    constexpr int8_t WeaponTypePistol       = 3;
    constexpr int8_t WeaponTypePistolEnergy = 4;
    constexpr int8_t WeaponTypeRifle        = 5;
    constexpr int8_t WeaponTypeLauncher     = 9;
    constexpr int8_t WeaponTypeGrenade      = 10;
    constexpr int8_t WeaponTypeThrown       = 13;

    // How long the magazine takes to move between the hand and its well
    constexpr float MagazineBlendTime = 0.12f;

    // How long a break action takes to swing open or closed
    constexpr float ActionSwingTime = 0.15f;

    // The bones a round is held between: the last joints of the thumb
    // and of the index finger
    constexpr char PinchThumbBone[] = "Bip01 L Thumb12";
    constexpr char PinchIndexBone[] = "Bip01 L Finger12";

    // How far from the left hand bone, in metres, the reload animation may
    // hold the new magazine's centre and still carry it: the Gatling
    // laser's never comes within 50 cm of the hand, the minigun's box is
    // under 20 cm away
    constexpr float MagazineHandReach = 0.3f;

    /// Meshes of the body hidden that are named in the log, at most
    constexpr uint32_t MaxLoggedBodyMeshes = 32;

    // How far out along its way in a round enters the loading port, in
    // metres, and the part of the time to its place it takes to get there
    // from the fingers
    constexpr float RoundPortEntrance = 0.04f;
    constexpr float RoundEntranceShare = 0.5f;

    // Without a reload animation that racks it, the rack part is pulled
    // straight back along the barrel (the weapon node's -x), this far in
    // metres
    constexpr VrVector3 FallbackRackAxis   = { -1.0f, 0.0f, 0.0f };
    constexpr float     FallbackRackTravel = 0.04f;

    // How many times the reload animation of a weapon is read, in case its
    // animations are not loaded yet, and how many draws apart (two per frame)
    constexpr uint32_t ReloadAnimationTries      = 3;
    constexpr uint32_t ReloadAnimationRetryDraws = 180;

    // The same for the attack animation of a grenade or mine
    constexpr uint32_t ThrowAnimationTries       = 3;

    // How far the release of a thrown weapon's attack animation turns it
    // in the hand from the idle pose, in radians (75 degrees), for the
    // hand to hold it as at the release: a spear's idle holds it as a cane
    constexpr float ThrownRegripTurn = 1.3089969f;

    // Bones of the free hand that take a pose: its fingers, the wrist and
    // the forearm's twist bone
    constexpr uint32_t MaxPosedBones = 48;

    constexpr float Pi               = 3.14159265f;
    constexpr float DegreesPerRadian = 180.0f / Pi;

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

    /**
     * \brief The particle emitter controller of an object, or \c nullptr
     */
    uint8_t* emitterController(uint8_t* object) {
      // A model's objects carry a handful of controllers at most
      constexpr uint32_t MaxControllers = 16;
      auto controller = field<uint8_t*>(object, VrGame::ObjectControllers);

      for (uint32_t i = 0; controller && i < MaxControllers; i++) {
        if (field<uintptr_t>(controller, 0) == VrGame::PSysEmitterCtlrVtable)
          return controller;

        controller = field<uint8_t*>(controller, VrGame::ControllerNext);
      }

      return nullptr;
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

    /**
     * \brief The model's node that holds a bone of its skeleton: the
     *    skeleton's root bone
     */
    uint8_t* skeletonRoot(const uint8_t* root, uint8_t* bone) {
      while (uint8_t* parent = field<uint8_t*>(bone, VrGame::ObjectParent)) {
        if (parent == root)
          return bone;

        bone = parent;
      }

      return nullptr;
    }

    /**
     * \brief Whether a mesh belongs to the body, away from the arms
     *
     * The game's first-person camera never sees the body, which the VR
     * layer moves with the hands: some outfits add pieces of their own to
     * the first-person model (legs, belts, pouches, a face wrap; the NCR
     * armours). A skinned mesh is the body's when none of its bones is a
     * hand's or a forearm's and one lies outside both arms; another mesh
     * when it hangs from the skeleton outside both arms, the weapon aside.
     * The hands and what they hold stay.
     */
    bool isBodyMesh(uint8_t* geometry, const UpperArm (&arms)[2], const uint8_t* skeleton, const uint8_t* weapon) {
      auto skin = field<uint8_t*>(geometry, VrGame::GeometrySkinInstance);

      if (!skin) {
        return skeleton && isAncestor(skeleton, geometry)
          && !isAncestor(arms[0].top, geometry) && !isAncestor(arms[1].top, geometry)
          && !(weapon && isAncestor(weapon, geometry));
      }

      auto data  = field<uint8_t*>(skin, VrGame::SkinInstanceData);
      auto bones = field<uint8_t**>(skin, VrGame::SkinInstanceBones);

      if (!data || !bones)
        return false;

      uint32_t count = field<uint32_t>(data, VrGame::SkinDataBoneCount);
      bool outside = false;

      for (uint32_t i = 0; i < count; i++) {
        if (!bones[i])
          continue;

        if (isAncestor(arms[0].forearm, bones[i]) || isAncestor(arms[1].forearm, bones[i]))
          return false;

        if (!isAncestor(arms[0].top, bones[i]) && !isAncestor(arms[1].top, bones[i]))
          outside = true;
      }

      return outside;
    }

    /**
     * \brief Whether a mesh is skinned to a bone or to one below it
     */
    bool isSkinnedBelow(uint8_t* geometry, const uint8_t* bone) {
      auto skin = field<uint8_t*>(geometry, VrGame::GeometrySkinInstance);

      if (!skin)
        return false;

      auto data  = field<uint8_t*>(skin, VrGame::SkinInstanceData);
      auto bones = field<uint8_t**>(skin, VrGame::SkinInstanceBones);

      if (!data || !bones)
        return false;

      uint32_t count = field<uint32_t>(data, VrGame::SkinDataBoneCount);

      for (uint32_t i = 0; i < count; i++) {
        if (bones[i] && isAncestor(bone, bones[i]))
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

    /**
     * \brief Pose of the free left hand, relative to its controller
     *
     * Measured in game with the 9mm pistol (2026-10-11), on the hand the
     * VR layer then held as the mirror image of the right hand: the hand
     * bone's rotation and offset in the left controller's frame (forward,
     * up and right, as in vrCameraRotation, in game units). The fingers'
     * open pose, measured with it, is in vr_hand_fingers.cpp. Every weapon
     * gets this pose: some,
     * such as the Fat Man, hold the weapon with an unusual grip that a
     * mirror of the right hand would copy.
     *
     * The wrist comes from a second measurement with the 9mm pistol: the
     * hand's and the forearm twist bone's local rotations in the forearm,
     * which the Fat Man's animation bends and twists otherwise.
     */
    const VrGameTransform FreeHandPose = {
      {{ {  0.995157f, 0.0949835f, 0.0253174f },
         { 0.0288025f, -0.035501f, -0.998955f },
         { -0.0939852f,  0.994846f, -0.0380647f } }},
      { -8.81553f, -1.23918f, -1.42719f },
    };

    // The controller's up, in its frame (forward, up and right)
    const VrVector3 ControllerUp = { 0.0f, 1.0f, 0.0f };

    /**
     * \brief Pose of the lighter of the dynamite and the fire bomb in the
     *    left hand, relative to its controller
     *
     * In the controller's frame (forward, up and right, as in
     * vrCameraRotation, in game units): the lighter stands up in the
     * closed hand, its node's y axis (towards its flame) along the
     * controller's up and its x axis pointing forward, a little above
     * the controller's centre, behind it and to its left so that the
     * thumb, middle and ring fingers close around it. With its x axis
     * along the controller's right or pointing back, or centred on the
     * controller, the fingers went through it.
     */
    const VrGameTransform LighterInHand = {
      {{ { 1.0f, 0.0f, 0.0f },
         { 0.0f, 1.0f, 0.0f },
         { 0.0f, 0.0f, 1.0f } }},
      { -0.035f * VrGameUnitsPerMetre, 0.015f * VrGameUnitsPerMetre, -0.02f * VrGameUnitsPerMetre },
    };

    struct FreeHandBone {
      const char*    name;
      VrGameRotation rotate;
    };

    const VrGameRotation FreeHandWrist = {{
      {  0.992455f,  0.114828f, -0.0429711f },
      { 0.0934869f, -0.481991f,  0.871174f },
      { 0.0793239f, -0.868619f,  -0.48909f } }};

    const VrGameRotation FreeHandTwist = {{
      {       1.0f,       0.0f,       0.0f },
      {       0.0f,  0.850957f,  0.525236f },
      {       0.0f, -0.525236f,  0.850957f } }};

    const FreeHandBone FreeHandBones[] = {
      { VrGame::LeftHandNodeName, FreeHandWrist },
      { VrGame::LeftForeTwistNodeName, FreeHandTwist },
    };

    /**
     * \brief The local rotation placeOtherHand gives a bone of the left
     *    forearm
     *
     * \param [in] grip The fingers' rotations on a held part, or \c nullptr
     * \param [in] curls Curl of the fingers the grip does not pose
     * \param [out] rotate The bone's rotation
     * \returns \c false if the bone keeps the game's rotation
     */
    bool freeHandRotation(const char* name, const std::vector<VrBonePose>* grip,
        const VrFingerCurls& curls, VrGameRotation& rotate) {
      for (size_t i = 0; grip && i < grip->size(); i++) {
        if ((*grip)[i].name == name) {
          rotate = (*grip)[i].rotate;
          return true;
        }
      }

      for (const FreeHandBone& bone : FreeHandBones) {
        if (!std::strcmp(name, bone.name)) {
          rotate = bone.rotate;
          return true;
        }
      }

      VrHand hand;
      uint32_t index;

      if (!vrFindFingerBone(name, hand, index) || hand != VrHand::Left)
        return false;

      rotate = vrPoseFingerBone(index, hand, curls);
      return true;
    }

    /**
     * \brief A bone's local rotation, kept to be written back
     */
    struct SavedBone {
      uint8_t*       bone;
      VrGameRotation rotate;
    };

    /**
     * \brief Curls the fingers below the right hand bone as the player's are
     *
     * \param [in] hand The right hand bone
     * \param [in] curls The player's finger curls
     * \param [out] fingers The bones' local rotations before, to write back
     * \returns How many bones were posed
     */
    uint32_t poseRightFingers(uint8_t* hand, const VrFingerCurls& curls,
        std::array<SavedBone, VrFingerBoneCount>& fingers) {
      uint32_t count = 0;

      forEachObject(hand, MaxModelDepth, [&curls, &fingers, &count] (uint8_t* object) {
        auto name = field<const char*>(object, VrGame::ObjectName);
        VrHand side;
        uint32_t index;

        if (!name || count == fingers.size() || !vrFindFingerBone(name, side, index) || side != VrHand::Right)
          return;

        fingers[count++] = { object, readRotation(object, VrGame::ObjectLocalRotation) };
        writeRotation(object, VrGame::ObjectLocalRotation, vrPoseFingerBone(index, side, curls));
      });

      return count;
    }

    /**
     * \brief Where a bone below the left hand is in the hand bone's frame
     *
     * With the bones between them posed as placeOtherHand poses them, in
     * units of the hand's scale.
     */
    bool boneInHand(uint8_t* hand, uint8_t* bone, const std::vector<VrBonePose>* grip, VrVector3& point) {
      point = VrVector3();

      for (; bone && bone != hand; bone = field<uint8_t*>(bone, VrGame::ObjectParent)) {
        auto name = field<const char*>(bone, VrGame::ObjectName);
        VrGameRotation rotate;

        if (!name || !freeHandRotation(name, grip, VrFingerCurls(), rotate))
          rotate = readRotation(bone, VrGame::ObjectLocalRotation);

        point = readVector(bone, VrGame::NodeLocalTranslate)
          + rotate * point * field<float>(bone, VrGame::ObjectLocalScale);
      }

      return bone == hand;
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

    // If this one fails, the Pip-Boy's screen is drawn where the game
    // animates the forearm, while the forearm is drawn on the controller
    if (found && !VrGameMemory::redirectCall(VrGame::PipBoyModelUpdateCallSite,
        VrGame::ObjectUpdate, reinterpret_cast<const void*>(&VrWeaponHand::pipBoyUpdateHook)))
      Logger::warn("VR: The Pip-Boy's model update was not found, its screen stays where the game animates it");

    // If this one fails, the weapon is drawn in the hand but shoots from
    // the game's muzzle, along the head
    if (found && !VrGameMemory::redirectCall(VrGame::ProjectileCreateCallSite,
        VrGame::ProjectileCreate, reinterpret_cast<const void*>(&VrWeaponHand::createProjectileHook)))
      Logger::warn("VR: The projectile call was not found, the weapon shoots along the head");

    // If this one fails, a grenade thrown by hand flies as fast as the
    // game's own throw, whatever the hand's speed
    if (found && !VrGameMemory::redirectCall(VrGame::ProjectileSpeedCallSite,
        VrGame::ProjectileGetSpeed, reinterpret_cast<const void*>(&VrWeaponHand::projectileSpeedHook)))
      Logger::warn("VR: The grenade speed call was not found, grenades thrown by hand fly at the game's speed");

    // If these fail, flames come from in front of the head
    if (found && !(VrGameMemory::redirectVirtual(VrGame::FlameProcess3DSlot,
          VrGame::FlameProcess3D, reinterpret_cast<const void*>(&VrWeaponHand::flameProcess3DHook))
        && VrGameMemory::redirectVirtual(VrGame::FlameUpdateSlot,
          VrGame::FlameUpdate, reinterpret_cast<const void*>(&VrWeaponHand::flameUpdateHook))))
      Logger::warn("VR: The flame projectile functions were not found, flames come from in front of the head");

    // If these fail, laser beams follow the head while they fade
    if (found && !(VrGameMemory::redirectVirtual(VrGame::BeamProcess3DSlot,
          VrGame::BeamProcess3D, reinterpret_cast<const void*>(&VrWeaponHand::beamProcess3DHook))
        && VrGameMemory::redirectVirtual(VrGame::BeamUpdateSlot,
          VrGame::BeamUpdate, reinterpret_cast<const void*>(&VrWeaponHand::beamUpdateHook))))
      Logger::warn("VR: The beam projectile functions were not found, laser beams follow the head");

    // If this one fails, the hand's shots still bend towards where the head looks
    if (found && !VrGameMemory::redirectCall(VrGame::AutoAimCallSite,
        VrGame::ApplyAutoAim, reinterpret_cast<const void*>(&VrWeaponHand::autoAimHook)))
      Logger::warn("VR: The auto-aim call was not found, shots bend towards where the head looks");

    if (found) {
      bool redirected = true;

      for (uintptr_t site : VrGame::PlaySequenceCallSites) {
        redirected &= VrGameMemory::redirectCall(site,
          VrGame::PlaySequence, reinterpret_cast<const void*>(&VrWeaponHand::playSequenceHook));
      }

      if (!redirected)
        Logger::warn("VR: An animation call was not found, the left hand may reach for a pistol before aiming");
    }

    if (!found) {
      Logger::info("VR: The first-person model was not found, the weapon stays in front of the head");
      return nullptr;
    }

    std::unique_ptr<VrWeaponHand> weaponHand(new VrWeaponHand());
    g_weaponHand = weaponHand.get();

    // If this one fails, a lit fuse shows no sparks
    weaponHand->m_canEmitSparks = VrGameMemory::matches(VrGame::PSysEmitterCtlrEmit,
      VrGame::PSysEmitterCtlrEmitPrologue, sizeof(VrGame::PSysEmitterCtlrEmitPrologue));

    if (!weaponHand->m_canEmitSparks)
      Logger::warn("VR: The particle emitter function was not found, a lit fuse shows no sparks");

    return weaponHand;
  }


  void VrWeaponHand::beginFrame(
    const VrGameCameraPose&     gameCamera,
    const VrGameCameraPose*     hand,
    const VrGameCameraPose*     otherHand,
          bool                  grip,
          VrFirstPersonMode     mode,
          float                 dt) {
    if (mode != VrFirstPersonMode::Hands && mode != VrFirstPersonMode::FreeHands)
      hand = nullptr;

    m_frameTime         = dt;
    m_heldSettleUpdated = false;
    m_drawSettleUpdated = false;
    m_idleLighterUpdated = false;
    m_sparkUpdated      = false;
    m_magazineBlend     = std::min(m_magazineBlend + dt / MagazineBlendTime, 1.0f);
    m_actionSwing       = m_actionOpen
      ? std::min(m_actionSwing + dt / ActionSwingTime, 1.0f)
      : std::max(m_actionSwing - dt / ActionSwingTime, 0.0f);

    m_gameCamera   = gameCamera;
    m_hasHand      = hand != nullptr;
    m_hasOtherHand = otherHand != nullptr;
    m_grip         = grip;
    m_mode         = mode;

    if (hand)
      m_hand = *hand;
    else
      m_hasAim = false;

    if (m_snappedAim) {
      m_snapAim    = false;
      m_snappedAim = false;
    }

    // The grip lets go as soon as it is released, even if the model is not
    // drawn this frame
    if (!hand || !otherHand || !grip) {
      m_twoHanded = false;
      m_braced    = false;
    }

    if (otherHand)
      m_otherHand = *otherHand;
  }


  void VrWeaponHand::beforePlaceCamera(
          void*                 main,
    const VrVector3&            eyePosition) {
    m_main         = static_cast<uint8_t*>(main);
    m_eyePosition  = eyePosition;
    m_pipBoyPlaced = false;

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


  void __fastcall VrWeaponHand::pipBoyUpdateHook(
          uint8_t*              object,
          void*                 unused,
          void*                 updateData) {
    updateObject(object, updateData);

    // The update puts the forearm back where the game animates it, for the
    // Pip-Boy screen drawn after it. The model is at the same draw origin
    // as in PlaceCamera, so the eye's shift still applies.
    uint8_t* character = player();

    if (!g_weaponHand || !g_weaponHand->m_pipBoyPlaced || !character
     || object != field<uint8_t*>(character, VrGame::PlayerFirstPersonRoot))
      return;

    if (uint8_t* otherHand = findObject(object, VrGame::LeftHandNodeName))
      g_weaponHand->placeOtherHand(object, otherHand, g_weaponHand->m_pipBoyShift, updateData, nullptr);
  }


  void* __cdecl VrWeaponHand::createProjectileHook(
          void* projectile, void* shooter, void* target, void* weapon,
          float x, float y, float z, float heading, float pitch,
          uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
          float spreadHeading, float spreadPitch, uint32_t a15) {
    VrVector3 muzzle;
    bool thrown    = g_weaponHand && g_weaponHand->m_throwing && shooter == player();
    bool handAimed = thrown || handMuzzle(shooter, muzzle);

    if (thrown) {
      // Along the hand's motion, without the game's spread: the hand
      // throws it. The spread still spins it.
      const VrThrowRelease& release = g_weaponHand->m_throwRelease;
      VrHeadAngles aim = vrComputeAimAngles(release.direction);

      x = release.origin.x;
      y = release.origin.y;
      z = release.origin.z;
      heading = aim.yaw;
      pitch   = aim.pitch;
    } else if (handAimed) {
      VrHeadAngles aim = vrComputeAimAngles(g_weaponHand->m_aimDirection);

      x = muzzle.x;
      y = muzzle.y;
      z = muzzle.z;
      heading = aim.yaw + spreadHeading;
      pitch   = aim.pitch + spreadPitch;
    }

    // The projectile is initialised, and aimed by the auto-aim, before
    // Create returns
    if (g_weaponHand)
      g_weaponHand->m_handAimedShot = handAimed;

    void* result = reinterpret_cast<ProjectileCreateFn>(VrGame::ProjectileCreate)(
      projectile, shooter, target, weapon, x, y, z, heading, pitch,
      a9, a10, a11, a12, spreadHeading, spreadPitch, a15);

    if (g_weaponHand)
      g_weaponHand->m_handAimedShot = false;

    // A missile reads its speed as it moves, a grenade got it while it was
    // created
    if (thrown && result && g_weaponHand) {
      g_weaponHand->setThrowSpeed(static_cast<uint8_t*>(result));
      g_weaponHand->m_thrownProjectile = static_cast<uint8_t*>(result);
    }

    return result;
  }


  float __fastcall VrWeaponHand::projectileSpeedHook(
          uint8_t*              projectile,
          void*                 unused) {
    if (g_weaponHand && g_weaponHand->m_throwing)
      g_weaponHand->setThrowSpeed(projectile);

    return reinterpret_cast<ProjectileGetSpeedFn>(VrGame::ProjectileGetSpeed)(projectile, nullptr);
  }


  void VrWeaponHand::setThrowRelease(const VrThrowRelease* release) {
    m_throwing  = release != nullptr;
    m_throwSped = nullptr;

    if (release) {
      m_throwRelease     = *release;
      m_throwSpeeds      = VrThrowSpeeds();
      m_thrownProjectile = nullptr;
    }
  }


  void VrWeaponHand::setThrowSpeed(uint8_t* projectile) {
    if (projectile == m_throwSped)
      return;

    // The game's speed is linear in the multiplier, so scaling it gives
    // the speed asked for: a share of the game's throw at full power
    float full = reinterpret_cast<ProjectileGetSpeedFn>(VrGame::ProjectileGetSpeed)(projectile, nullptr);

    m_throwSped = projectile;

    if (!(full > 0.0f))
      return;

    float given = std::min(full * std::min(std::max(m_throwRelease.share, 0.0f), 1.0f), m_throwRelease.maxSpeed);
    field<float>(projectile, VrGame::ProjectileSpeedMult) *= given / full;

    m_throwSpeeds.full  = full;
    m_throwSpeeds.given = given;
  }


  bool VrWeaponHand::handMuzzle(
    const void*                 shooter,
          VrVector3&            muzzle) {
    uint8_t* object = player();

    if (!g_weaponHand || !g_weaponHand->m_hasAim || !object || shooter != object
     || *reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState))
      return false;

    uint8_t* root = field<uint8_t*>(object, VrGame::PlayerFirstPersonRoot);

    if (!root)
      return false;

    muzzle = parentFrame(root).toWorld(readVector(root, VrGame::NodeLocalTranslate))
           + g_weaponHand->m_muzzleOffset;
    return true;
  }


  template<typename Fn>
  void VrWeaponHand::withNodeAtMuzzle(uint8_t* projectile, Fn&& call) {
    auto shooter = field<uint8_t*>(projectile, VrGame::ProjectileShooter);
    VrVector3 muzzle;

    if (!handMuzzle(shooter, muzzle)) {
      call();
      return;
    }

    auto getNode = *reinterpret_cast<GetProjectileNodeFn*>(
      field<uint8_t*>(shooter, 0) + VrGame::RefProjectileNodeSlot);
    uint8_t* node = getNode(shooter, nullptr);

    if (!node) {
      call();
      return;
    }

    VrGameRotation rotation    = readRotation(node, VrGame::ObjectWorldRotation);
    VrVector3      translation = readVector(node, VrGame::ObjectWorldTranslation);

    writeRotation(node, VrGame::ObjectWorldRotation, g_weaponHand->m_muzzleRotation);
    writeVector(node, VrGame::ObjectWorldTranslation, muzzle);

    call();

    writeRotation(node, VrGame::ObjectWorldRotation, rotation);
    writeVector(node, VrGame::ObjectWorldTranslation, translation);

    if (g_weaponHand && !g_weaponHand->m_loggedFollow) {
      Logger::info("VR: Flames and beams stay on the muzzle drawn in the hand");
      g_weaponHand->m_loggedFollow = true;
    }
  }


  void __fastcall VrWeaponHand::flameProcess3DHook(
          uint8_t*              projectile,
          void*                 unused) {
    // The flame keeps the rotation it has to the node from here on: its
    // spread, once the node is at the muzzle
    withNodeAtMuzzle(projectile, [projectile] {
      reinterpret_cast<ProjectileProcess3DFn>(VrGame::FlameProcess3D)(projectile, nullptr);
    });
  }


  void __fastcall VrWeaponHand::flameUpdateHook(
          uint8_t*              projectile,
          void*                 unused,
          float                 dt) {
    // The flame is placed, then picks what it burns from there
    withNodeAtMuzzle(projectile, [projectile, dt] {
      reinterpret_cast<ProjectileUpdateFn>(VrGame::FlameUpdate)(projectile, nullptr, dt);
    });
  }


  void __fastcall VrWeaponHand::beamProcess3DHook(
          uint8_t*              projectile,
          void*                 unused) {
    // As a flame: the beam keeps its spread to the node
    withNodeAtMuzzle(projectile, [projectile] {
      reinterpret_cast<ProjectileProcess3DFn>(VrGame::BeamProcess3D)(projectile, nullptr);
    });
  }


  void __fastcall VrWeaponHand::beamUpdateHook(
          uint8_t*              projectile,
          void*                 unused,
          float                 dt) {
    // The beam stays on the muzzle while it fades, as the game keeps it on
    // its own
    withNodeAtMuzzle(projectile, [projectile, dt] {
      reinterpret_cast<ProjectileUpdateFn>(VrGame::BeamUpdate)(projectile, nullptr, dt);
    });
  }


  void __fastcall VrWeaponHand::autoAimHook(
          uint8_t*              player,
          void*                 unused,
          void*                 projectile,
          void*                 releaseNode,
          float*                heading,
          float*                pitch,
          void*                 origin,
          float                 startHeading,
          float                 startPitch) {
    // The game turns the player's projectiles towards the point under the
    // centre of its camera, up to a few degrees each way, then adds the gun
    // wobble and the spread again. With the camera on the head, that bends
    // the shots from where the weapon points towards where the head looks.
    // The weapon drawn in the hand already sways with the wobble, and the
    // spread is applied once when the shot is aimed.
    if (g_weaponHand && g_weaponHand->m_handAimedShot) {
      if (!g_weaponHand->m_loggedAutoAim) {
        Logger::info("VR: The game's auto-aim is skipped for shots aimed with the hand");
        g_weaponHand->m_loggedAutoAim = true;
      }
      return;
    }

    reinterpret_cast<ApplyAutoAimFn>(VrGame::ApplyAutoAim)(player, nullptr,
      projectile, releaseNode, heading, pitch, origin, startHeading, startPitch);
  }


  void* __fastcall VrWeaponHand::playSequenceHook(
          uint8_t*              animData,
          void*                 unused,
          void*                 sequence,
          uint32_t              groupId,
          int32_t               sequenceType) {
    auto playSequence = reinterpret_cast<PlaySequenceFn>(VrGame::PlaySequence);
    uint8_t* object = player();
    uint8_t  group  = uint8_t(groupId & 0xFF);

    bool snap = g_weaponHand && g_weaponHand->m_braced && g_weaponHand->m_snapAim
      && object && animData && animData == field<uint8_t*>(object, VrGame::PlayerFirstPersonAnimData)
      && group >= VrGame::AnimGroupAimIS && group <= VrGame::AnimGroupAimISDown;

    if (!snap)
      return playSequence(animData, nullptr, sequence, groupId, sequenceType);

    // The game's own way to start a sequence in its pose: the flag makes
    // the blend time zero. It is restored so that only this sequence skips
    // its blend. The aim's up and down variants start in the same game
    // update, so the brace stops snapping at the next frame.
    uint8_t& skipBlend = field<uint8_t>(animData, VrGame::AnimDataSkipBlend);
    uint8_t  previous  = skipBlend;

    skipBlend = 1;
    void* result = playSequence(animData, nullptr, sequence, groupId, sequenceType);
    skipBlend = previous;

    g_weaponHand->m_snappedAim = true;

    if (!g_weaponHand->m_loggedSnapAim) {
      Logger::info("VR: The aim animation starts without blending while the left hand supports the pistol");
      g_weaponHand->m_loggedSnapAim = true;
    }

    return result;
  }


  void VrWeaponHand::placeModel(uint8_t* root, void* updateData) {
    uint8_t* main = m_main;
    m_main = nullptr;

    // Found again by each placement, for the eye about to be drawn
    m_highlightRoot = nullptr;
    m_highlightParts.clear();
    m_highlightDrawn.clear();

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

    // The size is written after the game's update, the next update shows it
    PipBoyNodes pipBoy = findPipBoy(root);
    measurePipBoyScreen(pipBoy);
    scalePipBoy(pipBoy);

    // The game places its camera at its own eye
    if (uint8_t* camera = field<uint8_t*>(main, VrGame::MainFirstPersonCamera))
      writeVector(camera, VrGame::CameraWorldTranslation, m_eyePosition - shift);

    m_hasMagazine  = false;
    m_hasThrown    = false;
    m_hasRack      = false;
    m_hasHinge     = false;
    m_swingAction  = false;
    m_hasRoundPort = false;
    m_hasLock      = false;

    readFingerRigs(root);

    if (m_mode == VrFirstPersonMode::Hidden) {
      m_hasAim    = false;
      m_hasBlade  = false;
      m_twoHanded = false;
      m_braced    = false;
      hideModel(root);

      if (!m_loggedHidden) {
        Logger::info("VR: The arms and the weapon are hidden while the lasers point at a menu");
        m_loggedHidden = true;
      }
      return;
    }

    // The arms and the weapon are culled one by one from here on. The
    // weapon node, culled while it is holstered, is shown again with them.
    if (m_hidesModel || (m_hidesHolstered && m_mode != VrFirstPersonMode::FreeHands)) {
      showArms(root);
      m_hidesHolstered = false;
    }

    if (m_mode == VrFirstPersonMode::PipBoy) {
      m_hasAim    = false;
      m_hasBlade  = false;
      m_twoHanded = false;
      m_braced    = false;
      placePipBoyArm(root, shift, updateData);
      return;
    }

    if (m_mode == VrFirstPersonMode::FreeHands && m_hasHand) {
      m_hasAim     = false;
      m_hasBlade   = false;
      m_twoHanded  = false;
      m_braced     = false;
      m_drawWeapon = nullptr;
      placeFreeHands(root, drawnRoot, shift, updateData);
      return;
    }

    uint8_t* weapon = m_hasHand && holdsWeapon(object)
      ? findObject(root, VrGame::WeaponNodeName) : nullptr;

    if (!weapon) {
      m_hasAim    = false;
      m_hasBlade  = false;
      m_twoHanded = false;
      m_braced    = false;
      m_drawWeapon = nullptr;
      showArms(root);
      return;
    }

    // Where the game animates the weapon and the other hand, before the
    // model is moved
    uint8_t* otherHand = findObject(root, VrGame::LeftHandNodeName);
    uint8_t* forearm   = otherHand ? field<uint8_t*>(otherHand, VrGame::ObjectParent) : nullptr;

    VrVector3 drawnWeapon = readVector(weapon, VrGame::ObjectWorldTranslation);
    VrVector3 drawnHand   = forearm ? readVector(otherHand, VrGame::ObjectWorldTranslation) : drawnWeapon;

    VrGameCameraPose hand = m_hand;
    hand.position = hand.position - shift;

    // A melee weapon is swung by the hand: it keeps the angle in the hand
    // that the game gives it in its idle pose, whatever the game animates
    // on it later, such as blocking. Until the game has settled a weapon
    // just drawn, it follows the game's animation as other weapons do.
    const uint8_t* form = weaponForm(object);
    bool melee = isMelee(form);
    VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);

    if (!melee)
      m_heldForm = nullptr;

    if (melee && (weapon != m_heldWeapon || form != m_heldForm)) {
      m_heldWeapon = weapon;
      m_heldForm   = form;
      m_heldSettle.reset();
      m_bladeMeasured = false;
    }

    if (melee && !m_heldSettleUpdated) {
      m_heldSettleUpdated = true;

      if (m_heldSettle.update(vrComputeHeldRotation(m_gameCamera, weaponRotation), m_frameTime)) {
        Logger::info(str::format("VR: The melee weapon is held still in the hand after ", m_heldSettle.waited(), " s",
          m_heldSettle.timedOut() ? ", the game kept animating it" : ", once the game settled it"));
      }
    }

    bool held = melee && m_heldSettle.settled();

    // Without a weapon, or with one worn on the fist, the weapon node is
    // not where the hand is: the right hand bone takes the free hand's
    // pose around its controller, mirrored
    int8_t type = form ? readField<int8_t>(form, VrGame::WeaponAnimationType) : WeaponTypeNone;
    uint8_t* fist = (type == WeaponTypeNone || type == WeaponTypeHandToHand)
      ? findObject(root, VrGame::RightHandNodeName) : nullptr;

    // While the game blends the arms into the idle of a weapon just drawn,
    // the right hand can be far from the weapon node, which lands on the
    // controller: the model is hidden until the hand settles on the weapon
    if (!fist && settleDrawnWeapon(root, weapon, form, drawnWeapon, weaponRotation)) {
      m_hasAim    = false;
      m_hasBlade  = false;
      m_twoHanded = false;
      m_braced    = false;
      hideModel(root);
      return;
    }

    // A thrown weapon the idle holds otherwise than its throw is held as
    // the throw lets go of it
    bool regripped = !fist && type == WeaponTypeThrown && regripThrown(root, weapon, form, updateData);

    if (regripped) {
      drawnWeapon    = readVector(weapon, VrGame::ObjectWorldTranslation);
      weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);
    }

    VrGameTransform rig = held
      ? vrComputeRigHoldingWeapon(weaponRotation, drawnWeapon, m_heldSettle.rotation(), hand)
      : vrComputeRigToHand(m_gameCamera, drawnWeapon, hand);

    if (fist) {
      VrGameTransform target = vrComputeHeldTransform(hand, vrMirrorHandPose(FreeHandPose));
      VrGameRotation drawnRotation = readRotation(fist, VrGame::ObjectWorldRotation);
      VrVector3 drawnFist = readVector(fist, VrGame::ObjectWorldTranslation);

      rig.rotate    = target.rotate * vrTranspose(drawnRotation);
      rig.translate = target.translate - rig.rotate * drawnFist;

      if (!m_loggedFist) {
        Logger::info(str::format("VR: The right hand takes the free pose around its controller, the weapon node is ",
          vrLength(drawnWeapon - drawnFist) / VrGameUnitsPerMetre, " m from the hand"));
        m_loggedFist = true;
      }
    }

    // With both hands on a gun, it points along the line between them. A
    // two-handed melee weapon is swung by the right hand, the left hand
    // only rides on its handle.
    VrVector3 support = rig.rotate * (drawnHand - drawnWeapon);
    GripKind  grip    = weaponGrip(object);
    updateGrip(grip, forearm != nullptr, support);

    if (m_twoHanded && grip == GripKind::TwoHanded) {
      hand = vrComputeTwoHandedPose(hand, support, m_otherHand.position - m_hand.position);
      rig  = vrComputeRigToHand(m_gameCamera, drawnWeapon, hand);
    }

    // The new world transform of the root, brought back to its parent's frame
    VrGameRotation worldRotation = rig.rotate * readRotation(root, VrGame::ObjectWorldRotation);
    VrVector3 worldTranslation   = rig.rotate * drawnRoot + rig.translate;

    VrGameRotation rotation = readRotation(root, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(root, VrGame::NodeLocalTranslate);

    // With no weapon at all, the right fingers curl as the player's do
    // between the free hand's open pose and the game's fist
    std::array<SavedBone, VrFingerBoneCount> fingers;
    uint32_t fingerCount = 0;

    if (fist && !form && m_hasFingers[uint32_t(VrHand::Right)])
      fingerCount = poseRightFingers(fist, m_fingerCurls[uint32_t(VrHand::Right)], fingers);

    writeRotation(root, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * worldRotation);
    writeVector(root, VrGame::NodeLocalTranslate, parent.toLocal(worldTranslation));
    updateObject(root, updateData);

    // PlaceCamera restores the translation itself, the next eye's update
    // starts from the game's fingers
    writeRotation(root, VrGame::ObjectLocalRotation, rotation);
    writeVector(root, VrGame::NodeLocalTranslate, translation);

    if (regripped)
      restoreThrownGrip(weapon, updateData);

    for (uint32_t i = 0; i < fingerCount; i++)
      writeRotation(fingers[i].bone, VrGame::ObjectLocalRotation, fingers[i].rotate);

    // The left hand is still where the game animates it
    if (forearm && type >= WeaponTypeGrenade && type <= WeaponTypeThrown)
      measureIdleLighter(root, weapon, otherHand, form);

    bool handPlaced = forearm && m_hasOtherHand && !m_twoHanded && !m_braced;

    // An action that opens is drawn open first: a break action's shells
    // and the hand holding them follow the barrel
    ActionKind action = ActionKind::None;
    uint8_t* hinge = findAction(weapon, form, action);
    openAction(weapon, hinge, action, form, updateData);

    // The magazine out of the weapon and the rack part are placed first: a
    // hand holding the magazine in its well, or the rack part, follows it
    VrGameTransform magazinePose;
    uint8_t* magazine = placeMagazine(root, weapon, action, hinge, otherHand, handPlaced, hand, form, shift, magazinePose);

    VrGameTransform rackPose;
    uint8_t* rack = placeRack(weapon, hand, rackPose);

    uint8_t* lock = placeLock(weapon, hand, updateData);

    if (handPlaced) {
      HeldPart held = { };

      // While a handful is loaded the hand stays at its controller, its
      // rounds going into the port
      bool loading = m_hasRoundPort && m_roundState == VrRoundState::Loading;

      if (loading && holdsMagazine())
        held = { magazineAtController(field<float>(otherHand, VrGame::ObjectWorldScale), shift), &magazineInHand(), &m_reloadAnimation.fingers };
      else if (magazine && holdsMagazine())
        held = { magazinePose, &magazineInHand(), &m_reloadAnimation.fingers };
      else if (rack && holdsRack())
        held = { rackPose, &m_reloadAnimation.rack.partInHand, &m_reloadAnimation.rack.fingers };
      else
        thrownHeldPart(form, otherHand, shift, held);

      placeOtherHand(root, otherHand, shift, updateData, held.inHand ? &held : nullptr);
    }

    // Held still, the blade does not move in the hand
    if (held && !m_bladeMeasured)
      measureBlade(weapon, hand);

    m_hasBlade = held && m_bladeMeasured;
    m_bladeAutomatic = form && (readField<uint8_t>(form, VrGame::WeaponFlags) & VrGame::WeaponFlagAutomatic);

    hideArms(root, false);

    if (magazine)
      drawPart(magazine, magazinePose, updateData);

    if (m_hasHinge && !m_swingAction)
      placeOtherShells(weapon, magazine, form, magazine ? &magazinePose : nullptr, updateData);

    // A revolver loaded round by round keeps its cases
    if (m_hasHinge && m_swingAction && !readField<uint8_t>(form, VrGame::WeaponLoopingReload))
      hideCylinder(weapon);

    if (rack)
      drawPart(rack, rackPose, updateData);

    findHighlight(root, weapon, hinge, lock, action == ActionKind::Swing);

    uint8_t* muzzle = findObject(weapon, VrGame::ProjectileNodeName);

    if (!muzzle)
      muzzle = findObject(weapon, VrGame::ProjectileNodeAltName);

    // Relative to the root, which the game places at the player. The shots
    // follow the barrel as it is drawn: the game animates the weapon at an
    // angle to its camera, so the barrel does not point along the hand.
    m_muzzleOffset   = readVector(muzzle ? muzzle : weapon, VrGame::ObjectWorldTranslation) - drawnRoot;
    m_muzzleRotation = readRotation(muzzle ? muzzle : weapon, VrGame::ObjectWorldRotation);
    m_aimDirection   = muzzle
      ? vrComputeBarrelDirection(m_muzzleRotation)
      : hand.forward;
    m_hasAim         = true;

    if (weapon != m_loggedBarrelWeapon) {
      VrHeadAngles barrelAngles = vrComputeAimAngles(m_aimDirection);
      VrHeadAngles handAngles   = vrComputeAimAngles(hand.forward);

      // Heading difference wrapped into [-pi, pi)
      float right = vrWrapHeading(barrelAngles.yaw - handAngles.yaw + Pi) - Pi;
      float up    = handAngles.pitch - barrelAngles.pitch;

      Logger::info(str::format("VR: Weapon barrel ", right * DegreesPerRadian, " degrees right and ",
        up * DegreesPerRadian, " degrees up of the hand",
        muzzle ? "" : " (no projectile node, the shots follow the hand)"));
      m_loggedBarrelWeapon = weapon;
    }

    if (type >= WeaponTypeGrenade && type <= WeaponTypeThrown)
      placeThrown(root, weapon, otherHand, form, type, hand, shift, updateData);

    // The weapon's model is attached below the node once it is loaded
    uint32_t objects = vrCountWeaponObjects(weapon);

    if (form != m_loggedPartsForm || objects != m_loggedPartsObjects) {
      vrLogWeaponParts(weapon, form);
      m_loggedPartsForm    = form;
      m_loggedPartsObjects = objects;
    }

    if (!m_loggedActive) {
      Logger::info("VR: The weapon is in the right hand");
      m_loggedActive = true;
    }
  }


  void VrWeaponHand::readFingerRigs(uint8_t* root) {
    for (uint32_t h = 0; h < VrHandCount; h++) {
      if (m_hasFingerRig[h])
        continue;

      VrHand side = VrHand(h);
      uint8_t* hand = findObject(root, side == VrHand::Left ? VrGame::LeftHandNodeName : VrGame::RightHandNodeName);

      if (!hand)
        continue;

      // The bones' translations and scales do not change, only their
      // rotations, and the free hand's bone has a fixed pose
      VrFingerRig& rig = m_fingerRigs[h];
      rig.hand  = side == VrHand::Left ? FreeHandPose : vrMirrorHandPose(FreeHandPose);
      rig.scale = field<float>(hand, VrGame::ObjectWorldScale);
      uint32_t found = 0;

      forEachObject(hand, MaxModelDepth, [&rig, &found, side] (uint8_t* object) {
        auto name = field<const char*>(object, VrGame::ObjectName);
        VrHand boneSide;
        uint32_t index;

        if (!name || !vrFindFingerBone(name, boneSide, index) || boneSide != side)
          return;

        rig.translate[index] = readVector(object, VrGame::NodeLocalTranslate);
        rig.boneScale[index] = field<float>(object, VrGame::ObjectLocalScale);
        found += 1;
      });

      m_hasFingerRig[h] = found == VrFingerBoneCount && rig.scale > 0.0f;

      if (m_hasFingerRig[h])
        Logger::info(str::format("VR: The ", side == VrHand::Left ? "left" : "right",
          " hand's finger bones were read for fitting the fingers on held objects"));
    }
  }


  void VrWeaponHand::placeFreeHands(
          uint8_t*              root,
    const VrVector3&            drawnRoot,
    const VrVector3&            shift,
          void*                 updateData) {
    uint8_t* rightHand = findObject(root, VrGame::RightHandNodeName);
    uint8_t* otherHand = findObject(root, VrGame::LeftHandNodeName);
    uint8_t* forearm   = otherHand ? field<uint8_t*>(otherHand, VrGame::ObjectParent) : nullptr;

    if (!rightHand) {
      showArms(root);
      return;
    }

    ParentFrame parent = parentFrame(root);

    // The right hand bone takes the free hand's pose around its
    // controller, mirrored, wherever the game holds the arms with the
    // weapon holstered
    VrGameCameraPose hand = m_hand;
    hand.position = hand.position - shift;

    VrGameTransform target = vrComputeHeldTransform(hand, vrMirrorHandPose(FreeHandPose));
    VrGameRotation drawnRotation = readRotation(rightHand, VrGame::ObjectWorldRotation);
    VrVector3 drawnHand = readVector(rightHand, VrGame::ObjectWorldTranslation);

    VrGameTransform rig;
    rig.rotate    = target.rotate * vrTranspose(drawnRotation);
    rig.translate = target.translate - rig.rotate * drawnHand;

    VrGameRotation worldRotation = rig.rotate * readRotation(root, VrGame::ObjectWorldRotation);
    VrVector3 worldTranslation   = rig.rotate * drawnRoot + rig.translate;

    VrGameRotation rotation = readRotation(root, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(root, VrGame::NodeLocalTranslate);

    std::array<SavedBone, VrFingerBoneCount> fingers;
    uint32_t fingerCount = 0;

    if (m_hasFingers[uint32_t(VrHand::Right)])
      fingerCount = poseRightFingers(rightHand, m_fingerCurls[uint32_t(VrHand::Right)], fingers);

    writeRotation(root, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * worldRotation);
    writeVector(root, VrGame::NodeLocalTranslate, parent.toLocal(worldTranslation));
    updateObject(root, updateData);

    writeRotation(root, VrGame::ObjectLocalRotation, rotation);
    writeVector(root, VrGame::NodeLocalTranslate, translation);

    for (uint32_t i = 0; i < fingerCount; i++)
      writeRotation(fingers[i].bone, VrGame::ObjectLocalRotation, fingers[i].rotate);

    if (forearm && m_hasOtherHand)
      placeOtherHand(root, otherHand, shift, updateData, nullptr);

    hideArms(root, false);

    // A holstered weapon is not in the hand
    if (uint8_t* weapon = findObject(root, VrGame::WeaponNodeName))
      hide(weapon, !m_loggedFreeHands);

    m_hidesHolstered = true;

    if (!m_loggedFreeHands) {
      Logger::info("VR: The weapon is holstered, both hands take the free pose around their controllers");
      m_loggedFreeHands = true;
    }
  }


  bool VrWeaponHand::takeHighlight(uint8_t*& root, std::vector<uint8_t*>& parts, VrGameWorldTransforms& drawn) {
    if (!m_highlightRoot)
      return false;

    root = m_highlightRoot;
    parts.swap(m_highlightParts);
    std::swap(drawn, m_highlightDrawn);

    m_highlightRoot = nullptr;
    m_highlightParts.clear();
    m_highlightDrawn.clear();
    return true;
  }


  void VrWeaponHand::findHighlight(uint8_t* root, uint8_t* weapon, uint8_t* hinge, uint8_t* lock, bool swing) {
    if (!m_highlight.any())
      return;

    auto add = [this] (uint8_t* part) {
      if (part && !(field<uint32_t>(part, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled))
        m_highlightParts.push_back(part);
    };

    if (m_highlight.magazine && m_magazineState == VrMagazineState::InWeapon)
      add(vrFindWeaponPart(weapon, VrWeaponPartKind::Magazine));

    if (m_highlight.rack && !m_hasHinge)
      add(vrFindWeaponPart(weapon, VrWeaponPartKind::Rack));

    if (m_highlight.lock)
      add(lock);

    // A break action's barrel turns down about the breech
    if (m_highlight.hinge && m_hasHinge && !swing)
      add(hinge);

    // A revolver's loading gate swings out, a rifle's cap is named as one
    if (m_highlight.port)
      add(swing ? hinge : vrFindWeaponPart(weapon, VrWeaponPartKind::Gate));

    // Every part is placed by now: the game draws the model as it is, then
    // updates it again where it animates it, long before the highlight
    if (!m_highlightParts.empty()) {
      m_highlightRoot = root;
      m_highlightDrawn.capture(reinterpret_cast<uintptr_t>(root));
    }
  }


  void VrWeaponHand::placePipBoyArm(uint8_t* root, const VrVector3& shift, void* updateData) {
    // The game raises the forearm in front of its camera, which follows the
    // head: the forearm follows the left controller instead, and the
    // Pip-Boy with it
    uint8_t* otherHand = findObject(root, VrGame::LeftHandNodeName);

    if (otherHand && m_hasOtherHand && field<uint8_t*>(otherHand, VrGame::ObjectParent)) {
      placeOtherHand(root, otherHand, shift, updateData, nullptr);
      m_pipBoyShift  = shift;
      m_pipBoyPlaced = true;

      if (!m_loggedPipBoy) {
        Logger::info("VR: The Pip-Boy is on the left forearm, which follows the left controller");
        m_loggedPipBoy = true;
      }
    }

    // The game animates the right hand in front of its camera, so it is
    // hidden with the weapon until it has a pose of its own
    hideArms(root, true);
  }


  VrWeaponHand::PipBoyNodes VrWeaponHand::findPipBoy(uint8_t* root) {
    PipBoyNodes nodes;
    nodes.hand    = findObject(root, VrGame::LeftHandNodeName);
    nodes.forearm = nodes.hand ? field<uint8_t*>(nodes.hand, VrGame::ObjectParent) : nullptr;
    nodes.twist   = nodes.forearm ? findObject(nodes.forearm, VrGame::LeftForeTwistNodeName) : nullptr;
    nodes.screen  = nodes.twist ? findObject(nodes.twist, VrGame::PipBoyScreenNodeName) : nullptr;

    if (nodes.twist && !nodes.screen)
      nodes.screen = findObject(nodes.twist, VrGame::PipBoyScreenNodeAltName);

    if (!nodes.screen || field<uint8_t*>(nodes.twist, VrGame::ObjectParent) != nodes.forearm) {
      nodes.screen = nullptr;
      return nodes;
    }

    // The model's root is the screen's ancestor attached to the twist bone
    uint8_t* device = nodes.screen;

    while (device && field<uint8_t*>(device, VrGame::ObjectParent) != nodes.twist)
      device = field<uint8_t*>(device, VrGame::ObjectParent);

    nodes.device = device;
    return nodes;
  }


  void VrWeaponHand::measurePipBoyScreen(const PipBoyNodes& nodes) {
    // Offsets between the bones and from the twist bone to the screen do
    // not depend on the pose the game animates, only the rotations that
    // the free pose replaces do
    if (!nodes.screen) {
      if (!m_loggedScreen)
        Logger::warn("VR: The Pip-Boy screen was not found on the left forearm, looking at it does not open the Pip-Boy");

      m_hasPipBoyScreen = false;
      m_loggedScreen    = true;
      return;
    }

    uint8_t* hand    = nodes.hand;
    uint8_t* forearm = nodes.forearm;
    uint8_t* twist   = nodes.twist;
    uint8_t* screen  = nodes.screen;

    VrGameRotation forearmRotation = readRotation(forearm, VrGame::ObjectWorldRotation);
    VrVector3 forearmPosition      = readVector(forearm, VrGame::ObjectWorldTranslation);
    VrGameRotation twistRotation   = readRotation(twist, VrGame::ObjectWorldRotation);
    VrVector3 twistPosition        = readVector(twist, VrGame::ObjectWorldTranslation);

    // The screen geometry's origin need not be its centre, its bound's is
    VrVector3 centre = readVector(screen, VrGame::ObjectWorldTranslation);

    if (auto bound = field<const uint8_t*>(screen, VrGame::ObjectWorldBound))
      centre = readVector(bound, 0);

    VrVector3 wristOffset = vrTranspose(forearmRotation) * (readVector(hand, VrGame::ObjectWorldTranslation) - forearmPosition);
    VrVector3 twistOffset = vrTranspose(forearmRotation) * (twistPosition - forearmPosition);
    VrVector3 point       = vrTranspose(twistRotation) * (centre - twistPosition);

    m_pipBoyScreen = vrComputeForearmPoint(FreeHandPose, FreeHandWrist, FreeHandTwist,
      wristOffset, twistOffset, point) * (1.0f / VrGameUnitsPerMetre);

    // The screen lies on the forearm, facing away from its axis, which runs
    // from the elbow (the forearm bone) to the wrist (the hand bone)
    VrVector3 axis   = FreeHandPose.rotate * vrTranspose(FreeHandWrist) * wristOffset;
    VrVector3 normal = m_pipBoyScreen * VrGameUnitsPerMetre - (FreeHandPose.translate - axis);
    float axisLength = vrLength(axis);

    if (axisLength > 0.0f) {
      axis   = axis * (1.0f / axisLength);
      normal = normal - axis * vrDot(normal, axis);
    }

    float normalLength = vrLength(normal);
    m_pipBoyScreenNormal = normalLength > 0.0f ? normal * (1.0f / normalLength) : VrVector3();
    m_hasPipBoyScreen = normalLength > 0.0f;

    if (!m_loggedScreen) {
      Logger::info(str::format("VR: The Pip-Boy screen is ", m_pipBoyScreen.x, " m forward, ",
        m_pipBoyScreen.y, " m up and ", m_pipBoyScreen.z, " m right of the left controller, facing ",
        m_pipBoyScreenNormal.x, ", ", m_pipBoyScreenNormal.y, ", ", m_pipBoyScreenNormal.z));
      m_loggedScreen = true;
    }
  }


  void VrWeaponHand::scalePipBoy(const PipBoyNodes& nodes) {
    uint8_t* device = nodes.device;

    if (!device)
      return;

    VrNodeScale current = {
      readVector(device, VrGame::ObjectLocalTranslation),
      field<float>(device, VrGame::ObjectLocalScale) };

    // A model the game attached or placed again is at its own size
    bool replaced = device != m_pipBoyDevice
      || std::memcmp(&current, &m_pipBoyWrittenPlacement, sizeof(current));

    if (replaced) {
      m_pipBoyDevice           = device;
      m_pipBoyOwnPlacement     = current;
      m_pipBoyWrittenPlacement = current;
      m_pipBoyAppliedScale     = 1.0f;
    }

    if (m_pipBoyScale == m_pipBoyAppliedScale)
      return;

    VrNodeScale placement = m_pipBoyOwnPlacement;

    if (m_pipBoyScale != 1.0f) {
      // The screen's centre in the twist bone's frame at the model's own
      // placement, and the forearm's axis there: the line from the forearm
      // bone to the wrist
      ParentFrame twist = parentFrame(device);

      ParentFrame model;
      model.rotate    = readRotation(device, VrGame::ObjectWorldRotation);
      model.translate = readVector(device, VrGame::ObjectWorldTranslation);
      model.scale     = field<float>(device, VrGame::ObjectWorldScale);

      VrVector3 centre = readVector(nodes.screen, VrGame::ObjectWorldTranslation);

      if (auto bound = field<const uint8_t*>(nodes.screen, VrGame::ObjectWorldBound))
        centre = readVector(bound, 0);

      VrVector3 screen = readRotation(device, VrGame::ObjectLocalRotation)
        * (model.toLocal(centre) * m_pipBoyOwnPlacement.scale) + m_pipBoyOwnPlacement.translate;

      VrVector3 pivot = vrClosestPointOnLine(
        twist.toLocal(readVector(nodes.forearm, VrGame::ObjectWorldTranslation)),
        twist.toLocal(readVector(nodes.hand, VrGame::ObjectWorldTranslation)),
        screen);

      placement = vrScaleAboutPoint(m_pipBoyOwnPlacement, pivot, m_pipBoyScale);
    }

    writeVector(device, VrGame::ObjectLocalTranslation, placement.translate);
    field<float>(device, VrGame::ObjectLocalScale) = placement.scale;

    m_pipBoyWrittenPlacement = placement;
    m_pipBoyAppliedScale     = m_pipBoyScale;

    if (!m_loggedPipBoyScale && m_pipBoyScale != 1.0f) {
      Logger::info(str::format("VR: The Pip-Boy grows while it is open, up to ", m_pipBoyScale, " times its size"));
      m_loggedPipBoyScale = true;
    }
  }


  void VrWeaponHand::updateGrip(GripKind kind, bool canHold, const VrVector3& support) {
    if (!canHold || !m_hasOtherHand || !m_grip || kind == GripKind::None) {
      m_twoHanded = false;
      m_braced    = false;
      return;
    }

    // The grip takes the weapon when the controller comes near where the
    // game holds it, and keeps it until it is released. The game only
    // holds a one-handed weapon with both hands while it aims, so there
    // the other controller only has to come near the hand.
    if (kind == GripKind::TwoHanded || kind == GripKind::TwoHandedMelee) {
      m_braced = false;

      if (!m_twoHanded)
        m_twoHanded = vrLength(m_hand.position + support - m_otherHand.position) <= TwoHandedGripDistance;
    } else {
      m_twoHanded = false;

      if (!m_braced) {
        m_braced  = vrLength(m_hand.position - m_otherHand.position) <= BracedGripDistance;
        m_snapAim = m_braced;
      }
    }

    if (m_twoHanded && kind == GripKind::TwoHanded && !m_loggedTwoHanded) {
      Logger::info("VR: The weapon is held with both hands");
      m_loggedTwoHanded = true;
    }

    if (m_twoHanded && kind == GripKind::TwoHandedMelee && !m_loggedTwoHandedMelee) {
      Logger::info("VR: The melee weapon is held with both hands, the right hand swings it");
      m_loggedTwoHandedMelee = true;
    }

    if (m_braced && !m_loggedBraced) {
      Logger::info("VR: The left hand supports the one-handed weapon, the game aims");
      m_loggedBraced = true;
    }
  }


  void VrWeaponHand::placeOtherHand(
          uint8_t*              root,
          uint8_t*              otherHand,
    const VrVector3&            shift,
          void*                 updateData,
    const HeldPart*             held) {
    // The game animates the free hand anywhere, so its pose is not kept:
    // it takes a fixed pose around its controller. The forearm carries the
    // hand, and the hand's meshes only use bones below the forearm.
    uint8_t* forearm = field<uint8_t*>(otherHand, VrGame::ObjectParent);

    VrGameRotation forearmRotation = readRotation(forearm, VrGame::ObjectWorldRotation);
    VrVector3 forearmPosition      = readVector(forearm, VrGame::ObjectWorldTranslation);
    VrVector3 handPosition         = readVector(otherHand, VrGame::ObjectWorldTranslation);

    VrGameCameraPose controller = m_otherHand;
    controller.position = controller.position - shift;

    VrGameTransform target = vrComputeHeldTransform(controller, FreeHandPose);

    // A hand holding a part holds it where it is drawn: a magazine in its
    // well too, the rack part on its way back
    if (held) {
      const VrGameTransform& inHand = *held->inHand;
      float handScale = field<float>(otherHand, VrGame::ObjectWorldScale);

      target.rotate    = held->pose.rotate * vrTranspose(inHand.rotate);
      target.translate = held->pose.translate - target.rotate * inHand.translate * handScale;
    }

    // The forearm is placed so that the hand, at its fixed wrist rotation,
    // lands on the target. The hand's offset in the forearm, scale
    // included, is the animation's: it is the length of the forearm.
    VrVector3 wrist = vrTranspose(forearmRotation) * (handPosition - forearmPosition);

    VrGameRotation worldRotation = target.rotate * vrTranspose(FreeHandWrist);
    VrVector3 worldTranslation   = target.translate - worldRotation * wrist;

    ParentFrame parent = parentFrame(forearm);

    VrGameRotation rotation = readRotation(forearm, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(forearm, VrGame::NodeLocalTranslate);

    writeRotation(forearm, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * worldRotation);
    writeVector(forearm, VrGame::NodeLocalTranslate, parent.toLocal(worldTranslation));

    // The wrist, the twist bone and the fingers take the pose too. A hand
    // holding a part closes its fingers on it as the game's reload
    // animation does, a free hand curls them as the player does.
    const std::vector<VrBonePose>* grip = held ? held->fingers : nullptr;
    VrFingerCurls curls = held ? VrFingerCurls() : m_fingerCurls[uint32_t(VrHand::Left)];

    std::array<SavedBone, MaxPosedBones> saved;
    uint32_t savedCount = 0;

    forEachObject(forearm, MaxModelDepth, [grip, &curls, &saved, &savedCount] (uint8_t* object) {
      auto name = field<const char*>(object, VrGame::ObjectName);
      VrGameRotation pose;

      if (!name || savedCount == saved.size() || !freeHandRotation(name, grip, curls, pose))
        return;

      saved[savedCount++] = { object, readRotation(object, VrGame::ObjectLocalRotation) };
      writeRotation(object, VrGame::ObjectLocalRotation, pose);
    });

    updateObject(forearm, updateData);

    // The next eye's update starts from the game's pose again
    writeRotation(forearm, VrGame::ObjectLocalRotation, rotation);
    writeVector(forearm, VrGame::NodeLocalTranslate, translation);

    for (uint32_t i = 0; i < savedCount; i++)
      writeRotation(saved[i].bone, VrGame::ObjectLocalRotation, saved[i].rotate);

    // A skinned mesh computes its bound from its bones when it is updated
    // itself, and the hand's meshes are not below the forearm: without this,
    // they are culled where the game animated the hand
    forEachObject(root, MaxModelDepth, [forearm, updateData] (uint8_t* object) {
      uint8_t* geometry = asGeometry(object);

      if (geometry && isSkinnedBelow(geometry, forearm))
        updateObject(geometry, updateData);
    });

    if (!m_loggedOtherHand) {
      auto name = field<const char*>(forearm, VrGame::ObjectName);
      Logger::info(str::format("VR: The left hand takes its free pose around its controller, moved by '",
        name ? name : "", "'"));
      m_loggedOtherHand = true;
    }
  }


  void VrWeaponHand::setRoundLoop(VrRoundState state, float time) {
    // The round moves from where it was drawn last between the hand and
    // the loop
    bool moving = (state == VrRoundState::Loading) != (m_roundState == VrRoundState::Loading);

    if (moving && m_hasMagazineDrawn) {
      m_magazineFrom  = m_magazineDrawn;
      m_magazineBlend = 0.0f;
    }

    m_roundState = state;
    m_roundTime  = time;
  }


  void VrWeaponHand::pinchRound(uint8_t* otherHand, const VrVector3& centre, float scale) {
    uint8_t* thumb = findObject(otherHand, PinchThumbBone);
    uint8_t* index = findObject(otherHand, PinchIndexBone);

    VrVector3 thumbPoint, indexPoint;
    m_hasPinch = thumb && index
      && boneInHand(otherHand, thumb, &m_reloadAnimation.fingers, thumbPoint)
      && boneInHand(otherHand, index, &m_reloadAnimation.fingers, indexPoint);

    if (!m_hasPinch)
      return;

    m_pinchInHand.rotate    = m_reloadAnimation.magazineInHand.rotate;
    m_pinchInHand.translate = (thumbPoint + indexPoint) * 0.5f - m_pinchInHand.rotate * centre * scale;
  }


  VrGameTransform VrWeaponHand::magazineAtController(float handScale, const VrVector3& shift) const {
    VrGameCameraPose controller = m_otherHand;
    controller.position = controller.position - shift;

    VrGameTransform hand = vrComputeHeldTransform(controller, FreeHandPose);
    const VrGameTransform& inHand = magazineInHand();

    VrGameTransform pose;
    pose.rotate    = hand.rotate * inHand.rotate;
    pose.translate = hand.translate + hand.rotate * inHand.translate * handScale;
    return pose;
  }


  void VrWeaponHand::setMagazine(VrMagazineState state, float seat) {
    // Between the hand and the well the magazine moves from where it was
    // drawn last
    bool moving = (state == VrMagazineState::Seating && m_magazineState == VrMagazineState::InHand)
               || (state == VrMagazineState::InHand && m_magazineState == VrMagazineState::Seating);

    if (moving && m_hasMagazineDrawn) {
      m_magazineFrom  = m_magazineDrawn;
      m_magazineBlend = 0.0f;
    }

    if (state != VrMagazineState::Seating && state != VrMagazineState::InHand) {
      m_magazineBlend    = 1.0f;
      m_hasMagazineDrawn = false;
    }

    m_magazineState = state;
    m_magazineSeat  = seat;
  }


  bool VrWeaponHand::holdsMagazine() const {
    return m_mode == VrFirstPersonMode::Hands && m_hasReloadAnimation
      && (m_magazineState == VrMagazineState::InHand || m_magazineState == VrMagazineState::Seating);
  }


  void VrWeaponHand::setRack(bool held, float pull, float rest) {
    m_rackHeld = held;
    m_rackPull = pull;
    m_rackRest = rest;
  }


  bool VrWeaponHand::holdsRack() const {
    return m_mode == VrFirstPersonMode::Hands && m_rackHeld
      && m_hasReloadAnimation && m_reloadAnimation.hasRack && m_reloadAnimation.rack.held;
  }


  uint8_t* VrWeaponHand::findAction(uint8_t* weapon, const uint8_t* form, ActionKind& kind) {
    kind = ActionKind::None;

    if (!form)
      return nullptr;

    bool looping = readField<uint8_t>(form, VrGame::WeaponLoopingReload);

    // A revolver: the part that swings its cylinder out is the one its
    // reload animation turns, its loading gate is found by name
    if (readField<int8_t>(form, VrGame::WeaponAnimationType) == WeaponTypePistol) {
      uint8_t* gate = looping ? vrFindWeaponPart(weapon, VrWeaponPartKind::Gate) : nullptr;

      if (gate || (!looping && vrFindWeaponPart(weapon, VrWeaponPartKind::Loader)))
        kind = ActionKind::Swing;

      return gate;
    }

    if (looping)
      return nullptr;

    // By name: a magazine taken out is culled, and has no bound then
    bool magazine = false;

    vrVisitGameObjects(reinterpret_cast<uintptr_t>(weapon), MaxModelDepth,
      [&magazine] (uintptr_t object, uint32_t) {
        auto name = field<const char*>(reinterpret_cast<uint8_t*>(object), VrGame::ObjectName);
        magazine |= vrClassifyWeaponPart(name) == VrWeaponPartKind::Magazine;
      });

    uint8_t* hinge = magazine ? nullptr : vrFindWeaponPart(weapon, VrWeaponPartKind::Hinge);

    if (hinge)
      kind = ActionKind::Break;

    return hinge;
  }


  void VrWeaponHand::openAction(
          uint8_t*              weapon,
          uint8_t*              hinge,
          ActionKind            kind,
    const uint8_t*              form,
          void*                 updateData) {
    m_actionTurn = VrGameRotation();

    if (kind == ActionKind::None || m_actionSwing <= 0.0f || form != m_reloadForm
     || !m_hasReloadAnimation || !m_reloadAnimation.hasHinge)
      return;

    // The part that swings a cylinder out is named by the animation
    if (!hinge)
      hinge = findObject(weapon, m_reloadAnimation.hinge.part.c_str());

    if (!hinge)
      return;

    VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);
    VrVector3 weaponPosition      = readVector(weapon, VrGame::ObjectWorldTranslation);
    float weaponScale             = field<float>(weapon, VrGame::ObjectWorldScale);

    if (weaponScale <= 0.0f)
      return;

    // The hinge part where the game draws it, closed, in the world and in
    // the weapon node's frame
    VrGameTransform hingeWorld;
    hingeWorld.rotate    = readRotation(hinge, VrGame::ObjectWorldRotation);
    hingeWorld.translate = readVector(hinge, VrGame::ObjectWorldTranslation);

    VrGameTransform closed;
    closed.rotate    = vrTranspose(weaponRotation) * hingeWorld.rotate;
    closed.translate = vrTranspose(weaponRotation) * (hingeWorld.translate - weaponPosition) * (1.0f / weaponScale);

    // As far open as the swing is
    const VrGameTransform& open = m_reloadAnimation.hinge.open;
    float t = m_actionSwing * m_actionSwing * (3.0f - 2.0f * m_actionSwing);

    VrGameTransform opened;
    opened.rotate    = vrGameRotationFromQuaternion(vrSlerp(
      vrQuaternionFromGameRotation(closed.rotate), vrQuaternionFromGameRotation(open.rotate), t));
    opened.translate = closed.translate + (open.translate - closed.translate) * t;

    // The part swings out in the weapon, which stays in the hand: a
    // break action's hinge part carries the barrel and its shells
    VrGameTransform partPose;
    partPose.rotate    = weaponRotation * opened.rotate;
    partPose.translate = weaponPosition + weaponRotation * opened.translate * weaponScale;

    drawPart(hinge, partPose, updateData);
    m_actionTurn = opened.rotate * vrTranspose(closed.rotate);
  }


  void VrWeaponHand::hideCylinder(uint8_t* weapon) {
    // Every cartridge, not counting the meshes below one, nor those of the
    // speed loader
    std::vector<uint8_t*> cartridges;

    vrVisitGameObjects(reinterpret_cast<uintptr_t>(weapon), MaxModelDepth,
      [&cartridges, weapon] (uintptr_t object, uint32_t) {
        auto part = reinterpret_cast<uint8_t*>(object);

        if (!vrIsCartridgePart(field<const char*>(part, VrGame::ObjectName)))
          return;

        for (uint8_t* parent = field<uint8_t*>(part, VrGame::ObjectParent); parent && parent != weapon;
            parent = field<uint8_t*>(parent, VrGame::ObjectParent)) {
          auto name = field<const char*>(parent, VrGame::ObjectName);

          if (vrIsCartridgePart(name) || vrClassifyWeaponPart(name) == VrWeaponPartKind::Loader)
            return;
        }

        cartridges.push_back(part);
      });

    // Emptied from the time it is swung open until a speed loader is home
    bool emptied = m_magazineState != VrMagazineState::InWeapon;

    for (auto i = m_hiddenShells.begin(); i != m_hiddenShells.end(); ) {
      bool found = std::find(cartridges.begin(), cartridges.end(), *i) != cartridges.end();

      if (found && emptied) {
        ++i;
        continue;
      }

      if (found)
        field<uint32_t>(*i, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;

      i = m_hiddenShells.erase(i);
    }

    if (!emptied)
      return;

    for (uint8_t* cartridge : cartridges) {
      uint32_t& flags = field<uint32_t>(cartridge, VrGame::ObjectFlags);

      // A part the game culls itself is left to the game
      if (!(flags & VrGame::ObjectFlagAppCulled)) {
        flags |= VrGame::ObjectFlagAppCulled;
        m_hiddenShells.push_back(cartridge);
      }
    }
  }


  void VrWeaponHand::placeOtherShells(
          uint8_t*              weapon,
          uint8_t*              round,
    const uint8_t*              form,
    const VrGameTransform*      pose,
          void*                 updateData) {
    if (form != m_otherShellsForm) {
      m_otherShells.clear();
      m_otherShellsForm = form;
    }

    // The round the game draws in the weapon, to measure the others from
    bool inWeapon = m_magazineState == VrMagazineState::InWeapon;

    if (!round && inWeapon)
      round = vrFindWeaponPart(weapon, VrWeaponPartKind::Round);

    // Every other round part, not counting the meshes below one
    std::vector<uint8_t*> shells;

    auto isRound = [] (uint8_t* part) {
      return vrClassifyWeaponPart(field<const char*>(part, VrGame::ObjectName)) == VrWeaponPartKind::Round;
    };

    vrVisitGameObjects(reinterpret_cast<uintptr_t>(weapon), MaxModelDepth,
      [&shells, &isRound, round, weapon] (uintptr_t object, uint32_t) {
        auto part = reinterpret_cast<uint8_t*>(object);

        if (part == round || !isRound(part))
          return;

        for (uint8_t* parent = field<uint8_t*>(part, VrGame::ObjectParent); parent && parent != weapon;
            parent = field<uint8_t*>(parent, VrGame::ObjectParent)) {
          if (isRound(parent))
            return;
        }

        shells.push_back(part);
      });

    // The shells are culled while the weapon is emptied
    bool emptied = m_magazineState == VrMagazineState::Out;

    for (auto i = m_hiddenShells.begin(); i != m_hiddenShells.end(); ) {
      bool found = std::find(shells.begin(), shells.end(), *i) != shells.end();

      if (found && emptied) {
        ++i;
        continue;
      }

      if (found)
        field<uint32_t>(*i, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;

      i = m_hiddenShells.erase(i);
    }

    for (uint8_t* shell : shells) {
      uint32_t& flags = field<uint32_t>(shell, VrGame::ObjectFlags);

      if (emptied) {
        if (!(flags & VrGame::ObjectFlagAppCulled)) {
          flags |= VrGame::ObjectFlagAppCulled;
          m_hiddenShells.push_back(shell);
        }
        continue;
      }

      auto known = std::find_if(m_otherShells.begin(), m_otherShells.end(),
        [shell] (const OtherShell& other) { return other.part == shell; });

      float roundScale = round ? field<float>(round, VrGame::ObjectWorldScale) : 0.0f;

      // Measured next to the round while both are in the weapon
      if (inWeapon && round && roundScale > 0.0f && !(flags & VrGame::ObjectFlagAppCulled)) {
        VrGameRotation toRound = vrTranspose(readRotation(round, VrGame::ObjectWorldRotation));

        OtherShell other;
        other.part                = shell;
        other.fromRound.rotate    = toRound * readRotation(shell, VrGame::ObjectWorldRotation);
        other.fromRound.translate = toRound * (readVector(shell, VrGame::ObjectWorldTranslation)
          - readVector(round, VrGame::ObjectWorldTranslation)) * (1.0f / roundScale);

        if (known != m_otherShells.end())
          *known = other;
        else
          m_otherShells.push_back(other);
        continue;
      }

      if (pose && round && roundScale > 0.0f && known != m_otherShells.end()) {
        VrGameTransform shellPose;
        shellPose.rotate    = pose->rotate * known->fromRound.rotate;
        shellPose.translate = pose->translate + pose->rotate * known->fromRound.translate * roundScale;
        drawPart(shell, shellPose, updateData);
      }
    }
  }


  uint8_t* VrWeaponHand::placeMagazine(
          uint8_t*              root,
          uint8_t*              weapon,
          ActionKind            action,
          uint8_t*              hinge,
          uint8_t*              leftHand,
          bool                  handPlaced,
    const VrGameCameraPose&     hand,
    const uint8_t*              form,
    const VrVector3&            shift,
          VrGameTransform&      pose) {
    // A magazine culled by the VR layer is still the magazine, even if a
    // culled part no longer has a bound to be found by
    bool hiddenFound = false;
    bool shownFound  = false;

    if (m_hiddenMagazine || m_shownRound) {
      vrVisitGameObjects(reinterpret_cast<uintptr_t>(weapon), MaxModelDepth,
        [this, &hiddenFound, &shownFound] (uintptr_t object, uint32_t) {
          hiddenFound |= object == reinterpret_cast<uintptr_t>(m_hiddenMagazine);
          shownFound  |= object == reinterpret_cast<uintptr_t>(m_shownRound);
        });
    }

    // Weapons loaded round by round are loaded with the round their reload
    // animation carries in (a revolver's through its loading gate), break
    // actions with their shells and the other revolvers with their speed
    // loader
    bool looping = form && readField<uint8_t>(form, VrGame::WeaponLoopingReload);
    bool round   = action != ActionKind::None || (looping
      && readField<int8_t>(form, VrGame::WeaponAnimationType) != WeaponTypePistol);

    VrWeaponPartKind kind = action == ActionKind::Swing && !looping ? VrWeaponPartKind::Loader
      : (round ? VrWeaponPartKind::Round : VrWeaponPartKind::Magazine);

    uint8_t* magazine = hiddenFound ? m_hiddenMagazine : vrFindWeaponPart(weapon, kind);

    // A launcher is loaded with the round it fires
    if (!magazine && kind == VrWeaponPartKind::Magazine)
      magazine = vrFindWeaponPart(weapon, VrWeaponPartKind::Projectile);

    bool fired = magazine
      && vrClassifyWeaponPart(field<const char*>(magazine, VrGame::ObjectName)) == VrWeaponPartKind::Projectile;

    if (m_hiddenMagazine && (!hiddenFound || m_magazineState != VrMagazineState::Out)) {
      if (hiddenFound)
        field<uint32_t>(m_hiddenMagazine, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;

      m_hiddenMagazine = nullptr;
    }

    // A round the game hides is shown only while the hand holds it, not
    // once the loading loop has put it in its place
    bool roundIn = m_roundState == VrRoundState::Loading && m_hasReloadAnimation && m_reloadAnimation.hasLoop
      && m_roundTime >= m_reloadAnimation.loop.timing.placed;
    bool drawn = (m_magazineState == VrMagazineState::InHand || m_magazineState == VrMagazineState::Seating)
      && !roundIn;

    if (m_shownRound && (!shownFound || m_shownRound != magazine || !drawn)) {
      if (shownFound)
        field<uint32_t>(m_shownRound, VrGame::ObjectFlags) |= VrGame::ObjectFlagAppCulled;

      m_shownRound = nullptr;
    }

    if (!magazine)
      return nullptr;

    if (form != m_magazineForm) {
      m_magazineForm      = form;
      m_hasMagazineCentre = false;
      m_hasMagazineGrip   = false;
      m_hasPinch          = false;
      m_hasLockCentre     = false;
      m_loggedMagazine    = false;
      m_loggedLock        = false;
    }

    // The game's reload animation, once the weapon's model is in the hand
    if (form != m_reloadForm) {
      m_reloadForm         = form;
      m_hasReloadAnimation = false;
      m_reloadTries        = 0;
      m_reloadWait         = 0;
    }

    if (!m_hasReloadAnimation && leftHand && m_reloadTries < ReloadAnimationTries) {
      if (m_reloadWait) {
        m_reloadWait -= 1;
      } else {
        m_reloadTries += 1;
        m_reloadWait   = ReloadAnimationRetryDraws;
        // A part that opens is not racked: the hunting revolver's crane is
        // named as a lever
        m_hasReloadAnimation = vrReadReloadAnimation(player(), root, weapon, magazine, round, looping,
          action == ActionKind::None ? vrFindWeaponPart(weapon, VrWeaponPartKind::Rack) : nullptr,
          hinge, action == ActionKind::Swing,
          action == ActionKind::None ? vrFindWeaponPart(weapon, VrWeaponPartKind::Lock) : nullptr,
          leftHand, m_reloadAnimation);
      }
    }

    // The reload animations never hold their rounds well in the hand, and
    // some never carry their magazine: they are held between the fingers
    // instead
    m_pinched = kind == VrWeaponPartKind::Round
      || (m_hasReloadAnimation && m_reloadAnimation.handDistance > MagazineHandReach);

    // An action that opens is reloaded by hand only once its reload
    // animation tells how it opens
    if (action != ActionKind::None
     && !(m_hasReloadAnimation && m_reloadAnimation.round && m_reloadAnimation.hasHinge))
      return nullptr;

    // A weapon loaded round by round is reloaded by hand only with the
    // loop of its reload animation that loads one round
    if (round && looping && !(m_hasReloadAnimation && m_reloadAnimation.hasLoop))
      return nullptr;

    // Where the game draws the magazine in the weapon. Its bound is only
    // read while the game draws it there. A round is put where the reload
    // animation leaves it, which may not be where its part rests.
    VrGameRotation rotation = readRotation(magazine, VrGame::ObjectWorldRotation);
    VrVector3 position      = readVector(magazine, VrGame::ObjectWorldTranslation);
    float scale             = field<float>(magazine, VrGame::ObjectWorldScale);

    auto bound = field<uint8_t*>(magazine, VrGame::ObjectWorldBound);
    bool shown = !(field<uint32_t>(magazine, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled);

    if (m_magazineState == VrMagazineState::InWeapon && bound && scale > 0.0f && shown
     && field<float>(bound, sizeof(VrVector3)) > 0.0f) {
      m_magazineCentre    = vrTranspose(rotation) * (readVector(bound, 0) - position) * (1.0f / scale);
      m_hasMagazineCentre = true;
    }

    // A launcher's round is put where its reload animation leaves it: the
    // shot carried it away from its place
    if (m_hasReloadAnimation && (fired || (round && m_reloadAnimation.round))) {
      VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);

      rotation = weaponRotation * m_reloadAnimation.place.rotate;
      position = readVector(weapon, VrGame::ObjectWorldTranslation)
        + weaponRotation * m_reloadAnimation.place.translate * field<float>(weapon, VrGame::ObjectWorldScale);
    }

    VrVector3 centre = m_hasMagazineCentre ? m_magazineCentre : VrVector3();
    VrVector3 well   = position + rotation * centre * scale;

    m_magazineWell  = vrPointToHand(hand, well);
    m_hasMagazine   = true;
    m_magazineFired = fired;
    m_hasHinge     = action != ActionKind::None;
    m_swingAction  = action == ActionKind::Swing;

    // The barrel runs along the weapon node's x axis, turned down with
    // the hinge part of a break action that is open. A revolver is
    // flicked open about it, the way its part swings out.
    if (m_hasHinge) {
      VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);
      VrGameRotation barrelRotation = weaponRotation * (m_swingAction ? VrGameRotation() : m_actionTurn);
      VrVector3 barrel = { barrelRotation.m[0][0], barrelRotation.m[1][0], barrelRotation.m[2][0] };

      if (m_swingAction && vrDot(weaponRotation * m_reloadAnimation.hinge.axis, barrel) < 0.0f)
        barrel = barrel * -1.0f;

      VrVector3& axis = m_swingAction ? m_actionRoll : m_actionBarrel;
      axis = { vrDot(barrel, hand.forward), vrDot(barrel, hand.up), vrDot(barrel, hand.right) };
    }

    // The direction it comes out along: the animation's, or away from the
    // middle of the weapon
    VrVector3 axis;

    if (m_hasReloadAnimation) {
      axis = readRotation(weapon, VrGame::ObjectWorldRotation) * m_reloadAnimation.axis;
    } else if (auto weaponBound = field<const uint8_t*>(weapon, VrGame::ObjectWorldBound)) {
      axis = well - readVector(weaponBound, 0);
    }

    float axisLength = vrLength(axis);
    axis = axisLength > 1e-3f ? axis * (1.0f / axisLength) : hand.up * -1.0f;

    m_magazineAxis = { vrDot(axis, hand.forward), vrDot(axis, hand.up), vrDot(axis, hand.right) };

    // Where the left hand holds the magazine's centre, in the left
    // controller's frame, where the free pose puts the hand bone. A round
    // the game hides at rest has no bound to centre it by.
    float handScale = leftHand ? field<float>(leftHand, VrGame::ObjectWorldScale) : 1.0f;

    if (m_pinched && m_hasReloadAnimation && leftHand && handScale > 0.0f)
      pinchRound(leftHand, centre, scale / handScale);

    const VrGameTransform& inHand = magazineInHand();

    if (m_hasReloadAnimation && leftHand) {
      m_magazineHeld = FreeHandPose.translate + FreeHandPose.rotate
        * (inHand.translate * handScale + inHand.rotate * centre * scale);
    } else {
      m_magazineHeld = VrVector3();
    }

    if (!m_loggedMagazine) {
      auto name = field<const char*>(magazine, VrGame::ObjectName);
      const char* opening = m_reloadAnimation.hinge.part.c_str();

      if (action == ActionKind::Break) {
        Logger::info(str::format("VR: The weapon is a break action loaded with '", name ? name : "",
          "', opening about '", opening, "'"));
      } else if (action == ActionKind::Swing) {
        Logger::info(str::format(looping ? "VR: The weapon is a revolver loaded round by round with '"
          : "VR: The weapon is a revolver loaded with '", name ? name : "",
          looping ? "' through its '" : "', its cylinder swinging out on '", opening,
          "', flicked open about ", m_actionRoll.x, ", ", m_actionRoll.y, ", ", m_actionRoll.z, " in the hand"));
      } else {
        Logger::info(str::format(round ? "VR: The weapon is loaded round by round with '" : "VR: The weapon's magazine is '",
          name ? name : "", "'"));
      }
      m_loggedMagazine = true;
    }

    // A weapon loaded round by round with its loading loop: where a round
    // enters the port, on its way in
    bool loop = round && looping && m_hasReloadAnimation && m_reloadAnimation.hasLoop;
    VrVector3 entrance = axis * (RoundPortEntrance * VrGameUnitsPerMetre);

    m_hasRoundPort = loop && leftHand;

    if (m_hasRoundPort)
      m_roundPort = vrPointToHand(hand, well + entrance);

    if (m_magazineState == VrMagazineState::Out) {
      uint32_t& flags = field<uint32_t>(magazine, VrGame::ObjectFlags);

      // A part the game culls itself is left to the game
      if (!m_hiddenMagazine && !(flags & VrGame::ObjectFlagAppCulled)) {
        flags |= VrGame::ObjectFlagAppCulled;
        m_hiddenMagazine = magazine;
      }
      return nullptr;
    }

    if (m_magazineState == VrMagazineState::InWeapon) {
      // The game hides a launcher's round when it fires it, or when the
      // weapon is drawn empty, until it plays its reload animation or the
      // weapon is drawn loaded: it is shown in its place once a round is
      // pushed into the tube
      if (!fired)
        return nullptr;

      field<uint32_t>(magazine, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;
      pose.rotate    = rotation;
      pose.translate = position;
      return magazine;
    }

    if (loop && m_roundState == VrRoundState::Loading) {
      // From between the fingers to the port, then along the way in to its
      // place when the loading loop puts its round there; in the port it is
      // the weapon's until the loop starts again
      float placed = m_reloadAnimation.loop.timing.placed;
      float along  = placed > 0.0f ? m_roundTime / placed : 1.0f;

      if (roundIn || !leftHand)
        return nullptr;

      if (along < RoundEntranceShare) {
        float t = along / RoundEntranceShare;
        t = t * t * (3.0f - 2.0f * t);

        VrGameTransform held = magazineAtController(handScale, shift);
        pose.translate = held.translate + (position + entrance - held.translate) * t;
        pose.rotate    = vrGameRotationFromQuaternion(vrSlerp(
          vrQuaternionFromGameRotation(held.rotate), vrQuaternionFromGameRotation(rotation), t));
      } else {
        float t = (along - RoundEntranceShare) / (1.0f - RoundEntranceShare);
        pose.rotate    = rotation;
        pose.translate = position + entrance * (1.0f - t);
      }
    } else if (m_magazineState == VrMagazineState::Seating) {
      // In its well, out of its place by the seat
      pose.rotate    = rotation;
      pose.translate = position + axis * (m_magazineSeat * VrGameUnitsPerMetre);
    } else if (m_hasReloadAnimation && handPlaced) {
      // As the reload animation holds it in the hand (a round between the
      // fingers), the hand taking its free pose around its controller
      pose = magazineAtController(handScale, shift);
    } else if (m_hasOtherHand) {
      // Centred on the left controller, at the angle to it it had when it
      // was taken out of the weapon, which new magazines take too
      VrGameRotation controller = vrCameraRotation(m_otherHand);

      if (!m_hasMagazineGrip) {
        m_magazineGrip    = vrTranspose(controller) * rotation;
        m_hasMagazineGrip = true;
      }

      pose.rotate    = controller * m_magazineGrip;
      pose.translate = (m_otherHand.position - shift) - pose.rotate * centre * scale;
    } else {
      return nullptr;
    }

    if (m_magazineBlend < 1.0f) {
      float t = m_magazineBlend * m_magazineBlend * (3.0f - 2.0f * m_magazineBlend);
      VrVector3 from = m_magazineFrom.translate - shift;

      pose.translate = from + (pose.translate - from) * t;
      pose.rotate    = vrGameRotationFromQuaternion(vrSlerp(
        vrQuaternionFromGameRotation(m_magazineFrom.rotate),
        vrQuaternionFromGameRotation(pose.rotate), t));
    }

    m_magazineDrawn.rotate    = pose.rotate;
    m_magazineDrawn.translate = pose.translate + shift;
    m_hasMagazineDrawn        = true;

    uint32_t& flags = field<uint32_t>(magazine, VrGame::ObjectFlags);

    if ((round || fired) && (flags & VrGame::ObjectFlagAppCulled)) {
      flags &= ~VrGame::ObjectFlagAppCulled;
      m_shownRound = magazine;
    }

    return magazine;
  }


  uint8_t* VrWeaponHand::placeLock(
          uint8_t*              weapon,
    const VrGameCameraPose&     hand,
          void*                 updateData) {
    // Only over a magazine reloaded by hand, whose reload animation turns it
    uint8_t* lock = m_hasMagazine && !m_hasHinge && m_hasReloadAnimation && m_reloadAnimation.hasLock
      ? findObject(weapon, m_reloadAnimation.lock.part.c_str()) : nullptr;

    if (!lock)
      return nullptr;

    VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);
    VrGameRotation lockRotation   = readRotation(lock, VrGame::ObjectWorldRotation);
    VrVector3 pivot               = readVector(lock, VrGame::ObjectWorldTranslation);
    float scale                   = field<float>(lock, VrGame::ObjectWorldScale);

    // Its middle in its own frame, wherever it is drawn
    auto bound = field<uint8_t*>(lock, VrGame::ObjectWorldBound);

    if (bound && scale > 0.0f && field<float>(bound, sizeof(VrVector3)) > 0.0f) {
      m_lockCentre    = vrTranspose(lockRotation) * (readVector(bound, 0) - pivot) * (1.0f / scale);
      m_hasLockCentre = true;
    }

    if (!m_hasLockCentre)
      return nullptr;

    // Shut as the reload animation starts with it, which is not always how
    // the model has it: the grenade machinegun's lock is open in its model
    const VrHingeAnimation& animation = m_reloadAnimation.lock;
    VrVector3 axis = weaponRotation * animation.axis;
    VrVector3 arm  = weaponRotation * animation.closed.rotate * m_lockCentre * scale;

    m_lockPivot = vrPointToHand(hand, pivot);
    m_lockAxis  = { vrDot(axis, hand.forward), vrDot(axis, hand.up), vrDot(axis, hand.right) };
    m_lockArm   = { vrDot(arm, hand.forward), vrDot(arm, hand.up), vrDot(arm, hand.right) };
    m_hasLock   = true;

    if (!m_loggedLock) {
      Logger::info(str::format("VR: The weapon's magazine is under the lock '", animation.part, "', turned open by ",
        animation.angle * 57.2957795f, " degrees, its middle ", vrLength(arm) / VrGameUnitsPerMetre * 100.0f,
        " cm from its pivot"));
      m_loggedLock = true;
    }

    // The game animates it while the weapon is not reloaded by hand
    if (!m_lockDriven)
      return lock;

    // Turned about its pivot, from shut to open as the animation has it
    VrGameTransform pose;
    pose.rotate    = weaponRotation * vrGameRotationFromQuaternion(vrSlerp(
      vrQuaternionFromGameRotation(animation.closed.rotate), vrQuaternionFromGameRotation(animation.open.rotate), m_lockTurn));
    pose.translate = pivot;

    drawPart(lock, pose, updateData);
    return lock;
  }


  uint8_t* VrWeaponHand::placeRack(
          uint8_t*              weapon,
    const VrGameCameraPose&     hand,
          VrGameTransform&      pose) {
    // Only the weapons reloaded by hand are racked by hand, and the actions
    // that open are not racked
    uint8_t* rack = m_hasMagazine && !m_hasHinge ? vrFindWeaponPart(weapon, VrWeaponPartKind::Rack) : nullptr;
    auto bound    = rack ? field<const uint8_t*>(rack, VrGame::ObjectWorldBound) : nullptr;

    if (!bound)
      return nullptr;

    const VrRackAnimation* animation = m_hasReloadAnimation && m_reloadAnimation.hasRack
      ? &m_reloadAnimation.rack : nullptr;

    VrGameRotation weaponRotation = readRotation(weapon, VrGame::ObjectWorldRotation);
    VrVector3 weaponPosition      = readVector(weapon, VrGame::ObjectWorldTranslation);
    float weaponScale             = field<float>(weapon, VrGame::ObjectWorldScale);

    VrVector3 axis = weaponRotation * (animation ? animation->axis : FallbackRackAxis);
    float travel   = animation ? animation->travel : FallbackRackTravel;

    // Where the game draws it, home
    m_rackCentre = vrPointToHand(hand, readVector(bound, 0));
    m_rackAxis   = { vrDot(axis, hand.forward), vrDot(axis, hand.up), vrDot(axis, hand.right) };
    m_rackTravel = travel;
    m_hasRack    = true;

    if (rack != m_loggedRack) {
      auto name = field<const char*>(rack, VrGame::ObjectName);
      Logger::info(str::format("VR: The weapon's rack part is '", name ? name : "", "', pulled back ",
        travel * 100.0f, animation ? " cm as the reload animation does" : " cm along the barrel"));
      m_loggedRack = rack;
    }

    float pull = std::min(m_rackHeld ? m_rackPull : m_rackRest, travel);

    if (!m_rackHeld && pull <= 0.0f)
      return nullptr;

    if (animation && !animation->poses.empty()) {
      // Between the two poses of the animation the pull falls between
      const std::vector<VrRackPose>& poses = animation->poses;
      size_t next = 0;

      while (next + 1 < poses.size() && poses[next].along < pull)
        next += 1;

      VrGameTransform local = poses[next].pose;

      if (next > 0 && poses[next].along > poses[next - 1].along) {
        const VrRackPose& last = poses[next - 1];
        float t = std::clamp((pull - last.along) / (poses[next].along - last.along), 0.0f, 1.0f);

        local.translate = last.pose.translate + (local.translate - last.pose.translate) * t;
        local.rotate    = vrGameRotationFromQuaternion(vrSlerp(
          vrQuaternionFromGameRotation(last.pose.rotate),
          vrQuaternionFromGameRotation(local.rotate), t));
      }

      pose.rotate    = weaponRotation * local.rotate;
      pose.translate = weaponPosition + weaponRotation * local.translate * weaponScale;
    } else {
      pose.rotate    = readRotation(rack, VrGame::ObjectWorldRotation);
      pose.translate = readVector(rack, VrGame::ObjectWorldTranslation) + axis * (pull * VrGameUnitsPerMetre);
    }

    return rack;
  }


  void VrWeaponHand::drawPart(
          uint8_t*              part,
    const VrGameTransform&      pose,
          void*                 updateData) {
    ParentFrame parent = parentFrame(part);

    VrGameRotation localRotation  = readRotation(part, VrGame::ObjectLocalRotation);
    VrVector3 localTranslation    = readVector(part, VrGame::NodeLocalTranslate);

    writeRotation(part, VrGame::ObjectLocalRotation, vrTranspose(parent.rotate) * pose.rotate);
    writeVector(part, VrGame::NodeLocalTranslate, parent.toLocal(pose.translate));
    updateObject(part, updateData);

    // The next eye's update starts from the game's pose again
    writeRotation(part, VrGame::ObjectLocalRotation, localRotation);
    writeVector(part, VrGame::NodeLocalTranslate, localTranslation);
  }


  void VrWeaponHand::hideArms(uint8_t* root, bool rightHand) {
    uint8_t* hands[2] = {
      findObject(root, VrGame::LeftHandNodeName),
      findObject(root, VrGame::RightHandNodeName) };

    if (!hands[0] || !hands[1]) {
      if (!m_loggedArms)
        Logger::warn("VR: The hand bones were not found, the arms stay visible");

      m_loggedArms = true;
      return;
    }

    // The right hand and the weapon are shown again when they no longer
    // have to be hidden
    if (rightHand != m_hidesRightHand)
      showArms(root);

    m_hidesRightHand = rightHand;

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

    // A mesh shared by both hands stays, so that the left hand is drawn
    auto isRightHandMesh = [&arms, rightHand] (uint8_t* geometry) {
      return rightHand && isSkinnedBelow(geometry, arms[1].forearm)
        && !isSkinnedBelow(geometry, arms[0].forearm);
    };

    uint8_t* skeleton = skeletonRoot(root, arms[0].top);
    uint8_t* weaponNode = findObject(root, VrGame::WeaponNodeName);

    // The meshes are culled with the first-person camera after this update
    forEachObject(root, MaxModelDepth, [this, &arms, &isRightHandMesh, skeleton, weaponNode] (uint8_t* object) {
      uint8_t* geometry = asGeometry(object);

      if (!geometry)
        return;

      if (isArmMesh(geometry, arms)) {
        hide(geometry, !m_loggedArms);
      } else if (isBodyMesh(geometry, arms, skeleton, weaponNode)) {
        if (hide(geometry, m_loggedBodyMeshes < MaxLoggedBodyMeshes))
          m_loggedBodyMeshes++;
      } else if (isRightHandMesh(geometry)) {
        hide(geometry, !m_loggedRightHand);
      }
    });

    if (rightHand) {
      if (uint8_t* weapon = findObject(root, VrGame::WeaponNodeName))
        hide(weapon, !m_loggedRightHand);

      m_loggedRightHand = true;
    }

    if (!m_loggedArms && m_hiddenObjects.empty())
      Logger::warn("VR: No arm mesh was found, the arms stay visible");

    m_loggedArms = true;
  }


  void VrWeaponHand::hideModel(uint8_t* root) {
    // Culling the root culls the whole model. The objects culled one by
    // one are shown again first, so that they are not left culled when the
    // model is shown again.
    if (!m_hidesModel)
      showArms(root);

    hide(root, false);
    m_hidesModel = true;
  }


  bool VrWeaponHand::hide(uint8_t* object, bool log) {
    uint32_t& flags = field<uint32_t>(object, VrGame::ObjectFlags);
    bool hidden = std::find(m_hiddenObjects.begin(), m_hiddenObjects.end(), object) != m_hiddenObjects.end();

    // An object the game culls itself is left to the game, so that it is
    // not shown again when the VR layer lets go of it
    if (!hidden && (flags & VrGame::ObjectFlagAppCulled))
      return false;

    flags |= VrGame::ObjectFlagAppCulled;

    if (hidden)
      return false;

    m_hiddenObjects.push_back(object);

    if (log) {
      auto name = field<const char*>(object, VrGame::ObjectName);
      Logger::info(str::format("VR: Hiding '", name ? name : "", "'"));
    }

    return true;
  }


  void VrWeaponHand::show(uint8_t* object) {
    auto hidden = std::find(m_hiddenObjects.begin(), m_hiddenObjects.end(), object);

    if (hidden == m_hiddenObjects.end())
      return;

    field<uint32_t>(object, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;
    m_hiddenObjects.erase(hidden);
  }


  void VrWeaponHand::showArms(uint8_t* root) {
    m_hidesRightHand = false;
    m_hidesModel     = false;

    if (m_hiddenObjects.empty())
      return;

    // Objects no longer in the model may have been freed, so only the ones
    // still found in it are touched
    forEachObject(root, MaxModelDepth, [this] (uint8_t* object) {
      if (std::find(m_hiddenObjects.begin(), m_hiddenObjects.end(), object) != m_hiddenObjects.end())
        field<uint32_t>(object, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;
    });

    m_hiddenObjects.clear();
  }


  bool VrWeaponHand::settleDrawnWeapon(
          uint8_t*              root,
    const uint8_t*              weapon,
    const uint8_t*              form,
    const VrVector3&            drawnWeapon,
    const VrGameRotation&       weaponRotation) {
    if (weapon != m_drawWeapon || form != m_drawForm) {
      m_drawWeapon = weapon;
      m_drawForm   = form;
      m_drawSettle.reset();
    }

    uint8_t* rightHand = m_drawSettle.settled() ? nullptr : findObject(root, VrGame::RightHandNodeName);

    if (!rightHand || m_drawSettleUpdated)
      return m_drawSettle.away();

    m_drawSettleUpdated = true;

    VrVector3 offset = vrTranspose(weaponRotation) * (readVector(rightHand, VrGame::ObjectWorldTranslation) - drawnWeapon);

    if (m_drawSettle.update(offset * (1.0f / VrGameUnitsPerMetre), m_frameTime) && m_drawSettle.awayTime() > 0.0f) {
      auto name = form ? readField<const char*>(form, VrGame::WeaponFullName) : nullptr;
      std::string trace;

      for (uint32_t i = 0; i < m_drawSettle.traceCount(); i++)
        trace += str::format(i ? ", " : "", int(m_drawSettle.trace()[i] * 100.0f + 0.5f));

      Logger::info(str::format("VR: '", name ? name : "", "' just drawn was hidden for ", m_drawSettle.awayTime(),
        " s, while the game moved the right hand up to ", m_drawSettle.farthest(), " m from the weapon node; ",
        m_drawSettle.timedOut() ? "the wait ran out " : "it settled ", m_drawSettle.distance(),
        " m from it (cm every ", VrDrawSettle::TraceStep, " s: ", trace, ")"));
    }

    return m_drawSettle.away();
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


  void VrWeaponHand::measureBlade(const uint8_t* weapon, const VrGameCameraPose& hand) {
    auto bound = readField<const uint8_t*>(weapon, VrGame::ObjectWorldBound);

    // The hand and the weapon's bound are both where the model is drawn
    m_bladeMeasured = bound
      && vrMeasureBlade(hand, readVector(bound, 0), readField<float>(bound, sizeof(VrVector3)), m_blade);

    if (m_bladeMeasured) {
      Logger::info(str::format("VR: Melee weapon blade ", vrLength(m_blade.tip - m_blade.base) / VrGameUnitsPerMetre,
        " m long, tip at ", m_blade.tip.x, ", ", m_blade.tip.y, ", ", m_blade.tip.z, " units in the hand's frame"));
    }
  }


  void VrWeaponHand::placeThrown(
          uint8_t*              root,
          uint8_t*              weapon,
          uint8_t*              otherHand,
    const uint8_t*              form,
          int8_t                type,
    const VrGameCameraPose&     hand,
    const VrVector3&            shift,
          void*                 updateData) {
    // The model in the hand is the weapon node's first child, which the
    // game culls when it launches the weapon
    auto children = field<uint8_t**>(weapon, VrGame::NodeChildren);
    uint8_t* model = children && field<uint16_t>(weapon, VrGame::NodeChildCount) ? children[0] : nullptr;

    if (form != m_thrown.form) {
      m_thrown = VrHeldThrown();
      m_thrown.form = form;
      m_thrown.type = type;
      m_hasPinInHand  = false;
      m_loggedLighter = false;
      m_sparking      = false;
      m_loggedSparks  = false;
      m_loggedSparkState = false;
      hideSparks(nullptr);
    }

    m_hasThrown = true;

    if (!model)
      return;

    uint32_t& flags = field<uint32_t>(model, VrGame::ObjectFlags);

    if (m_showThrown) {
      flags &= ~VrGame::ObjectFlagAppCulled;
      m_showThrown = false;
    }

    m_thrown.shown  = !(flags & VrGame::ObjectFlagAppCulled);
    m_thrown.loaded = true;

    // A culled model's bound is not updated: the last one drawn is kept
    auto bound = readField<const uint8_t*>(weapon, VrGame::ObjectWorldBound);

    if (m_thrown.shown && bound) {
      m_thrown.centre = vrPointToHand(hand, readVector(bound, 0));
      m_thrown.radius = readField<float>(bound, sizeof(VrVector3));
    }

    // The model's names tell how it is armed
    uint32_t objects = vrCountWeaponObjects(weapon);

    if (form != m_loggedThrownForm || objects != m_loggedThrownObjects) {
      vrLogModelObjects(weapon, form);
      m_loggedThrownForm    = form;
      m_loggedThrownObjects = objects;
    }

    uint8_t* pin     = nullptr;
    uint8_t* lighter = nullptr;
    uint8_t* flame   = nullptr;
    uint8_t* button  = nullptr;
    uint8_t* light   = nullptr;
    bool     armed   = m_throwArm.armed;

    forEachObject(model, MaxModelDepth, [&] (uint8_t* object) {
      VrThrowPart kind = vrThrowPartKind(field<const char*>(object, VrGame::ObjectName));
      uint32_t& objectFlags = field<uint32_t>(object, VrGame::ObjectFlags);

      switch (kind) {
        case VrThrowPart::Pin:     pin     = pin     ? pin     : object; break;
        case VrThrowPart::Lighter: lighter = lighter ? lighter : object; break;
        case VrThrowPart::Flame:   flame   = flame   ? flame   : object; break;
        case VrThrowPart::Button:  button  = button  ? button  : object; break;

        // A mine's light shows whether it is armed, and is where it is
        // pressed. The game leaves it as the model has it.
        case VrThrowPart::LightOn:
        case VrThrowPart::LightOff:
          if ((kind == VrThrowPart::LightOn) == armed)
            objectFlags &= ~VrGame::ObjectFlagAppCulled;
          else
            objectFlags |= VrGame::ObjectFlagAppCulled;

          if (!light && !(objectFlags & VrGame::ObjectFlagAppCulled))
            light = object;
          break;

        case VrThrowPart::None:
          break;
      }
    });

    m_thrown.parts.pin     = pin != nullptr;
    m_thrown.parts.lighter = lighter != nullptr;
    m_thrown.parts.button  = button != nullptr;

    auto partBound = [&hand] (const uint8_t* object, VrVector3& centre, float& radius) {
      auto objectBound = readField<const uint8_t*>(object, VrGame::ObjectWorldBound);

      if (!objectBound)
        return false;

      centre = vrPointToHand(hand, readVector(objectBound, 0));
      radius = readField<float>(objectBound, sizeof(VrVector3));
      return true;
    };

    if (uint8_t* pressed = button ? button : light)
      m_thrown.hasButton = partBound(pressed, m_thrown.button, m_thrown.buttonRadius) || m_thrown.hasButton;

    // The lighter lights the largest mesh of the model besides itself: the
    // stick of dynamite, the bottle. A particle system besides the
    // lighter's gives the lit fuse's sparks, which the animation emits.
    uint8_t* fuse   = nullptr;
    uint8_t* sparks = nullptr;
    float fuseRadius = 0.0f;

    if (lighter) {
      forEachObject(model, MaxModelDepth, [&] (uint8_t* object) {
        VrVector3 centre;
        float radius = 0.0f;

        if (isAncestor(lighter, object))
          return;

        if (emitterController(object)) {
          sparks = sparks ? sparks : object;
          return;
        }

        if (asGeometry(object) && partBound(object, centre, radius) && radius > fuseRadius) {
          m_thrown.fuse = centre;
          fuse          = object;
          fuseRadius    = radius;
        }
      });
    }

    // What the game's animation does with the left hand, on the part it
    // acts on, or the whole model without one
    VrThrowAnimationNodes nodes;
    nodes.root     = root;
    nodes.leftHand = otherHand;
    nodes.target   = pin ? pin : (lighter ? fuse : (button ? button : (light ? light : model)));
    nodes.button   = button ? button : light;
    nodes.lighter  = lighter;
    nodes.flame    = flame;
    nodes.sparks   = sparks;
    readThrowAnimation(form, type, nodes);

    const VrThrowAnimation* animation = throwAnimation();

    // A switch is left as the animation leaves it once armed
    if (animation && armed && (button || light))
      poseParts(button ? button : light, animation->pressed, updateData);

    VrGameCameraPose controller = m_otherHand;
    controller.position = controller.position - shift;

    if (pin && m_hasOtherHand)
      placeThrownPin(pin, hand, vrComputeHeldTransform(controller, VrGameTransform()), updateData);

    if (!lighter) {
      hideSparks(nullptr);
      return;
    }

    if (fuseRadius > 0.0f) {
      m_thrown.hasFuse    = true;
      m_thrown.fuseRadius = fuseRadius;
    }

    if (m_hasOtherHand)
      placeLighter(lighter, flame, otherHand, controller, shift, updateData);

    emitSparks(sparks, model);
  }


  void VrWeaponHand::emitSparks(uint8_t* sparks, uint8_t* model) {
    // The fuse burns from when it is lit until the weapon leaves the hand
    uint8_t* controller = m_canEmitSparks && sparks && m_throwArm.armed && m_thrown.shown
      ? emitterController(sparks) : nullptr;

    if (!controller) {
      m_sparking = false;
      hideSparks(sparks);
      return;
    }

    // The model may cull the particle system until an animation shows it
    if (m_sparkShownSystem != sparks) {
      hideSparks(nullptr);
      m_sparkShownSystem = sparks;

      for (uint8_t* part = sparks; part && part != model && m_sparkShownCount < m_sparkShown.size();
           part = field<uint8_t*>(part, VrGame::ObjectParent)) {
        uint32_t& flags = field<uint32_t>(part, VrGame::ObjectFlags);

        if (flags & VrGame::ObjectFlagAppCulled) {
          flags &= ~VrGame::ObjectFlagAppCulled;
          m_sparkShown[m_sparkShownCount++] = part;
        }
      }

      if (m_sparkShownCount) {
        Logger::info(str::format("VR: The model culls ", m_sparkShownCount, " part(s) of the particle system '",
          field<const char*>(sparks, VrGame::ObjectName), "', shown while the fuse sparks"));
      }
    }

    // Both eyes are drawn at the same time: the second gives none
    if (m_sparkUpdated)
      return;

    m_sparkUpdated = true;

    // The game updates the model in the hand at time 0 and without its
    // controllers, so the sparks keep their own clock: the system's time
    // if the game updates the system itself, else the last frame's moved
    // by this frame's time, the system then updated here
    float systemTime = field<float>(sparks, VrGame::PSysLastUpdateTime);
    float time;

    if (m_sparking) {
      m_sparkGameClock = m_sparkGameClock || systemTime != m_sparkSeen;
      time = m_sparkGameClock ? systemTime : m_sparkLast + m_frameTime;

      if (time < m_sparkLast)
        m_sparking = false;
    }

    float rate = m_hasThrowAnimation && m_throwAnimation.sparkRate > 0.0f
      ? m_throwAnimation.sparkRate : DefaultSparkRate;

    if (!m_sparking) {
      time = systemTime == -FLT_MAX ? 0.0f : systemTime;

      m_sparking   = true;
      m_sparkGameClock = false;
      m_sparkStart = time;
      m_sparkLast  = time;
      m_sparkBegin = time;

      if (!m_loggedSparks) {
        Logger::info(str::format("VR: The lit fuse gives sparks from '", field<const char*>(sparks, VrGame::ObjectName),
          "', ", rate, " per second", m_hasThrowAnimation && m_throwAnimation.sparkRate > 0.0f ? " as the attack animation does" : ""));
        m_loggedSparks = true;
      }
    }

    // The particles already given are dropped from the start, so that the
    // game's counts stay small however long the fuse burns
    m_sparkStart += std::floor((m_sparkLast - m_sparkStart) * rate) / rate;

    reinterpret_cast<EmitParticlesFn>(VrGame::PSysEmitterCtlrEmit)(controller, nullptr,
      time, time, m_sparkLast, m_sparkStart, FLT_MAX, rate);

    if (!m_sparkGameClock) {
      auto vtable = field<const uint8_t*>(sparks, 0);
      reinterpret_cast<UpdateSystemFn>(readField<uintptr_t>(vtable, VrGame::PSysUpdateSystemSlot))(sparks, nullptr, time);
    }

    m_sparkSeen = field<float>(sparks, VrGame::PSysLastUpdateTime);
    m_sparkLast = time;

    logSparks(sparks, controller, time);
  }


  void VrWeaponHand::hideSparks(uint8_t* sparks) {
    // Culled again only while they are still above the system in the
    // hand: another model's parts may have been freed
    if (sparks && sparks == m_sparkShownSystem) {
      for (uint8_t* part = sparks; part; part = field<uint8_t*>(part, VrGame::ObjectParent)) {
        if (std::find(m_sparkShown.begin(), m_sparkShown.begin() + m_sparkShownCount, part) != m_sparkShown.begin() + m_sparkShownCount)
          field<uint32_t>(part, VrGame::ObjectFlags) |= VrGame::ObjectFlagAppCulled;
      }
    }

    m_sparkShownCount  = 0;
    m_sparkShownSystem = nullptr;
  }


  void VrWeaponHand::logSparks(uint8_t* sparks, uint8_t* controller, float time) {
    // Long enough for the first particles to be updated and drawn
    constexpr float SparkLogDelay = 0.5f;

    if (m_loggedSparkState || time - m_sparkBegin < SparkLogDelay)
      return;

    m_loggedSparkState = true;

    auto data = field<uint8_t*>(sparks, VrGame::GeometryModelData);
    float last = field<float>(sparks, VrGame::PSysLastUpdateTime);
    auto emitter = field<uint8_t*>(controller, VrGame::PSysEmitterCtlrEmitter);

    std::string culled;

    for (uint8_t* part = sparks; part; part = field<uint8_t*>(part, VrGame::ObjectParent)) {
      if (field<uint32_t>(part, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled) {
        auto name = field<const char*>(part, VrGame::ObjectName);
        culled += str::format(culled.empty() ? "" : ", ", "'", name ? name : "", "'");
      }
    }

    // The controller that updates the system, among its controllers
    constexpr uint32_t MaxControllers = 16;
    std::string update = "none";
    auto other = field<uint8_t*>(sparks, VrGame::ObjectControllers);

    for (uint32_t i = 0; other && i < MaxControllers; i++) {
      if (field<uintptr_t>(other, 0) == VrGame::PSysUpdateCtlrVtable) {
        uint16_t flags = field<uint16_t>(other, VrGame::ControllerFlags);
        update = str::format("flags ", flags, (flags & VrGame::ControllerActive) ? " (active)" : " (inactive)");
        break;
      }

      other = field<uint8_t*>(other, VrGame::ControllerNext);
    }

    auto bound = readField<const uint8_t*>(sparks, VrGame::ObjectWorldBound);

    Logger::info(str::format("VR: The fuse sparks after ", time - m_sparkBegin, " s: ",
      data ? str::format(field<uint16_t>(data, VrGame::PSysDataActiveParticles), " particle(s) active, ",
        field<uint16_t>(data, VrGame::PSysDataAddedParticles), " added, ",
        field<uint16_t>(data, VrGame::PSysDataMaxParticles), " at most, data bound radius ",
        field<float>(data, VrGame::PSysDataBoundRadius)) : std::string("no particle data"),
      ", drawn bound radius ", bound ? readField<float>(bound, sizeof(VrVector3)) : 0.0f,
      ", system updated ", last == -FLT_MAX ? std::string("never") : str::format(time - last, " s ago"),
      ", update controller ", update, m_sparkGameClock ? ", updated by the game" : ", updated here",
      ", particle lifespan ", emitter ? field<float>(emitter, VrGame::PSysEmitterLifeSpan) : 0.0f, " s",
      ", culled: ", culled.empty() ? std::string("none") : culled));
  }


  void VrWeaponHand::readThrowAnimation(
    const uint8_t*              form,
          int8_t                type,
    const VrThrowAnimationNodes& nodes) {
    if (form != m_throwAnimationForm) {
      m_throwAnimationForm  = form;
      m_hasThrowAnimation   = false;
      m_throwAnimationTries = 0;
      m_throwAnimationWait  = 0;
    }

    if (m_hasThrowAnimation || m_throwAnimationTries >= ThrowAnimationTries || !nodes.leftHand || !nodes.target)
      return;

    if (m_throwAnimationWait) {
      m_throwAnimationWait -= 1;
      return;
    }

    m_throwAnimationTries += 1;
    m_throwAnimationWait   = ReloadAnimationRetryDraws;

    // Thrown weapons have nothing to arm
    VrThrowArming arming = vrThrowArming(type, readField<const char*>(form, VrGame::WeaponModelPath), m_thrown.parts);

    if (arming == VrThrowArming::None) {
      m_throwAnimationTries = ThrowAnimationTries;
      return;
    }

    m_hasThrowAnimation = vrReadThrowAnimation(player(), form, arming, nodes, m_throwAnimation);
  }


  bool VrWeaponHand::regripThrown(uint8_t* root, uint8_t* weapon, const uint8_t* form, void* updateData) {
    if (form != m_thrownGripForm) {
      m_thrownGripForm  = form;
      m_hasThrownGrip   = false;
      m_usesThrownGrip  = false;
      m_thrownGripTries = 0;
      m_thrownGripWait  = 0;
    }

    uint8_t* rightHand = findObject(root, VrGame::RightHandNodeName);

    if (!rightHand)
      return false;

    // Read while the game holds the weapon in its idle pose, compared with
    // it, before anything is written to it
    if (!m_hasThrownGrip && m_thrownGripTries < ThrowAnimationTries && m_drawSettle.settled()) {
      if (m_thrownGripWait) {
        m_thrownGripWait -= 1;
      } else {
        m_thrownGripTries += 1;
        m_thrownGripWait   = ReloadAnimationRetryDraws;
        m_hasThrownGrip    = vrReadThrownGrip(player(), form, weapon, rightHand, m_thrownGrip);

        if (m_hasThrownGrip) {
          m_usesThrownGrip = m_thrownGrip.turn > ThrownRegripTurn;

          const char* model = readField<const char*>(form, VrGame::WeaponModelPath);
          Logger::info(str::format("VR: '", model ? model : "", "' is held ", m_usesThrownGrip
            ? "as its attack animation lets go of it" : "in its idle pose, close enough to its throw"));
        }
      }
    }

    uint8_t* parent = field<uint8_t*>(weapon, VrGame::ObjectParent);

    if (!m_usesThrownGrip || !parent)
      return false;

    // The release's pose in the hand, brought to the weapon node's parent
    // as the game holds it now
    ParentFrame hand  = { readRotation(rightHand, VrGame::ObjectWorldRotation),
                          readVector(rightHand, VrGame::ObjectWorldTranslation),
                          field<float>(rightHand, VrGame::ObjectWorldScale) };
    ParentFrame frame = parentFrame(weapon);

    VrGameRotation worldRotation = hand.rotate * m_thrownGrip.weapon.rotate;
    VrVector3      worldPosition = hand.toWorld(m_thrownGrip.weapon.translate);

    m_gripSavedRotate    = readRotation(weapon, VrGame::ObjectLocalRotation);
    m_gripSavedTranslate = readVector(weapon, VrGame::NodeLocalTranslate);
    m_gripSavedScale     = field<float>(weapon, VrGame::ObjectLocalScale);

    writeRotation(weapon, VrGame::ObjectLocalRotation, vrTranspose(frame.rotate) * worldRotation);
    writeVector(weapon, VrGame::NodeLocalTranslate, frame.toLocal(worldPosition));
    *reinterpret_cast<float*>(weapon + VrGame::ObjectLocalScale) =
      m_thrownGrip.weapon.scale * hand.scale / frame.scale;
    updateObject(weapon, updateData);
    return true;
  }


  void VrWeaponHand::restoreThrownGrip(uint8_t* weapon, void* updateData) {
    // The next eye's update starts from the game's pose again
    writeRotation(weapon, VrGame::ObjectLocalRotation, m_gripSavedRotate);
    writeVector(weapon, VrGame::NodeLocalTranslate, m_gripSavedTranslate);
    *reinterpret_cast<float*>(weapon + VrGame::ObjectLocalScale) = m_gripSavedScale;

    poseParts(weapon, m_thrownGrip.parts, updateData);
  }


  const VrLighterHold* VrWeaponHand::lighterHold(const uint8_t* form) const {
    if (!form || m_thrown.form != form)
      return nullptr;

    if (m_hasIdleLighter && m_idleLighterForm == form)
      return &m_idleLighter;

    if (m_hasThrowAnimation && m_throwAnimationForm == form && m_throwAnimation.hasLighter)
      return &m_throwAnimation.lighter;

    return nullptr;
  }


  void VrWeaponHand::measureIdleLighter(uint8_t* root, uint8_t* weapon, uint8_t* otherHand, const uint8_t* form) {
    if (form != m_idleLighterForm) {
      m_idleLighterForm   = form;
      m_hasIdleLighter    = false;
      m_loggedIdleLighter = -1;
    }

    // Once per frame, once the game has settled the weapon just drawn, and
    // kept as it was while the fuse is lit
    if (m_idleLighterUpdated || !m_drawSettle.settled() || m_throwArm.lit || m_throwArm.armed)
      return;

    m_idleLighterUpdated = true;

    uint8_t* lighter = nullptr;
    uint8_t* flame   = nullptr;

    forEachObject(weapon, MaxModelDepth, [&lighter, &flame] (uint8_t* object) {
      VrThrowPart kind = vrThrowPartKind(field<const char*>(object, VrGame::ObjectName));

      if (kind == VrThrowPart::Lighter && !lighter)
        lighter = object;
      else if (kind == VrThrowPart::Flame && !flame)
        flame = object;
    });

    auto lighterBound = lighter ? readField<const uint8_t*>(lighter, VrGame::ObjectWorldBound) : nullptr;
    float handScale   = field<float>(otherHand, VrGame::ObjectWorldScale);
    float rootScale   = field<float>(root, VrGame::ObjectWorldScale);

    if (!lighterBound || readField<float>(lighterBound, sizeof(VrVector3)) <= 0.0f || handScale <= 0.0f || rootScale <= 0.0f)
      return;

    VrGameRotation handRotation = readRotation(otherHand, VrGame::ObjectWorldRotation);
    VrVector3 handPosition      = readVector(otherHand, VrGame::ObjectWorldTranslation);
    VrGameRotation toHand       = vrTranspose(handRotation);

    VrGameRotation lighterRotation = readRotation(lighter, VrGame::ObjectWorldRotation);
    VrVector3 centre = readVector(lighterBound, 0);

    // In the root's units, as the attack animation's distance
    float distance = vrLength(centre - handPosition) / rootScale;
    bool  inReach  = distance <= VrLighterReach;

    if (int32_t(inReach) != m_loggedIdleLighter) {
      Logger::info(str::format("VR: The game's idle pose holds the lighter ", distance / VrGameUnitsPerMetre * 100.0f,
        " cm from the left hand bone", inReach ? ", the left hand holds it so" : ", out of reach of the hand"));
      m_loggedIdleLighter = int32_t(inReach);
    }

    if (!inReach)
      return;

    // Where it stands from: its meshes' centre towards its flame, or its
    // node's y axis without one
    VrVector3 up = lighterRotation * VrVector3 { 0.0f, 1.0f, 0.0f };
    auto flameBound = flame ? readField<const uint8_t*>(flame, VrGame::ObjectWorldBound) : nullptr;

    if (flameBound && readField<float>(flameBound, sizeof(VrVector3)) > 0.0f) {
      VrVector3 toFlame = readVector(flameBound, 0) - centre;

      if (vrLength(toFlame) > 0.0f)
        up = toFlame * (1.0f / vrLength(toFlame));
    }

    m_idleLighter.inHand.rotate    = toHand * lighterRotation;
    m_idleLighter.inHand.translate = (toHand * (readVector(lighter, VrGame::ObjectWorldTranslation) - handPosition)) * (1.0f / handScale);
    m_idleLighter.centre           = (toHand * (centre - handPosition)) * (1.0f / handScale);
    m_idleLighter.up               = toHand * up;

    // The fingers as the game poses them on it
    std::vector<VrBonePose>& fingers = m_idleLighter.fingers;
    fingers.clear();

    forEachObject(otherHand, MaxModelDepth, [otherHand, &fingers] (uint8_t* object) {
      auto name = field<const char*>(object, VrGame::ObjectName);

      if (object != otherHand && name)
        fingers.push_back({ name, readRotation(object, VrGame::ObjectLocalRotation) });
    });

    m_hasIdleLighter = true;
  }


  bool VrWeaponHand::thrownHeldPart(
    const uint8_t*              form,
          uint8_t*              otherHand,
    const VrVector3&            shift,
          HeldPart&             held) const {
    if (!form || m_thrown.form != form)
      return false;

    VrGameCameraPose controller = m_otherHand;
    controller.position = controller.position - shift;

    float handScale = field<float>(otherHand, VrGame::ObjectWorldScale);

    // The lighter stays in the hand as the game holds it, the fingers on
    // it. The hand at its free pose and the lighter are turned together
    // about the lighter so that it stands up in the fist, its flame along
    // the controller's up: the game's hand is not at the free pose.
    if (const VrLighterHold* hold = lighterHold(form)) {
      const VrGameTransform& inHand = hold->inHand;

      VrGameTransform lighter;
      lighter.rotate    = FreeHandPose.rotate * inHand.rotate;
      lighter.translate = FreeHandPose.translate + FreeHandPose.rotate * inHand.translate * handScale;

      VrVector3 centre = FreeHandPose.translate + FreeHandPose.rotate * hold->centre * handScale;
      lighter = vrTurnTransform(lighter, centre, FreeHandPose.rotate * hold->up, ControllerUp);

      held.pose    = vrComputeHeldTransform(controller, lighter);
      held.inHand  = &inHand;
      held.fingers = &hold->fingers;
      return true;
    }

    if (!m_hasThrowAnimation || m_throwAnimationForm != form)
      return false;

    // The fingers close on the pin as the animation's do. Around a lighter
    // standing at its fixed pose, they take the animation's grip as the
    // flame reaches the fuse rather than the player's curl.
    if (m_throwArm.pinHeld || m_thrown.parts.lighter) {
      held.pose    = vrComputeHeldTransform(controller, FreeHandPose);
      held.inHand  = &m_atHandBone;
      held.fingers = &m_throwAnimation.fingers;
      return true;
    }

    return false;
  }


  void VrWeaponHand::posePart(uint8_t* part, const VrPartPose& pose, void* updateData) {
    VrGameRotation rotation = readRotation(part, VrGame::ObjectLocalRotation);
    VrVector3 translation   = readVector(part, VrGame::NodeLocalTranslate);

    writeRotation(part, VrGame::ObjectLocalRotation, pose.pose.rotate);
    writeVector(part, VrGame::NodeLocalTranslate, pose.pose.translate);
    updateObject(part, updateData);

    // The next eye's update starts from the game's pose again
    writeRotation(part, VrGame::ObjectLocalRotation, rotation);
    writeVector(part, VrGame::NodeLocalTranslate, translation);
  }


  void VrWeaponHand::poseParts(uint8_t* node, const std::vector<VrPartPose>& poses, void* updateData) {
    // In the order they were read, parents first
    for (const VrPartPose& pose : poses) {
      if (uint8_t* part = findObject(node, pose.name.c_str()))
        posePart(part, pose, updateData);
    }
  }


  void VrWeaponHand::placeThrownPin(
          uint8_t*              pin,
    const VrGameCameraPose&     hand,
    const VrGameTransform&      controller,
          void*                 updateData) {
    // Held, the pin stays in the left hand as it was when the hand took it
    if (m_throwArm.pinHeld) {
      VrGameTransform drawn = { readRotation(pin, VrGame::ObjectWorldRotation),
                                readVector(pin, VrGame::ObjectWorldTranslation) };

      if (!m_hasPinInHand) {
        VrGameTransform toController = vrInverse(controller);
        m_pinInHand.rotate    = toController.rotate * drawn.rotate;
        m_pinInHand.translate = toController.rotate * drawn.translate + toController.translate;
        m_hasPinInHand = true;
      }

      VrGameTransform pose;
      pose.rotate    = controller.rotate * m_pinInHand.rotate;
      pose.translate = controller.rotate * m_pinInHand.translate + controller.translate;

      show(pin);
      drawPart(pin, pose, updateData);
      return;
    }

    m_hasPinInHand = false;

    // Out and let go of, it is gone
    if (m_throwArm.pinOut) {
      hide(pin, !m_loggedPin);
      m_loggedPin = true;
      return;
    }

    show(pin);

    auto bound = readField<const uint8_t*>(pin, VrGame::ObjectWorldBound);

    if (bound) {
      m_thrown.pin       = vrPointToHand(hand, readVector(bound, 0));
      m_thrown.pinRadius = readField<float>(bound, sizeof(VrVector3));
    }
  }


  void VrWeaponHand::placeLighter(
          uint8_t*              lighter,
          uint8_t*              flame,
          uint8_t*              otherHand,
    const VrGameCameraPose&     controller,
    const VrVector3&            shift,
          void*                 updateData) {
    // The game holds the lighter in the left hand, which the model does not
    // follow here: it is held in the left hand as the game holds it, or
    // stands in it at a fixed pose
    HeldPart held = { };
    const VrThrowAnimation* animation = throwAnimation();
    const VrLighterHold* hold = lighterHold(m_thrown.form);
    bool inHand = hold && thrownHeldPart(m_thrown.form, otherHand, shift, held);

    drawPart(lighter, inHand ? held.pose : vrComputeHeldTransform(controller, LighterInHand), updateData);

    // Lit, its cap is open
    if (animation && m_throwArm.lit)
      poseParts(lighter, animation->lighterLit, updateData);

    // Its flame burns while the left trigger is held. A culled object is
    // still placed, so the flame is found either way.
    uint8_t* tip = flame ? flame : lighter;
    uint32_t& flags = field<uint32_t>(tip, VrGame::ObjectFlags);

    if (flame && m_throwArm.lit)
      flags &= ~VrGame::ObjectFlagAppCulled;
    else if (flame)
      flags |= VrGame::ObjectFlagAppCulled;

    m_thrown.flame    = vrPointToHand(controller, readVector(tip, VrGame::ObjectWorldTranslation));
    m_thrown.hasFlame = true;

    // Logged again once the attack animation or the idle pose is read
    int32_t source = inHand ? (hold == &m_idleLighter ? 2 : 1) : 0;

    if (!m_loggedLighter || source != m_loggedLighterSource) {
      std::string how = ", standing at a fixed pose, the fingers as the attack animation's";

      if (inHand) {
        VrVector3 up = FreeHandPose.rotate * hold->up;
        float angle = std::acos(std::clamp(vrDot(up, ControllerUp) / std::max(vrLength(up), 1e-6f), -1.0f, 1.0f));
        how = str::format(source == 2 ? " as the game's idle pose holds it" : " as the attack animation holds it",
          ", hand and lighter turned ", angle * 180.0f / 3.14159265f, " degrees to stand it up");
      }

      Logger::info(str::format("VR: The lighter is in the left hand", how,
        ", its flame ", vrLength(m_thrown.flame) / VrGameUnitsPerMetre, " m from the controller",
        animation ? str::format(", ", animation->lighterLit.size(), " part(s) moved when lit") : std::string()));
      m_loggedLighter       = true;
      m_loggedLighterSource = source;
    }
  }


  const uint8_t* VrWeaponHand::weaponForm(const uint8_t* player) const {
    auto process = readField<uint8_t*>(player, VrGame::ActorProcess);

    if (!process)
      return nullptr;

    auto vtable = readField<const uint8_t*>(process, 0);
    auto getWeaponInfo = reinterpret_cast<GetWeaponInfoFn>(readField<uintptr_t>(vtable, VrGame::ProcessWeaponInfoSlot));
    auto entry = getWeaponInfo(process, nullptr);

    return entry ? field<uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;
  }


  int8_t VrWeaponHand::weaponType(const uint8_t* player) const {
    const uint8_t* weapon = weaponForm(player);
    return weapon ? readField<int8_t>(weapon, VrGame::WeaponAnimationType) : WeaponTypeNone;
  }


  bool VrWeaponHand::isMelee(const uint8_t* weapon) {
    int8_t type = weapon ? readField<int8_t>(weapon, VrGame::WeaponAnimationType) : WeaponTypeNone;
    return type == WeaponTypeOneHandMelee || type == WeaponTypeTwoHandMelee;
  }


  VrWeaponHand::GripKind VrWeaponHand::weaponGrip(const uint8_t* player) const {
    // One-handed melee, thrown and placed weapons are left to the game's
    // animation
    int8_t type = weaponType(player);

    if (type == WeaponTypeTwoHandMelee)
      return GripKind::TwoHandedMelee;

    if (type == WeaponTypePistol || type == WeaponTypePistolEnergy)
      return GripKind::Braced;

    if (type >= WeaponTypeRifle && type <= WeaponTypeLauncher)
      return GripKind::TwoHanded;

    return GripKind::None;
  }

}
