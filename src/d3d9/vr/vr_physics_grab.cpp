#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_math.h"
#include "vr_physics_grab.h"

namespace dxvk {

  namespace {

    // PlayerCharacter::HandlePhysicsGrab, __thiscall with the frame time
    using HandlePhysicsGrabFn = void (__fastcall*)(uint8_t* player, void* unused, float dt);

    // PlayerCharacter::ForceGrenadeHold, __thiscall
    using ForceGrenadeHoldFn  = void (__fastcall*)(uint8_t* player, void* unused);
    using SetParentCellFn     = void (__fastcall*)(uint8_t* player, void* unused, uint8_t* cell);

    using PickConstructFn    = void* (__fastcall*)(void* pick, void* unused);
    using PickSetFilterFn    = void (__fastcall*)(void* pick, void* unused, uint32_t filter);
    using PickSetPointFn     = void (__fastcall*)(void* pick, void* unused, const VrVector3* point);
    using PickObjectFn       = bool (__fastcall*)(uint8_t* world, void* unused, void* pick);
    using PickGetBodyFn      = uint8_t* (__fastcall*)(void* pick, void* unused);
    using GetFilterFn        = uint32_t* (__fastcall*)(uint8_t* actor, void* unused, uint32_t* filter);
    using ObjectForBodyFn    = uint8_t* (__cdecl*)(uint8_t* body);
    using FindReferenceFn    = uint8_t* (__cdecl*)(uint8_t* object);
    using GetPartFn          = uint8_t* (__fastcall*)(uint8_t* object, void* unused);
    using IsActorFn          = bool (__fastcall*)(uint8_t* ref, void* unused);
    using IsDeadFn           = uint32_t (__fastcall*)(uint8_t* actor, void* unused, uint32_t unknown);
    using ActivateFn         = void (__fastcall*)(uint8_t* body, void* unused);
    using UpdateFilterFn     = void (__fastcall*)(uint8_t* world, void* unused, uint8_t* entity,
                                                  int32_t mode, int32_t collections);
    using SpringInfoFn       = void* (__fastcall*)(void* info, void* unused);
    using NiNewFn            = void* (__cdecl*)(size_t size);
    using SpringConstructFn  = void* (__fastcall*)(void* spring, void* unused, const void* info);
    using SpringAssignFn     = void* (__fastcall*)(void** pointer, void* unused, void* spring);
    using SpringAddFn        = bool (__fastcall*)(void* spring, void* unused, uint8_t* world);
    using SpringRemoveFn     = bool (__fastcall*)(void* spring, void* unused);
    using SpringSetTargetFn  = void (__fastcall*)(void* spring, void* unused, const VrVector3* target);
    using SetActionFlagFn    = bool (__cdecl*)(void* form, void* extraData, uint32_t flag);
    using AddChangeFn        = void (__fastcall*)(uint8_t* ref, void* unused, uint32_t change);
    using NotifyGuardsFn     = void (__fastcall*)(void* extraData, void* unused, uint8_t* ref, uint8_t* player);
    using RemoveExtraFn      = void (__fastcall*)(void* extraData, void* unused, uint8_t type);
    using FormByIdFn         = uint8_t* (__cdecl*)(uint32_t id);
    using RefActivateFn      = bool (__fastcall*)(uint8_t* ref, void* unused, uint8_t* actor, bool flag, void* object, int32_t count);
    using GetActivateRefFn   = uint8_t* (__cdecl*)();
    using Get3DFn            = uint8_t* (__fastcall*)(uint8_t* ref, void* unused);
    using FindCollisionFn    = uint8_t* (__cdecl*)(uint8_t* object);
    using SetVelocityFn      = void (__fastcall*)(uint8_t* body, void* unused, const float* velocity);
    using SpringCallbackFn   = void (__cdecl*)(uint8_t* action, const uint8_t* stepInfo, const void* impulse);
    using ActorTestFn        = bool (__fastcall*)(uint8_t* actor, void* unused);
    using SetFeedbackActiveFn = void (__fastcall*)(uint8_t* ragdoll, void* unused, bool active);
    using DoFeedbackFn        = void (__fastcall*)(uint8_t* ragdoll, void* unused);
    using DebugLinesFn        = void (__fastcall*)(uint8_t* ragdoll, void* unused, uint32_t stage);
    using ActorMoveFn         = void* (__fastcall*)(uint8_t* actor, void* unused, float dt, VrVector3* move, uint8_t flags);
    using MoveModeFn          = uint32_t (__fastcall*)(uint8_t* mover, void* unused);
    using SetHeadingFn        = void (__fastcall*)(uint8_t* actor, void* unused, float heading);
    using GetSitStateFn       = uint32_t (__fastcall*)(uint8_t* actor, void* unused);
    using KnockExplosionFn    = void (__fastcall*)(uint8_t* process, void* unused, uint8_t* actor,
                                                   float sourceX, float sourceY, float sourceZ, float force);
    using SetSpeedFn          = void (__fastcall*)(uint8_t* actor, void* unused, float speed);
    using SetCachedDirtyFn    = void (__fastcall*)(uint8_t* process, void* unused, uint32_t flags);
    using PhantomFilterFn     = void (__fastcall*)(uint8_t* phantom, void* unused);
    using VelocityModFn       = void (__fastcall*)(uint8_t* controller, void* unused, const VrVector3* move, float seconds);
    using CollidableFilterFn  = bool* (__fastcall*)(void* filter, void* unused, bool* result, const uint8_t* a, const uint8_t* b);
    using GetAvFloatFn        = float (__fastcall*)(uint8_t* owner, void* unused, uint32_t index);
    using DamagePointsFn      = void (__fastcall*)(uint8_t* player, void* unused, float amount);
    using IsGodModeFn         = bool (__cdecl*)();
    using WeaponOutFn         = bool (__fastcall*)(uint8_t* process, void* unused);

    static_assert(sizeof(VrVector3) == 12, "The game's NiPoint3 is three floats");

    VrPhysicsGrab* g_physicsGrab = nullptr;

    // How far around the grip's centre the hand touches what it holds
    constexpr float ProbeRadius = 0.10f * VrGameUnitsPerMetre;

    // The game's grab lets go when the held point is this far from where
    // it belongs (findings/grab.md §2)
    constexpr float MaxStretch = 96.0f;

    // A living actor the player is not strong enough to keep in place pulls
    // free of the hand this far from it: about an arm's length
    constexpr float PullFreeStretch = 0.6f * VrGameUnitsPerMetre;

    // A body let go of collides with the player again once its centre is
    // this far from the player, horizontally
    constexpr float ClearDistance = 0.6f * VrGameUnitsPerMetre;

    // Factor on a held body's velocities at each Havok step. The game's
    // grab takes 0.75, which a spring can only make up for up to a slow
    // walk (findings/grab.md §10); this still stops a body held by one
    // point from swinging within a fraction of a second.
    constexpr float ObjectDamping = 0.9f;

    // The spring's target is set from the hand of the frame drawn last,
    // placed for the player of the frame before, and the step after the
    // target is set moves the player again
    constexpr float StaleFrames = 2.0f;

    // Grip of both hands on an actor, and hands, closer than this do not
    // tell a turn from the limbs' play
    constexpr float MinTwistGrip = 0.15f * VrGameUnitsPerMetre;

    // How often the lead is logged while the player moves
    constexpr float LeadLogInterval = 5.0f;
    constexpr float LeadLogMinSpeed = 1.0f * VrGameUnitsPerMetre;

    // Fastest a hand turns a body of up to VrGrabLightMass, in radians per
    // second squared: a quick turn of the wrist
    constexpr float MaxTurnAcceleration = 300.0f;

    // Largest turn of the drawn palm towards the point a hand touched
    constexpr float MaxPalmTurn = 0.6f;

    constexpr uint32_t MaxLoggedGrabs  = 50;
    constexpr uint32_t MaxLoggedMisses = 20;
    constexpr uint32_t MaxLoggedLosses = 20;
    constexpr uint32_t MaxLoggedLeads  = 10;
    constexpr uint32_t MaxLoggedFits   = 20;
    constexpr uint32_t MaxLoggedStores = 20;
    constexpr uint32_t MaxLoggedDrags  = 20;
    constexpr uint32_t MaxLoggedManhandling = 50;

    // Height of an actor's middle above its feet
    constexpr float ActorMiddleHeight = 0.9f * VrGameUnitsPerMetre;

    // How far a shove moves an actor, in how long, and the share of the
    // hand's velocity the limb touched takes
    constexpr float ShoveDistance = 0.35f * VrGameUnitsPerMetre;
    constexpr float ShoveSeconds  = 0.3f;
    constexpr float ShoveLimbShare = 0.5f;

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      T value;
      std::memcpy(&value, base + offset, sizeof(value));
      return value;
    }

    template<typename T>
    void writeField(uint8_t* base, uintptr_t offset, const T& value) {
      std::memcpy(base + offset, &value, sizeof(value));
    }

    template<typename Fn>
    Fn virtualFunction(const void* object, uintptr_t slot) {
      auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
      return reinterpret_cast<Fn>(vtable[slot / sizeof(uintptr_t)]);
    }

    float settingFloat(uintptr_t setting) {
      return readField<float>(reinterpret_cast<const uint8_t*>(setting), VrGame::SettingValue);
    }

    // Whether the game runs the actors' hit reaction itself
    bool gameRunsFeedback() {
      return readField<bool>(reinterpret_cast<const uint8_t*>(VrGame::RagdollFeedbackSetting), VrGame::SettingValue);
    }

    float actorValueOf(uint8_t* actor, uintptr_t slot, uint32_t index) {
      uint8_t* values = actor + VrGame::ActorValueOwnerBase;
      return virtualFunction<GetAvFloatFn>(values, slot)(values, nullptr, index);
    }

    float havokScale() {
      return *reinterpret_cast<const float*>(VrGame::HavokScale);
    }

    uint8_t* havokWorld(const uint8_t* object) {
      return readField<uint8_t*>(object, VrGame::HavokObjectWorld);
    }

    /**
     * \brief The Havok action of a bhkMouseSpringAction
     */
    uint8_t* springAction(void* spring) {
      return readField<uint8_t*>(static_cast<const uint8_t*>(spring), VrGame::RefObjectHavok);
    }

    /**
     * \brief The hkpWorld of a reference's cell, or \c nullptr
     */
    uint8_t* referenceWorld(uint8_t* ref, uint8_t** bhkWorld) {
      uint8_t* cell = reinterpret_cast<GetPartFn>(VrGame::RefGetParentCell)(ref, nullptr);
      uint8_t* world = cell ? reinterpret_cast<GetPartFn>(VrGame::CellGetWorld)(cell, nullptr) : nullptr;

      if (bhkWorld)
        *bhkWorld = world;

      return world ? readField<uint8_t*>(world, VrGame::RefObjectHavok) : nullptr;
    }

    /**
     * \brief A point of a body's frame in the world, both in Havok units
     */
    VrVector3 bodyPointToWorld(const uint8_t* body, const VrVector3& local) {
      VrVector3 column[3];

      for (uint32_t i = 0; i < 3; i++)
        column[i] = readField<VrVector3>(body, VrGame::BodyRotation + 0x10 * i);

      return readField<VrVector3>(body, VrGame::BodyPosition)
        + column[0] * local.x + column[1] * local.y + column[2] * local.z;
    }

    /**
     * \brief A point of the world in a body's frame, both in Havok units
     */
    VrVector3 bodyPointFromWorld(const uint8_t* body, const VrVector3& world) {
      VrVector3 offset = world - readField<VrVector3>(body, VrGame::BodyPosition);
      VrVector3 local;
      local.x = vrDot(readField<VrVector3>(body, VrGame::BodyRotation + 0x00), offset);
      local.y = vrDot(readField<VrVector3>(body, VrGame::BodyRotation + 0x10), offset);
      local.z = vrDot(readField<VrVector3>(body, VrGame::BodyRotation + 0x20), offset);
      return local;
    }

    void writeVector4(uint8_t* base, uintptr_t offset, const VrVector3& v) {
      const float values[4] = { v.x, v.y, v.z, 0.0f };
      std::memcpy(base + offset, values, sizeof(values));
    }

    void updateFilter(uint8_t* body, uint32_t filter) {
      writeField(body, VrGame::BodyFilter, filter);

      if (uint8_t* world = havokWorld(body))
        reinterpret_cast<UpdateFilterFn>(VrGame::WorldUpdateFilter)(world, nullptr, body, 0, 1);
    }

    void releaseSpring(void*& spring) {
      reinterpret_cast<SpringAssignFn>(VrGame::SpringAssign)(&spring, nullptr, nullptr);
    }

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    VrGrabFrame bodyFrame(const uint8_t* body) {
      return {
        readField<VrVector3>(body, VrGame::BodyRotation + 0x00),
        readField<VrVector3>(body, VrGame::BodyRotation + 0x10),
        readField<VrVector3>(body, VrGame::BodyRotation + 0x20) };
    }

    void setVelocity(uint8_t* body, uintptr_t setter, const VrVector3& velocity) {
      alignas(16) const float value[4] = { velocity.x, velocity.y, velocity.z, 0.0f };
      reinterpret_cast<SetVelocityFn>(setter)(body, nullptr, value);
    }

    /**
     * \brief Where a hand turns the body of its spring
     *
     * Written on the main thread, read by the turn callback on the thread
     * that steps Havok.
     */
    struct Turn {
      uint8_t*    action = nullptr;
      VrGrabFrame target;
      /// Point held, in the body's frame, Havok units
      VrVector3   local;
      VrGrabSpringConstants spring;
      float       maxAcceleration = 0.0f;
    };

    std::mutex                       g_turnLock;
    std::array<Turn, VrHandCount>    g_turns;

    /**
     * \brief Turns a held body towards its hand, after each step of its
     *    spring (hkpMouseSpringAction's applied callback)
     */
    void __cdecl turnHeldBody(uint8_t* action, const uint8_t* stepInfo, const void*) {
      std::lock_guard<std::mutex> lock(g_turnLock);

      for (const Turn& turn : g_turns) {
        if (turn.action != action)
          continue;

        uint8_t* body = readField<uint8_t*>(action, VrGame::SpringActionBody);
        float dt = readField<float>(stepInfo, VrGame::StepInfoDeltaTime);

        if (!body)
          return;

        VrVector3 angular = readField<VrVector3>(body, VrGame::BodyAngularVelocity);
        VrVector3 change  = vrGrabTurnStep(turn.spring, angular,
          vrGrabRotationBetween(bodyFrame(body), turn.target), dt, turn.maxAcceleration);

        // About the point held, which the spring keeps in the hand, rather
        // than the centre of mass
        VrVector3 arm = readField<VrVector3>(body, VrGame::BodyCentreOfMass) - bodyPointToWorld(body, turn.local);
        VrVector3 linear = readField<VrVector3>(body, VrGame::BodyLinearVelocity) + cross(change, arm);

        setVelocity(body, VrGame::BodySetAngularVelocity, angular + change);
        setVelocity(body, VrGame::BodySetLinearVelocity, linear);
        return;
      }
    }

    const SpringCallbackFn g_turnCallbacks[] = { &turnHeldBody };

    std::string hexId(uint32_t id) {
      char text[16];
      std::snprintf(text, sizeof(text), "%08X", id);
      return text;
    }

    const char* handName(VrHand hand) {
      return hand == VrHand::Left ? "left" : "right";
    }

    // What keeps an actor's hit reaction from starting, null if nothing: the
    // conditions of Actor::HitMe, except its refusal of sitting and sleeping
    // actors, whose arms a hand holds as anyone else's
    const char* reactionObstacle(uint8_t* actor) {
      uint8_t* ragdoll = readField<uint8_t*>(actor, VrGame::ActorRagdollController);

      if (!ragdoll)
        return "it has no ragdoll";

      if (!readField<bool>(ragdoll, VrGame::RagdollAnimEnabled))
        return "its ragdoll does not follow its animation";

      if (!readField<bool>(ragdoll, VrGame::RagdollFeedbackEnabled))
        return "its reaction is disabled";

      if (reinterpret_cast<ActorTestFn>(VrGame::ActorIsUnconscious)(actor, nullptr))
        return "it is unconscious";

      return nullptr;
    }


    /**
     * \brief Whether a body is simulated, as bhkRigidBody::IsMotionTypeDynamic
     *
     * A body asked to be MOTION_DYNAMIC takes one of the inertia types.
     */
    bool isDynamic(const uint8_t* body) {
      uint8_t motion = readField<uint8_t>(body, VrGame::BodyMotionType);

      return (motion >= VrGame::MotionDynamic && motion <= VrGame::MotionBoxInertia)
          || motion == VrGame::MotionThinBoxInertia;
    }

    /**
     * \brief Name of the node a body belongs to, as a skeleton's bone
     */
    const char* bodyNodeName(uint8_t* body) {
      uint8_t* object = reinterpret_cast<ObjectForBodyFn>(VrGame::CollisionObjectForBody)(body);
      uint8_t* scene  = object ? readField<uint8_t*>(object, VrGame::CollisionSceneObject) : nullptr;
      const char* name = scene ? readField<const char*>(scene, VrGame::ObjectName) : nullptr;
      return name ? name : "?";
    }

  }


  VrPhysicsGrab::~VrPhysicsGrab() {
    // The patched call stays in place and falls through to the game's
    // function. The springs are left to the game, which is shutting down.
    g_physicsGrab = nullptr;
  }


  std::unique_ptr<VrPhysicsGrab> VrPhysicsGrab::install() {
    if (g_physicsGrab)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::PickDataConstruct,
        VrGame::PickDataConstructPrologue, sizeof(VrGame::PickDataConstructPrologue))
      && VrGameMemory::matches(VrGame::FindReferenceFor3D,
        VrGame::FindReferenceFor3DPrologue, sizeof(VrGame::FindReferenceFor3DPrologue))
      && VrGameMemory::matches(VrGame::GetActivateRef,
        VrGame::GetActivateRefPrologue, sizeof(VrGame::GetActivateRefPrologue))
      && VrGameMemory::matches(VrGame::FindFirstCollisionObject,
        VrGame::FindFirstCollisionObjectPrologue, sizeof(VrGame::FindFirstCollisionObjectPrologue))
      && VrGameMemory::matches(VrGame::WorldUpdateFilter,
        VrGame::WorldUpdateFilterPrologue, sizeof(VrGame::WorldUpdateFilterPrologue))
      && VrGameMemory::matches(VrGame::SpringInfoConstruct,
        VrGame::SpringInfoConstructPrologue, sizeof(VrGame::SpringInfoConstructPrologue))
      && VrGameMemory::matches(VrGame::NiNew, VrGame::NiNewPrologue, sizeof(VrGame::NiNewPrologue))
      && VrGameMemory::matches(VrGame::SpringConstruct,
        VrGame::SpringConstructPrologue, sizeof(VrGame::SpringConstructPrologue))
      && VrGameMemory::matches(VrGame::SpringSetTarget,
        VrGame::SpringSetTargetPrologue, sizeof(VrGame::SpringSetTargetPrologue))
      && VrGameMemory::matches(VrGame::SpringCallbackLoop,
        VrGame::SpringCallbackLoopBytes, sizeof(VrGame::SpringCallbackLoopBytes))
      && VrGameMemory::matches(VrGame::BodySetLinearVelocity,
        VrGame::BodySetVelocityPrologue, sizeof(VrGame::BodySetVelocityPrologue))
      && VrGameMemory::matches(VrGame::BodySetAngularVelocity,
        VrGame::BodySetVelocityPrologue, sizeof(VrGame::BodySetVelocityPrologue))
      && VrGameMemory::readable(VrGame::HavokScale, sizeof(float))
      && VrGameMemory::callsTo(VrGame::PhysicsGrabCallSite, VrGame::HandlePhysicsGrab);

    if (!found)
      return nullptr;

    if (!VrGameMemory::redirectCall(VrGame::PhysicsGrabCallSite, VrGame::HandlePhysicsGrab,
        reinterpret_cast<const void*>(&VrPhysicsGrab::physicsGrabHook)))
      return nullptr;

    std::unique_ptr<VrPhysicsGrab> grab(new VrPhysicsGrab());
    g_physicsGrab = grab.get();

    grab->m_canStore = VrGameMemory::matches(VrGame::RefActivate,
      VrGame::RefActivatePrologue, sizeof(VrGame::RefActivatePrologue));

    // Living actors are held by the limbs of their hit reaction
    grab->m_npcLimbs = VrGameMemory::callsTo(VrGame::RagdollSetFeedbackActiveCallSite, VrGame::RagdollSetFeedbackActive)
      && VrGameMemory::callsTo(VrGame::ActorIsUnconsciousCallSite, VrGame::ActorIsUnconscious);

    // Without bRagdollFeedback, the game does not draw the reaction nor
    // end it: it runs here for the actors the hands take
    grab->m_drivesFeedback = grab->m_npcLimbs
      && VrGameMemory::callsTo(VrGame::RagdollDoFeedbackCallSite, VrGame::RagdollDoFeedback)
      && VrGameMemory::readable(VrGame::RagdollFeedbackSetting + VrGame::SettingValue, sizeof(bool))
      && VrGameMemory::redirectCall(VrGame::RagdollFeedbackPoseCallSite, VrGame::RagdollUpdateDebugLines,
           reinterpret_cast<const void*>(&VrPhysicsGrab::feedbackPoseHook));

    // The spheres are only made where they can be stopped while the
    // player is not in control
    grab->m_handBodies = VrHandBodies::create();

    if (grab->m_handBodies && !VrGameMemory::redirectCall(VrGame::NotInControlCallSite,
        VrGame::ForceGrenadeHold, reinterpret_cast<const void*>(&VrPhysicsGrab::notInControlHook)))
      grab->m_handBodies.reset();

    // Nor without leaving the old world as the player changes cell
    if (!VrGameMemory::redirectVirtual(VrGame::PlayerSetParentCellSlot, VrGame::PlayerSetParentCell,
        reinterpret_cast<const void*>(&VrPhysicsGrab::setParentCellHook))) {
      Logger::warn("VR: The player's change of cell was not found, held objects and the hands' spheres may stay in an unloaded world");
      grab->m_handBodies.reset();
    }

    // Character and Creature, not the player, whose table holds it too
    grab->m_canHoldInPlace = VrGameMemory::redirectVirtual(VrGame::CharacterMoveSlot, VrGame::ActorMove,
        reinterpret_cast<const void*>(&VrPhysicsGrab::actorMoveHook))
      && VrGameMemory::redirectVirtual(VrGame::CreatureMoveSlot, VrGame::ActorMove,
        reinterpret_cast<const void*>(&VrPhysicsGrab::actorMoveHook));

    if (!grab->m_canHoldInPlace)
      Logger::warn("VR: The actors' move was not found, hands do not keep living actors in place");

    grab->m_canDrag = grab->m_canHoldInPlace
      && VrGameMemory::redirectVirtual(VrGame::ActorMoverGetMoveModeSlot, VrGame::ActorMoverGetMoveMode,
        reinterpret_cast<const void*>(&VrPhysicsGrab::moveModeHook));

    if (grab->m_canHoldInPlace && !grab->m_canDrag)
      Logger::warn("VR: The actors' move mode was not found, hands keep living actors in place without moving them");

    // What a grip does to an actor, after PLANCK
    grab->m_canKnockDown = VrGameMemory::callsTo(VrGame::ActorCanKnockDownCallSite, VrGame::ActorCanKnockDown);

    grab->m_canSpendPoints = VrGameMemory::matches(VrGame::PlayerDamageActionPoints,
        VrGame::PlayerDamageActionPointsPrologue, sizeof(VrGame::PlayerDamageActionPointsPrologue))
      && VrGameMemory::matches(VrGame::PlayerIsGodMode,
        VrGame::PlayerIsGodModePrologue, sizeof(VrGame::PlayerIsGodModePrologue));

    grab->m_canSetSpeeds = VrGameMemory::callsTo(VrGame::ActorSetWalkSpeedCallSite, VrGame::ActorSetWalkSpeed)
      && VrGameMemory::callsTo(VrGame::ActorSetRunSpeedCallSite, VrGame::ActorSetRunSpeed)
      && VrGameMemory::callsTo(VrGame::ProcessSetCachedDirtyCallSite, VrGame::ProcessSetCachedDirty);

    grab->m_canShove = VrGameMemory::callsTo(VrGame::ControllerSetVelocityModCallSite, VrGame::ControllerSetVelocityMod)
      && VrGameMemory::callsTo(VrGame::GetCharControllerCallSite, VrGame::GetCharController);

    grab->m_canPassThrough = VrGameMemory::callsTo(VrGame::GetCharControllerCallSite, VrGame::GetCharController)
      && VrGameMemory::matches(VrGame::PhantomUpdateFilter,
        VrGame::PhantomUpdateFilterPrologue, sizeof(VrGame::PhantomUpdateFilterPrologue))
      && VrGameMemory::redirectVirtual(VrGame::CollidableFilterEnabledSlot, VrGame::CollidableFilterEnabled,
        reinterpret_cast<const void*>(&VrPhysicsGrab::collisionFilterHook));

    bool brakes = VrGameMemory::redirectVirtual(VrGame::PlayerMoveSlot, VrGame::ActorMove,
      reinterpret_cast<const void*>(&VrPhysicsGrab::playerMoveHook));

    Logger::info(str::format("VR: Hands on living actors: knock down ", grab->m_canKnockDown ? "found" : "not found",
      ", action points ", grab->m_canSpendPoints ? "found" : "not found",
      ", walk speeds ", grab->m_canSetSpeeds ? "found" : "not found",
      ", shove ", grab->m_canShove ? "found" : "not found",
      ", passing through ", grab->m_canPassThrough ? "found" : "not found",
      ", player's brake ", brakes ? "found" : "not found"));

    Logger::info("VR: Each free hand holds what it touches or pulls what it points at with its grip");
    Logger::info(!grab->m_npcLimbs
      ? "VR: The actors' hit reaction was not found, living actors are not held"
      : grab->m_drivesFeedback
        ? "VR: Living actors are held by the limbs of their hit reaction, run by the VR layer while the game's is off"
        : "VR: Living actors are held by the limbs of their hit reaction while the game runs it (bRagdollFeedback)");
    Logger::info(grab->m_canStore
      ? "VR: Items let go of at the chest go into the inventory"
      : "VR: The game's activation was not found, items let go of at the chest fall");

    if (!grab->m_handBodies)
      Logger::info("VR: The game's rigid bodies were not found, the hands do not push objects");

    return grab;
  }


  void VrPhysicsGrab::setHand(
          VrHand                hand,
    const VrGameCameraPose*     pose,
          bool                  grip,
          bool                  free,
          bool                  aimed,
          bool                  chest) {
    HandInput& input = m_input[uint32_t(hand)];
    input.tracked = pose != nullptr;
    input.grip    = grip;
    input.free    = free;
    input.aimed   = aimed;
    input.chest   = chest;

    if (pose)
      input.pose = *pose;
  }


  VrPhysicsGrabEvents VrPhysicsGrab::takeEvents() {
    VrPhysicsGrabEvents events = m_events;
    m_events = VrPhysicsGrabEvents();
    return events;
  }


  void __fastcall VrPhysicsGrab::physicsGrabHook(
          uint8_t*              player,
          void*                 unused,
          float                 dt) {
    reinterpret_cast<HandlePhysicsGrabFn>(VrGame::HandlePhysicsGrab)(player, nullptr, dt);

    if (g_physicsGrab)
      g_physicsGrab->update(player, dt);
  }


  void __fastcall VrPhysicsGrab::notInControlHook(
          uint8_t*              player,
          void*                 unused) {
    reinterpret_cast<ForceGrenadeHoldFn>(VrGame::ForceGrenadeHold)(player, nullptr);

    if (g_physicsGrab && g_physicsGrab->m_handBodies)
      g_physicsGrab->m_handBodies->stop();
  }


  void __fastcall VrPhysicsGrab::setParentCellHook(
          uint8_t*              player,
          void*                 unused,
          uint8_t*              cell) {
    if (g_physicsGrab)
      g_physicsGrab->leaveCell(cell);

    reinterpret_cast<SetParentCellFn>(VrGame::PlayerSetParentCell)(player, nullptr, cell);
  }


  void __fastcall VrPhysicsGrab::feedbackPoseHook(
          uint8_t*              ragdoll,
          void*                 unused,
          uint32_t              stage) {
    // Where DoRagdollAnim runs the reaction when the setting is on: after
    // the ragdoll's bodies were updated, before the drawn pose is set
    if (g_physicsGrab && !gameRunsFeedback() && readField<bool>(ragdoll, VrGame::RagdollFeedbackActive)
     && g_physicsGrab->drives(ragdoll))
      reinterpret_cast<DoFeedbackFn>(VrGame::RagdollDoFeedback)(ragdoll, nullptr);

    reinterpret_cast<DebugLinesFn>(VrGame::RagdollUpdateDebugLines)(ragdoll, nullptr, stage);
  }


  void* __fastcall VrPhysicsGrab::actorMoveHook(
          uint8_t*              actor,
          void*                 unused,
          float                 dt,
          VrVector3*            move,
          uint8_t               flags) {
    if (const Drag* drag = g_physicsGrab ? g_physicsGrab->dragOf(actor) : nullptr) {
      float heading = readField<float>(actor, VrGame::RefHeading);

      if (drag->setsHeading) {
        heading = drag->heading;
        virtualFunction<SetHeadingFn>(actor, VrGame::SetHeadingSlot)(actor, nullptr, heading);
      }

      // In the actor's frame; the controller still applies gravity
      VrNpcGroundVector local = vrNpcToActor({ drag->velocityX * dt, drag->velocityY * dt }, heading);
      *move = { local.x, local.y, 0.0f };
    }

    return reinterpret_cast<ActorMoveFn>(VrGame::ActorMove)(actor, nullptr, dt, move, flags);
  }


  uint32_t __fastcall VrPhysicsGrab::moveModeHook(
          uint8_t*              mover,
          void*                 unused) {
    uint32_t mode = reinterpret_cast<MoveModeFn>(VrGame::ActorMoverGetMoveMode)(mover, nullptr);

    if (!g_physicsGrab)
      return mode;

    const Drag* drag = g_physicsGrab->dragOf(readField<uint8_t*>(mover, VrGame::ActorMoverActor));

    // The animation goes with the move the hands give, nothing of what the
    // AI wanted but the state it is in
    return drag && drag->walks ? (mode & VrNpcMoveKeptFlags) | drag->moveMode : mode;
  }


  void* __fastcall VrPhysicsGrab::playerMoveHook(
          uint8_t*              player,
          void*                 unused,
          float                 dt,
          VrVector3*            move,
          uint8_t               flags) {
    if (g_physicsGrab && dt > 0.0f) {
      float speed = std::sqrt(move->x * move->x + move->y * move->y) / dt;
      float factor = vrNpcPlayerMoveFactor(speed, g_physicsGrab->m_playerSpeedLimit,
        g_physicsGrab->m_playerBrakeStrength);

      move->x *= factor;
      move->y *= factor;
    }

    return reinterpret_cast<ActorMoveFn>(VrGame::ActorMove)(player, nullptr, dt, move, flags);
  }


  bool* __fastcall VrPhysicsGrab::collisionFilterHook(
          void*                 filter,
          void*                 unused,
          bool*                 result,
    const uint8_t*              a,
    const uint8_t*              b) {
    reinterpret_cast<CollidableFilterFn>(VrGame::CollidableFilterEnabled)(filter, nullptr, result, a, b);

    if (*result && g_physicsGrab && g_physicsGrab->passesThrough(
          readField<uint32_t>(a, VrGame::CollidableFilterInfo),
          readField<uint32_t>(b, VrGame::CollidableFilterInfo)))
      *result = false;

    return result;
  }


  bool VrPhysicsGrab::passesThrough(uint32_t filterA, uint32_t filterB) const {
    uint32_t player = m_playerGroup.load(std::memory_order_relaxed);

    if (!player)
      return false;

    uint32_t own, other;

    if (filterA >> VrGame::FilterGroupShift == player) {
      own   = filterA;
      other = filterB;
    } else if (filterB >> VrGame::FilterGroupShift == player) {
      own   = filterB;
      other = filterA;
    } else {
      return false;
    }

    // The capsule only: the player's hands and what they hold are not its
    uint32_t layer = other & VrGame::FilterLayerMask;

    if ((own & VrGame::FilterLayerMask) != VrGame::LayerCharController
     || (layer != VrGame::LayerCharController && layer != VrGame::LayerBiped && layer != VrGame::LayerDeadBiped))
      return false;

    uint32_t otherGroup = other >> VrGame::FilterGroupShift;

    if (!otherGroup)
      return false;

    for (const auto& group : m_passedGroups) {
      if (group.load(std::memory_order_relaxed) == otherGroup)
        return true;
    }

    return false;
  }


  void VrPhysicsGrab::leaveCell(uint8_t* cell) {
    uint8_t* world = cell ? reinterpret_cast<GetPartFn>(VrGame::CellGetWorld)(cell, nullptr) : nullptr;
    uint8_t* hkWorld = world ? readField<uint8_t*>(world, VrGame::RefObjectHavok) : nullptr;

    // Exterior cells share one world: crossing them changes nothing
    bool released = false;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      Held& held = m_hands[i];

      if (!held.grab.holding() || (hkWorld && held.body && havokWorld(held.body) == hkWorld))
        continue;

      release(held);
      held.grab.drop();
      m_events.released[i] = true;
      released = true;
    }

    bool spheres = m_handBodies && m_handBodies->leaveWorld(hkWorld);

    if (released || spheres) {
      Logger::info(str::format("VR: The player moves to ", hkWorld ? "another physics world" : "no cell",
        released ? ", held objects are let go of" : "", spheres ? ", the hands' spheres leave the old world" : ""));
    }
  }


  void VrPhysicsGrab::update(uint8_t* player, float dt) {
    // The scale is set when the game starts Havok
    if (!(havokScale() > 0.0f))
      return;

    updateLead(player, dt);
    updateDriven();
    updateHandMotion(dt);

    for (uint32_t i = 0; i < VrHandCount; i++)
      updateHand(player, VrHand(i), dt);

    updateYanks(player, dt);
    updateDrags(player, dt);
    updateEffort(player, dt);
    updateShoves(player, dt);
    updateAnger(dt);
    updatePassingThrough(player);
    restoreFilters(player);
    updateHandBodies(player, dt);

    if (m_playerUpdateTask)
      m_playerUpdateTask(player, dt);
  }


  void VrPhysicsGrab::updateHandMotion(float dt) {
    for (uint32_t i = 0; i < VrHandCount; i++) {
      const HandInput& input = m_input[i];

      if (!input.tracked) {
        m_handMotion[i].reset();
        m_hasHandPosition[i] = false;
        continue;
      }

      if (m_hasHandPosition[i])
        m_handMotion[i].update(input.pose.position - m_handPositions[i] - m_playerStep, dt);

      m_handPositions[i]   = input.pose.position;
      m_hasHandPosition[i] = true;
    }
  }


  void VrPhysicsGrab::noteLetGo(VrHand hand, const Held& held) {
    if (!held.living || held.knocked)
      return;

    LetGo& letGo = m_letGo[uint32_t(hand)];
    letGo.actor    = held.ref;
    letGo.refId    = held.refId;
    letGo.velocity = m_handMotion[uint32_t(hand)].peak();
    letGo.age      = 0.0f;
    letGo.fresh    = true;
    letGo.kept     = held.inPlace;
  }


  void VrPhysicsGrab::updateYanks(uint8_t* player, float dt) {
    for (LetGo& letGo : m_letGo) {
      if (!letGo.fresh)
        letGo.age += dt;
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      LetGo& letGo = m_letGo[i];
      LetGo& other = m_letGo[i ^ 1u];

      if (!letGo.fresh)
        continue;

      letGo.fresh = false;

      // Decided when the last hand lets go
      if (m_hands[i ^ 1u].grab.holding() && m_hands[i ^ 1u].ref == letGo.actor)
        continue;

      // The fastest hand decides, both hands add up
      bool both = other.actor == letGo.actor && other.age <= VrNpcYankWindow;
      bool fast = vrNpcYankIsFast(letGo.velocity) || (both && vrNpcYankIsFast(other.velocity));
      bool kept = letGo.kept || (both && other.kept);
      VrVector3 velocity = both ? letGo.velocity + other.velocity : letGo.velocity;

      if (both)
        other.fresh = false;

      if (!fast || !kept || !m_canKnockDown || !m_npcReaction
       || reinterpret_cast<FormByIdFn>(VrGame::FormById)(letGo.refId) != letGo.actor
       || knockedDown(letGo.actor)
       || !reinterpret_cast<ActorTestFn>(VrGame::ActorCanKnockDown)(letGo.actor, nullptr))
        continue;

      float cost = m_npcReaction->fighting(letGo.actor) ? VrNpcYankFightingCost : VrNpcYankCost;

      if (!spendActionPoints(player, cost)) {
        if (m_loggedManhandling < MaxLoggedManhandling) {
          Logger::info(str::format("VR: No action points left to yank ", hexId(letGo.refId)));
          m_loggedManhandling++;
        }
        continue;
      }

      bool knocked = knockDown(letGo.actor, velocity);

      if (knocked) {
        m_shoveCooldown = VrNpcShoveCooldown;

        if (Handled* handled = handledEntry(letGo.actor))
          handled->anger.add(VrNpcKnockDownAnger);
      }

      if (m_loggedManhandling < MaxLoggedManhandling) {
        Logger::info(str::format("VR: A hand at ", vrLength(velocity) / VrGameUnitsPerMetre, " m/s", both ? ", both hands," : "",
          knocked ? " yanks " : " fails to yank ", hexId(letGo.refId), ", ", cost, " action points"));
        m_loggedManhandling++;
      }
    }
  }


  bool VrPhysicsGrab::knockedDown(uint8_t* actor) const {
    return virtualFunction<ActorTestFn>(actor, VrGame::ActorIsKnockedOutSlot)(actor, nullptr);
  }


  bool VrPhysicsGrab::knockDown(uint8_t* actor, const VrVector3& velocity) {
    uint8_t* process = readField<uint8_t*>(actor, VrGame::ActorProcess);

    if (!process)
      return false;

    VrVector3 centre = readField<VrVector3>(actor, VrGame::RefLocation);
    centre.z += ActorMiddleHeight;

    VrNpcKnockPush push = vrNpcYankPush(velocity, centre, havokScale());

    virtualFunction<KnockExplosionFn>(process, VrGame::ProcessKnockExplosionSlot)(process, nullptr, actor,
      push.source.x, push.source.y, push.source.z, push.force);

    return knockedDown(actor);
  }


  bool VrPhysicsGrab::spendActionPoints(uint8_t* player, float cost) const {
    if (!m_canSpendPoints || reinterpret_cast<IsGodModeFn>(VrGame::PlayerIsGodMode)())
      return true;

    float points = actorValueOf(player, VrGame::AvOwnerFloatSlot, VrGame::AvActionPoints);

    if (!(points > 0.0f))
      return false;

    reinterpret_cast<DamagePointsFn>(VrGame::PlayerDamageActionPoints)(player, nullptr, std::min(cost, points));
    return true;
  }


  void VrPhysicsGrab::updateEffort(uint8_t* player, float dt) {
    if (!m_npcReaction || !m_canSpendPoints || reinterpret_cast<IsGodModeFn>(VrGame::PlayerIsGodMode)())
      return;

    float cost = 0.0f;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const Held& held = m_hands[i];

      if (!held.grab.holding() || !held.living || m_npcReaction->teammate(held.ref))
        continue;

      // Both hands on an actor cost as one
      if (i == 1 && m_hands[0].grab.holding() && m_hands[0].ref == held.ref)
        continue;

      cost += vrNpcGripCost(m_npcReaction->fighting(held.ref), m_npcReaction->healthShare(held.ref));
    }

    if (!(cost > 0.0f))
      return;

    spendActionPoints(player, cost * dt);

    if (actorValueOf(player, VrGame::AvOwnerFloatSlot, VrGame::AvActionPoints) > 0.0f)
      return;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      Held& held = m_hands[i];

      if (!held.grab.holding() || !held.living || m_npcReaction->teammate(held.ref))
        continue;

      if (m_loggedLosses < MaxLoggedLosses) {
        Logger::info(str::format("VR: The ", handName(VrHand(i)), " hand let go of ", hexId(held.refId),
          ", the player has no action points left"));
        m_loggedLosses++;
      }

      release(held);
      held.grab.drop();
      m_events.released[i] = true;
    }
  }


  void VrPhysicsGrab::updateShoves(uint8_t* player, float dt) {
    m_shoveCooldown = std::max(m_shoveCooldown - dt, 0.0f);

    if (!m_canShove || !m_npcReaction || !canHoldLiving() || m_shoveCooldown > 0.0f)
      return;

    // A shove is for empty hands: with a weapon out the player means harm
    uint8_t* process = readField<uint8_t*>(player, VrGame::ActorProcess);

    if (!process || virtualFunction<WeaponOutFn>(process, VrGame::ProcessIsWeaponOutSlot)(process, nullptr))
      return;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const HandInput& input = m_input[i];

      if (!input.tracked || !input.free || input.grip || m_hands[i].grab.holding())
        continue;

      VrVector3 velocity = m_handMotion[i].average();

      if (!vrNpcShoveIsFast(velocity))
        continue;

      Candidate touched;

      if (!probe(player, VrHand(i), input.pose.position, touched) || !touched.living || knockedDown(touched.ref))
        continue;

      shove(player, VrHand(i), touched, velocity);
      return;
    }
  }


  void VrPhysicsGrab::shove(uint8_t* player, VrHand hand, const Candidate& touched, const VrVector3& velocity) {
    uint8_t* actor = touched.ref;
    m_shoveCooldown = VrNpcShoveCooldown;

    bool spent = spendActionPoints(player, VrNpcShoveCost);

    if (spent) {
      // The limb touched gives way, if it is one of the reaction's
      if (startReaction(actor)) {
        driveReaction(actor);

        if (isDynamic(touched.body)) {
          VrVector3 current = readField<VrVector3>(touched.body, VrGame::BodyLinearVelocity);
          setVelocity(touched.body, VrGame::BodySetLinearVelocity,
            current + velocity * (ShoveLimbShare * havokScale()));
        }
      }

      float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
      uint8_t* controller = reinterpret_cast<GetPartFn>(VrGame::GetCharController)(actor, nullptr);

      if (controller && speed > 0.0f && !virtualFunction<GetSitStateFn>(actor, VrGame::GetSitStateSlot)(actor, nullptr)) {
        VrVector3 move = { velocity.x * ShoveDistance / speed, velocity.y * ShoveDistance / speed, 0.0f };
        reinterpret_cast<VelocityModFn>(VrGame::ControllerSetVelocityMod)(controller, nullptr, &move, ShoveSeconds);
      }

      if (Handled* handled = handledEntry(actor))
        handled->anger.add(VrNpcShoveAnger);
    }

    m_npcReaction->nag(actor);
    m_events.shoved[uint32_t(hand)] = true;

    if (m_loggedManhandling < MaxLoggedManhandling) {
      Logger::info(str::format("VR: The ", handName(hand), " open hand at ", vrLength(velocity) / VrGameUnitsPerMetre,
        " m/s shoves ", hexId(readField<uint32_t>(actor, VrGame::FormId)),
        spent ? "" : ", without moving it, no action points left"));
      m_loggedManhandling++;
    }
  }


  VrPhysicsGrab::Handled* VrPhysicsGrab::handledEntry(uint8_t* actor) {
    Handled* free = nullptr;

    for (Handled& handled : m_handled) {
      if (handled.actor == actor)
        return &handled;

      if (!handled.actor && !free)
        free = &handled;
    }

    if (!free)
      return nullptr;

    *free = Handled();
    free->actor    = actor;
    free->refId    = readField<uint32_t>(actor, VrGame::FormId);
    free->forgives = m_npcReaction->forgives(actor);
    return free;
  }


  void VrPhysicsGrab::updateAnger(float dt) {
    if (!m_npcReaction)
      return;

    for (const Held& held : m_hands) {
      if (held.grab.holding() && held.living)
        handledEntry(held.ref);
    }

    for (Handled& handled : m_handled) {
      if (!handled.actor)
        continue;

      bool held = false;

      for (const Held& hand : m_hands)
        held |= hand.grab.holding() && hand.ref == handled.actor;

      if (reinterpret_cast<FormByIdFn>(VrGame::FormById)(handled.refId) != handled.actor
       || (virtualFunction<IsDeadFn>(handled.actor, VrGame::ActorIsDeadSlot)(handled.actor, nullptr, 0) & 0xFF)
       || (handled.forgives && !held)) {
        handled = Handled();
        continue;
      }

      if (handled.forgives)
        continue;

      VrNpcAngerAction action = handled.anger.update(held, m_npcReaction->fighting(handled.actor), dt);

      if (action == VrNpcAngerAction::Complain)
        m_npcReaction->nag(handled.actor);
      else if (action == VrNpcAngerAction::Assault)
        m_npcReaction->assault(handled.actor);

      if (!held && !(handled.anger.anger() > 0.0f))
        handled = Handled();
    }
  }


  void VrPhysicsGrab::updateActorPhantom(uint8_t* actor) const {
    uint8_t* controller = reinterpret_cast<GetPartFn>(VrGame::GetCharController)(actor, nullptr);
    uint8_t* phantom = controller ? readField<uint8_t*>(controller, VrGame::ControllerShapePhantom) : nullptr;

    if (phantom)
      reinterpret_cast<PhantomFilterFn>(VrGame::PhantomUpdateFilter)(phantom, nullptr);
  }


  void VrPhysicsGrab::updatePassingThrough(uint8_t* player) {
    if (!m_canPassThrough)
      return;

    uint32_t filter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &filter);
    m_playerGroup = filter >> VrGame::FilterGroupShift;

    for (const Held& held : m_hands) {
      if (!held.grab.holding() || !held.living)
        continue;

      uint32_t slot = MaxPassedThrough;

      for (uint32_t i = 0; i < MaxPassedThrough; i++) {
        if (m_passedThrough[i].actor == held.ref) {
          slot = MaxPassedThrough + 1;
          break;
        }

        if (!m_passedThrough[i].actor && slot == MaxPassedThrough)
          slot = i;
      }

      if (slot >= MaxPassedThrough)
        continue;

      uint32_t actorFilter = 0;
      reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(held.ref, nullptr, &actorFilter);

      m_passedThrough[slot].actor = held.ref;
      m_passedThrough[slot].refId = held.refId;
      m_passedGroups[slot] = actorFilter >> VrGame::FilterGroupShift;
      updateActorPhantom(held.ref);
    }

    VrVector3 playerPosition = readField<VrVector3>(player, VrGame::RefLocation);

    for (uint32_t i = 0; i < MaxPassedThrough; i++) {
      PassedThrough& passed = m_passedThrough[i];

      if (!passed.actor)
        continue;

      bool held = false;

      for (const Held& hand : m_hands)
        held |= hand.grab.holding() && hand.ref == passed.actor;

      bool valid = reinterpret_cast<FormByIdFn>(VrGame::FormById)(passed.refId) == passed.actor;

      if (valid && (held || !vrGrabClearOfPlayer(readField<VrVector3>(passed.actor, VrGame::RefLocation),
            playerPosition, ClearDistance)))
        continue;

      m_passedGroups[i] = 0;

      if (valid)
        updateActorPhantom(passed.actor);

      passed = PassedThrough();
    }
  }


  void VrPhysicsGrab::updateDrags(uint8_t* player, float dt) {
    bool  keeps = false;
    float playerLimit = 0.0f;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      Held& held = m_hands[i];
      const Held& other = m_hands[i ^ 1u];

      bool living = m_canHoldInPlace && m_npcReaction && held.grab.holding() && held.living && !held.knocked;
      bool both   = living && other.grab.holding() && other.ref == held.ref;
      float margin = living ? m_npcReaction->gripMargin(held.ref, player, both ? 2 : 1) : 0.0f;

      held.inPlace = margin > 0.0f;

      if (!held.inPlace) {
        stopDrag(m_drags[i]);
        continue;
      }

      float base   = actorValueOf(player, VrGame::AvOwnerBaseFloatSlot, VrGame::AvActionPoints);
      float points = actorValueOf(player, VrGame::AvOwnerFloatSlot, VrGame::AvActionPoints);
      float limit  = vrNpcGripSpeedLimit(margin, m_npcReaction->healthShare(held.ref),
        base > 0.0f ? points / base : 1.0f, m_npcReaction->teammate(held.ref));

      playerLimit = keeps ? std::min(playerLimit, limit) : limit;
      keeps = true;

      if (both && i == 1)
        stopDrag(m_drags[i]);
      else
        updateDrag(m_drags[i], player, held, both ? &other : nullptr, margin, limit, dt);
    }

    m_playerBrake.update(keeps, playerLimit, dt);
    m_playerSpeedLimit    = m_playerBrake.limit();
    m_playerBrakeStrength = m_playerBrake.strength();
  }


  void VrPhysicsGrab::updateDrag(Drag& drag, uint8_t* player, const Held& held, const Held* other, float margin, float limit, float dt) {
    uint8_t* actor = held.ref;
    float current  = readField<float>(actor, VrGame::RefHeading);

    if (drag.actor != actor) {
      stopDrag(drag);
      drag.logic   = VrNpcDrag();
      drag.heading = current;
      drag.refId   = held.refId;
      drag.logged  = false;
    }

    bool walks = m_canDrag && dt > 0.0f
      && !virtualFunction<GetSitStateFn>(actor, VrGame::GetSitStateSlot)(actor, nullptr)
      && !virtualFunction<ActorTestFn>(actor, VrGame::ActorIsImmobileSlot)(actor, nullptr);

    if (!walks) {
      drag.walks       = false;
      drag.setsHeading = false;
      drag.velocityX   = 0.0f;
      drag.velocityY   = 0.0f;
      drag.heading     = current;
      drag.actor       = actor;
      clearDragSpeeds(drag, actor);
      return;
    }

    // A fighter faces its target when the hands do not turn it; anyone else
    // does not turn away from the hands' grip
    bool fighting = m_npcReaction->fighting(actor);

    VrNpcDragInput input;
    input.heading  = fighting ? current : float(drag.heading);
    input.maxSpeed = limit;
    input.dt       = dt;
    input.player   = { m_playerStep.x / dt, m_playerStep.y / dt };

    VrVector3 root = readField<VrVector3>(actor, VrGame::RefLocation);

    auto pullOf = [&] (const Held& hand) {
      VrNpcGroundVector anchor = vrNpcToWorld(hand.anchor, input.heading);
      return VrNpcGroundVector { hand.target.x - root.x - anchor.x, hand.target.y - root.y - anchor.y };
    };

    input.pull = pullOf(held);

    if (other) {
      VrNpcGroundVector second = pullOf(*other);
      input.pull = { (input.pull.x + second.x) * 0.5f, (input.pull.y + second.y) * 0.5f };

      // Hands apart on the actor turn it as they turn about each other
      VrNpcGroundVector grip  = { held.anchor.x - other->anchor.x, held.anchor.y - other->anchor.y };
      VrNpcGroundVector hands = { held.target.x - other->target.x, held.target.y - other->target.y };

      if (grip.x * grip.x + grip.y * grip.y >= MinTwistGrip * MinTwistGrip
       && hands.x * hands.x + hands.y * hands.y >= MinTwistGrip * MinTwistGrip) {
        VrNpcGroundVector worldGrip = vrNpcToWorld(grip, input.heading);
        input.twist = vrNpcTurnBetween(vrNpcHeadingOf(worldGrip), vrNpcHeadingOf(hands));
      }
    }

    drag.logic.update(input);

    VrNpcGroundVector velocity = drag.logic.velocity();

    drag.velocityX   = velocity.x;
    drag.velocityY   = velocity.y;
    drag.heading     = drag.logic.heading();
    drag.setsHeading = drag.logic.turned() || !fighting;
    drag.moveMode    = drag.logic.moveMode();
    drag.walks       = true;
    drag.actor       = actor;

    // The legs at the pace the actor goes
    if (drag.moveMode)
      setDragSpeeds(drag, actor, std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y));
    else
      clearDragSpeeds(drag, actor);

    if (drag.logic.translating() && !drag.logged && m_loggedDrags < MaxLoggedDrags) {
      Logger::info(str::format("VR: The hands drag ", hexId(held.refId), ", grip exceeding its strength by ", margin,
        other ? " with both hands" : "", ", up to ", input.maxSpeed / VrGameUnitsPerMetre, " m/s"));
      drag.logged = true;
      m_loggedDrags++;
    }
  }


  void VrPhysicsGrab::stopDrag(Drag& drag) {
    uint8_t* actor = drag.actor;

    if (actor && reinterpret_cast<FormByIdFn>(VrGame::FormById)(drag.refId) == actor)
      clearDragSpeeds(drag, actor);

    drag.setsSpeeds = false;
    drag.actor      = nullptr;
  }


  void VrPhysicsGrab::setDragSpeeds(Drag& drag, uint8_t* actor, float speed) {
    if (!m_canSetSpeeds)
      return;

    reinterpret_cast<SetSpeedFn>(VrGame::ActorSetWalkSpeed)(actor, nullptr, speed);
    reinterpret_cast<SetSpeedFn>(VrGame::ActorSetRunSpeed)(actor, nullptr, speed);
    drag.setsSpeeds = true;
  }


  void VrPhysicsGrab::clearDragSpeeds(Drag& drag, uint8_t* actor) {
    uint8_t* process = readField<uint8_t*>(actor, VrGame::ActorProcess);

    if (drag.setsSpeeds && process)
      reinterpret_cast<SetCachedDirtyFn>(VrGame::ProcessSetCachedDirty)(process, nullptr, VrGame::CachedWalkAndRunSpeed);

    drag.setsSpeeds = false;
  }


  const VrPhysicsGrab::Drag* VrPhysicsGrab::dragOf(const uint8_t* actor) const {
    for (const Drag& drag : m_drags) {
      if (actor && drag.actor == actor)
        return &drag;
    }

    return nullptr;
  }


  void VrPhysicsGrab::updateHandBodies(uint8_t* player, float dt) {
    if (!m_handBodies)
      return;

    if (!m_handPush) {
      m_handBodies->leaveWorld(nullptr);

      if (!m_loggedNoPush) {
        Logger::info("VR: The hands do not push objects (d3d9.vrHandPush)");
        m_loggedNoPush = true;
      }
      return;
    }

    // The hands are those of the frame drawn last, the step moves the
    // player again
    VrVector3 lead = vrGrabLead(m_playerStep, StaleFrames);

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const HandInput& input = m_input[i];
      VrVector3 position = input.pose.position + lead;
      bool free = input.free && !input.grip && !m_hands[i].grab.holding();

      m_handBodies->update(player, VrHand(i), input.tracked ? &position : nullptr, free, dt);
    }
  }


  void VrPhysicsGrab::updateLead(uint8_t* player, float dt) {
    VrVector3 position = readField<VrVector3>(player, VrGame::RefLocation);
    VrVector3 step = m_hasPlayerPosition ? position - m_playerPosition : VrVector3();

    m_playerPosition    = position;
    m_hasPlayerPosition = true;
    m_playerStep        = step;
    m_lead = vrGrabLead(step, StaleFrames + vrGrabSpringLagSteps(m_spring));

    if (!m_hands[0].grab.holding() && !m_hands[1].grab.holding())
      return;

    m_leadLogTime += dt;

    if (m_loggedLeads < MaxLoggedLeads && m_leadLogTime >= LeadLogInterval
     && dt > 0.0f && vrLength(step) > LeadLogMinSpeed * dt) {
      Logger::info(str::format("VR: The player moves at ", vrLength(step) / (dt * VrGameUnitsPerMetre),
        " m/s, held objects are aimed ", int32_t(vrLength(m_lead) * 100.0f / VrGameUnitsPerMetre),
        " cm ahead of the hands"));
      m_leadLogTime = 0.0f;
      m_loggedLeads++;
    }
  }


  void VrPhysicsGrab::updateHand(uint8_t* player, VrHand hand, float dt) {
    Held& held = m_hands[uint32_t(hand)];
    const HandInput& input = m_input[uint32_t(hand)];
    m_corrections[uint32_t(hand)].valid = false;

    // The object was unloaded, taken or moved out of the world
    if (held.grab.holding() && !stillHeld(held)) {
      if (m_loggedLosses < MaxLoggedLosses) {
        Logger::info(str::format("VR: The ", handName(hand), " hand lost ", hexId(held.refId), ", which left the world"));
        m_loggedLosses++;
      }

      release(held);
      held.grab.drop();
      m_events.released[uint32_t(hand)] = true;
    }

    VrGrabHandInput grabInput;
    grabInput.hand = input.tracked ? &input.pose : nullptr;
    grabInput.grip = input.grip;
    grabInput.free = input.free;
    grabInput.dt   = dt;

    switch (held.grab.update(grabInput)) {
      case VrGrabRequest::Grab:
        if (grab(player, hand, input.pose))
          m_events.grabbed[uint32_t(hand)] = true;
        break;

      case VrGrabRequest::Release:
        // The grip opened at the chest
        if (input.chest && input.tracked && input.free && !input.grip && storable(held, hand)) {
          store(player, hand, held);
          break;
        }

        if (held.living && m_loggedLivingReleases < MaxLoggedGrabs) {
          Logger::info(str::format("VR: The ", handName(hand), " hand let go of the limb '", bodyNodeName(held.body),
            "' of ", hexId(held.refId), " after ", held.heldTime, " s, up to ",
            int32_t(held.furthest * 100.0f / VrGameUnitsPerMetre), " cm from the hand",
            input.free ? "" : ", no longer free"));
          m_loggedLivingReleases++;
        }

        noteLetGo(hand, held);
        release(held);
        held.grab.drop();
        m_events.released[uint32_t(hand)] = true;
        break;

      case VrGrabRequest::None:
        if (!held.grab.holding())
          break;

        held.heldTime += dt;

        if (held.living && !keepReaction(held)) {
          if (m_loggedLosses < MaxLoggedLosses) {
            Logger::info(str::format("VR: The ", handName(hand), " hand let go of ", hexId(held.refId),
              ", whose limb follows its animation or ragdoll again"));
            m_loggedLosses++;
          }

          release(held);
          held.grab.drop();
          m_events.released[uint32_t(hand)] = true;
        } else if (moveTarget(held, input.pose)) {
          updateTurn(hand, held, input.pose);
          updateCorrection(player, hand, held, input.pose);
        } else {
          if (m_loggedLosses < MaxLoggedLosses) {
            Logger::info(str::format("VR: The ", handName(hand), " hand let go of ", hexId(held.refId),
              held.living && !held.knocked && !held.inPlace ? ", which pulled free of the hand" : ", held too far from the hand"));
            m_loggedLosses++;
          }

          noteLetGo(hand, held);
          release(held);
          held.grab.drop();
          m_events.released[uint32_t(hand)] = true;
        }
        break;
    }
  }


  bool VrPhysicsGrab::grab(uint8_t* player, VrHand hand, const VrGameCameraPose& pose) {
    Candidate best;

    bool found = probe(player, hand, pose.position, best)
      || (m_input[uint32_t(hand)].aimed && aimTarget(hand, pose.position, best));

    if (!found) {
      if (m_loggedMisses < MaxLoggedMisses) {
        Logger::info(str::format("VR: The ", handName(hand), " hand touches nothing it can hold",
          m_input[uint32_t(hand)].aimed ? " and points at nothing it can pull" : ""));
        m_loggedMisses++;
      }

      return false;
    }

    Held& held = m_hands[uint32_t(hand)];

    if (!attach(player, hand, best))
      return false;

    if (best.pulled)
      held.grab.pull(pose, best.point, vrGrabPullSpeed(best.mass));
    else
      held.grab.hold(pose, best.point);

    // The palm turns towards the surface touched; a pulled object comes
    // to the hand's centre
    held.palm   = VrVector3();
    held.fitted = false;

    if (!best.pulled && m_hasRig[uint32_t(hand)]) {
      VrVector3 palm = vrPalmDirection(m_rigs[uint32_t(hand)], hand, pose);
      VrVector3 turn = vrPalmTurn(palm, pose.position, best.point, MaxPalmTurn);
      held.palm = { vrDot(turn, pose.forward), vrDot(turn, pose.up), vrDot(turn, pose.right) };
    }

    if (m_loggedGrabs < MaxLoggedGrabs) {
      uint32_t hands = handsOn(held.body);
      float acceleration = vrGrabMaxAcceleration(held.mass, hands, m_fullAcceleration, m_gravity);

      Logger::info(str::format("VR: The ", handName(hand), best.pulled ? " hand pulls " : " hand holds ",
        hexId(held.refId), " (", held.mass, " kg), ", int32_t(best.distance * 100.0f / VrGameUnitsPerMetre),
        " cm from the hand's centre", !best.limb ? std::string() : str::format(", the limb '", bodyNodeName(held.body),
          held.knocked ? "' of an actor knocked down" : held.living ? "' of a living actor" : "' of a corpse"),
        held.anchored ? " that follows its animation" : "",
        held.living && !held.knocked && !gameRunsFeedback() ? " whose hit reaction the VR layer runs" : "",
        hands > 1 ? ", with the other hand" : "",
        ", up to ", m_gravity > 0.0f ? acceleration / m_gravity : acceleration, m_gravity > 0.0f ? " g" : "",
        held.grouped || best.limb ? "" : ", still colliding with the player",
        best.limb ? "" : ", turned with the hand"));
      m_loggedGrabs++;
    }

    // Once, by the first hand to take the actor
    if (held.living && !held.knocked && m_npcReaction && handsOn(held.body) == 1)
      m_npcReaction->grabbed(held.ref);

    return true;
  }


  bool VrPhysicsGrab::probe(uint8_t* player, VrHand hand, const VrVector3& centre, Candidate& best) {
    uint8_t* world = nullptr;

    if (!referenceWorld(player, &world) || !world)
      return false;

    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);

    // The game's grab ray: the item pick layer and the player's group
    uint32_t group     = playerFilter >> VrGame::FilterGroupShift;
    uint32_t rayFilter = (group << VrGame::FilterGroupShift) | VrGame::LayerItemPick;
    float maxMass = settingFloat(VrGame::GrabMaxWeightSetting);
    bool found = false;

    // What the hands hold has the player's group, which the rays skip:
    // the other hand's object, and one let go of a moment ago, can be
    // held again
    showGroupedBodies(true);

    for (const VrGrabRay& ray : vrGrabProbeRays(centre, ProbeRadius)) {
      alignas(16) uint8_t pick[VrGame::PickDataSize];
      VrVector3 length = ray.to - ray.from;

      reinterpret_cast<PickConstructFn>(VrGame::PickDataConstruct)(pick, nullptr);
      reinterpret_cast<PickSetFilterFn>(VrGame::PickDataSetFilter)(pick, nullptr, rayFilter);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetFrom)(pick, nullptr, &ray.from);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetLength)(pick, nullptr, &length);

      if (!virtualFunction<PickObjectFn>(world, VrGame::WorldPickObjectSlot)(world, nullptr, pick))
        continue;

      uint8_t* body = reinterpret_cast<PickGetBodyFn>(VrGame::PickDataGetRigidBody)(pick, nullptr);

      if (!body)
        continue;

      Candidate candidate;
      candidate.point    = ray.from + length * readField<float>(pick, VrGame::PickDataHitFraction);
      candidate.distance = vrLength(candidate.point - centre);

      if (found && candidate.distance >= best.distance)
        continue;

      if (!holdable(body, maxMass, candidate))
        continue;

      best  = candidate;
      found = true;
    }

    showGroupedBodies(false);
    return found;
  }


  bool VrPhysicsGrab::aimTarget(VrHand hand, const VrVector3& centre, Candidate& candidate) {
    uint8_t* ref = reinterpret_cast<GetActivateRefFn>(VrGame::GetActivateRef)();

    if (!ref)
      return false;

    // The body the game's grab key would take
    uint8_t* scene     = virtualFunction<Get3DFn>(ref, VrGame::RefGet3DSlot)(ref, nullptr);
    uint8_t* collision = scene ? reinterpret_cast<FindCollisionFn>(VrGame::FindFirstCollisionObject)(scene) : nullptr;
    uint8_t* bhkBody   = collision ? reinterpret_cast<GetPartFn>(VrGame::CollisionObjectGetBody)(collision, nullptr) : nullptr;
    uint8_t* body      = bhkBody ? readField<uint8_t*>(bhkBody, VrGame::RefObjectHavok) : nullptr;

    if (!body || body == m_hands[uint32_t(hand) ^ 1u].body)
      return false;

    // A body is dragged by the limb a hand holds, not pulled into it
    if (!holdable(body, settingFloat(VrGame::GrabMaxWeightSetting), candidate) || candidate.ref != ref
     || candidate.limb)
      return false;

    candidate.point    = readField<VrVector3>(body, VrGame::BodyCentreOfMass) * (1.0f / havokScale());
    candidate.distance = vrLength(candidate.point - centre);
    candidate.pulled   = true;
    return true;
  }


  bool VrPhysicsGrab::holdable(uint8_t* body, float maxMass, Candidate& candidate) const {
    if (!havokWorld(body))
      return false;

    uint8_t motion = readField<uint8_t>(body, VrGame::BodyMotionType);

    if (motion == VrGame::MotionFixed)
      return false;

    uint8_t* object = reinterpret_cast<ObjectForBodyFn>(VrGame::CollisionObjectForBody)(body);
    uint8_t* scene  = object ? readField<uint8_t*>(object, VrGame::CollisionSceneObject) : nullptr;
    uint8_t* ref    = scene ? reinterpret_cast<FindReferenceFn>(VrGame::FindReferenceFor3D)(scene) : nullptr;

    if (!ref)
      return false;

    // A dead actor is held by the limb touched; its ragdoll's constraints
    // bring the rest. A living actor's limbs follow its animation
    // (keyframed) until the hand starts its hit reaction, see attach.
    bool actor  = virtualFunction<IsActorFn>(ref, VrGame::RefIsActorSlot)(ref, nullptr);
    bool living = actor && !(virtualFunction<IsDeadFn>(ref, VrGame::ActorIsDeadSlot)(ref, nullptr, 0) & 0xFF);

    if (living ? !canHoldLiving() : motion == VrGame::MotionKeyframed)
      return false;

    float inverseMass = readField<float>(body, VrGame::BodyInverseMass);

    if (!living && (!(inverseMass > 0.0f) || 1.0f / inverseMass > maxMass))
      return false;

    uint8_t* base = reinterpret_cast<GetPartFn>(VrGame::RefGetBaseForm)(ref, nullptr);

    if (base && (base == *reinterpret_cast<uint8_t* const*>(VrGame::AshPileForm1)
              || base == *reinterpret_cast<uint8_t* const*>(VrGame::AshPileForm2)))
      return false;

    candidate.body   = body;
    candidate.ref    = ref;
    candidate.mass   = inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
    candidate.limb   = actor;
    candidate.living = living;
    return true;
  }


  bool VrPhysicsGrab::startReaction(uint8_t* actor) const {
    if (reactionObstacle(actor))
      return false;

    // Started, or its timer started again
    uint8_t* ragdoll = readField<uint8_t*>(actor, VrGame::ActorRagdollController);
    reinterpret_cast<SetFeedbackActiveFn>(VrGame::RagdollSetFeedbackActive)(ragdoll, nullptr, true);
    return readField<bool>(ragdoll, VrGame::RagdollFeedbackActive);
  }


  bool VrPhysicsGrab::canHoldLiving() const {
    // A reaction nobody runs would leave the limbs loose and never drawn
    return m_npcLimbs && (m_drivesFeedback || gameRunsFeedback());
  }


  void VrPhysicsGrab::driveReaction(uint8_t* actor) {
    if (!m_drivesFeedback)
      return;

    uint8_t* ragdoll = readField<uint8_t*>(actor, VrGame::ActorRagdollController);
    uint32_t slot = MaxDriven;

    for (uint32_t i = 0; i < MaxDriven && slot == MaxDriven; i++) {
      if (m_driven[i].actor == actor || !m_driven[i].actor)
        slot = i;
    }

    // All taken: one no hand holds any longer gives its place
    for (uint32_t i = 0; i < MaxDriven && slot == MaxDriven; i++) {
      bool held = false;

      for (const Held& hand : m_hands)
        held |= hand.grab.holding() && hand.ref == m_driven[i].actor;

      if (!held)
        slot = i;
    }

    if (slot == MaxDriven)
      return;

    m_driven[slot].actor = actor;
    m_driven[slot].refId = readField<uint32_t>(actor, VrGame::FormId);
    m_drivenRagdolls[slot] = ragdoll;
  }


  void VrPhysicsGrab::updateDriven() {
    for (uint32_t i = 0; i < MaxDriven; i++) {
      Driven& driven = m_driven[i];

      if (!driven.actor)
        continue;

      // Unloaded, given another ragdoll, or the reaction ended: the actor
      // is the game's again
      uint8_t* ragdoll = m_drivenRagdolls[i];

      if (reinterpret_cast<FormByIdFn>(VrGame::FormById)(driven.refId) == driven.actor
       && readField<uint8_t*>(driven.actor, VrGame::ActorRagdollController) == ragdoll
       && ragdoll && readField<bool>(ragdoll, VrGame::RagdollFeedbackActive))
        continue;

      m_drivenRagdolls[i] = nullptr;
      driven = Driven();
    }
  }


  bool VrPhysicsGrab::drives(const uint8_t* ragdoll) const {
    for (const auto& driven : m_drivenRagdolls) {
      if (driven == ragdoll)
        return true;
    }

    return false;
  }


  bool VrPhysicsGrab::keepReaction(Held& held) {
    if (reinterpret_cast<FormByIdFn>(VrGame::FormById)(held.refId) != held.ref)
      return false;

    // Dead, or out of the reaction's bone level of detail: the limb follows
    // the animation or the whole ragdoll again
    if (virtualFunction<IsDeadFn>(held.ref, VrGame::ActorIsDeadSlot)(held.ref, nullptr, 0) & 0xFF)
      return false;

    // Knocked down the limb stays in the hand, until the actor gets up
    held.knocked = m_canKnockDown && knockedDown(held.ref);

    if (held.knocked)
      return !held.anchored && isDynamic(held.body);

    uint8_t* ragdoll = readField<uint8_t*>(held.ref, VrGame::ActorRagdollController);

    if (!ragdoll || !readField<bool>(ragdoll, VrGame::RagdollFeedbackActive)
     || (!held.anchored && !isDynamic(held.body)))
      return false;

    reinterpret_cast<SetFeedbackActiveFn>(VrGame::RagdollSetFeedbackActive)(ragdoll, nullptr, true);
    return true;
  }


  bool VrPhysicsGrab::attach(uint8_t* player, VrHand hand, const Candidate& candidate) {
    Held& held        = m_hands[uint32_t(hand)];
    const Held& other = m_hands[uint32_t(hand) ^ 1u];

    uint8_t* body = candidate.body;
    uint8_t* ref  = candidate.ref;
    bool shared   = other.grab.holding() && other.body == body;
    float mass    = candidate.mass;

    bool knocked  = false;
    bool anchored = false;

    if (candidate.living) {
      float inverseMass = readField<float>(body, VrGame::BodyInverseMass);

      knocked = m_canKnockDown && knockedDown(ref) && isDynamic(body) && inverseMass > 0.0f;

      // A tiny actor goes down at the touch of a hand
      if (!knocked && m_canKnockDown && m_npcReaction
       && vrNpcKnockedDownByGrip(m_npcReaction->maxHealth(ref))
       && reinterpret_cast<ActorTestFn>(VrGame::ActorCanKnockDown)(ref, nullptr)
       && knockDown(ref, VrVector3())) {
        inverseMass = readField<float>(body, VrGame::BodyInverseMass);
        knocked = isDynamic(body) && inverseMass > 0.0f;
      }

      if (knocked) {
        mass = 1.0f / inverseMass;
      } else {
        // A living actor's limb is only a body to pull while its hit reaction
        // runs, and only if it is one of the reaction's bones
        bool started = startReaction(ref);
        inverseMass = readField<float>(body, VrGame::BodyInverseMass);

        // Run until it ends, even if this limb cannot be held
        if (started)
          driveReaction(ref);

        bool dynamic = isDynamic(body) && inverseMass > 0.0f;

        // Another limb stays where its animation puts it, and the hand on it
        anchored = started && !dynamic && m_canHoldInPlace;

        if (!started || (!dynamic && !anchored)) {
          if (m_loggedLimbs < MaxLoggedMisses) {
            const char* obstacle = reactionObstacle(ref);

            Logger::info(str::format("VR: The ", handName(hand), " hand touches ",
              hexId(readField<uint32_t>(ref, VrGame::FormId)), started
                ? str::format(", a living actor's limb that follows its animation ('", bodyNodeName(body),
                    "', motion type ", uint32_t(readField<uint8_t>(body, VrGame::BodyMotionType)), ")")
                : str::format(", a living actor whose hit reaction cannot start: ",
                    obstacle ? obstacle : "it ended at once")));
            m_loggedLimbs++;
          }
          return false;
        }

        mass = dynamic ? 1.0f / inverseMass : 1.0f;
      }
    }

    // The game adds its spring to the world of the reference's cell
    uint8_t* world = nullptr;
    uint8_t* hkWorld = referenceWorld(ref, &world);

    if (!hkWorld || hkWorld != havokWorld(body))
      return false;

    readSpring(hkWorld);

    float scale = havokScale();
    VrVector3 point = candidate.point * scale;

    alignas(16) uint8_t info[VrGame::SpringInfoSize];
    reinterpret_cast<SpringInfoFn>(VrGame::SpringInfoConstruct)(info, nullptr);

    held.local = bodyPointFromWorld(body, point);

    writeField(info, VrGame::SpringInfoBody, body);
    writeVector4(info, VrGame::SpringInfoLocalPoint, held.local);
    writeVector4(info, VrGame::SpringInfoTarget, point);
    writeField(info, VrGame::SpringInfoDamping,       m_spring.damping);
    writeField(info, VrGame::SpringInfoElasticity,    m_spring.elasticity);
    writeField(info, VrGame::SpringInfoMaxForce,
      vrGrabMaxAcceleration(mass, shared ? 2 : 1, m_fullAcceleration, m_gravity));
    writeField(info, VrGame::SpringInfoObjectDamping, m_spring.objectDamping);

    void* memory = reinterpret_cast<NiNewFn>(VrGame::NiNew)(VrGame::SpringSize);

    if (!memory)
      return false;

    void* spring = reinterpret_cast<SpringConstructFn>(VrGame::SpringConstruct)(memory, nullptr, info);
    reinterpret_cast<SpringAssignFn>(VrGame::SpringAssign)(&held.spring, nullptr, spring);

    // Out of the world the spring only keeps the body alive
    if (!anchored)
      virtualFunction<SpringAddFn>(held.spring, VrGame::SpringAddSlot)(held.spring, nullptr, world);

    uint8_t* action = springAction(held.spring);

    if (!action || (!anchored && havokWorld(action) != hkWorld)) {
      releaseSpring(held.spring);
      return false;
    }

    held.body  = body;
    held.ref   = ref;
    held.refId = readField<uint32_t>(ref, VrGame::FormId);
    held.mass   = mass;
    held.limb   = candidate.limb;
    held.living = candidate.living;
    held.inPlace = false;
    held.knocked = knocked;
    held.anchored = anchored;

    if (held.living) {
      VrVector3 root = readField<VrVector3>(ref, VrGame::RefLocation);
      held.anchor = vrNpcToActor({ candidate.point.x - root.x, candidate.point.y - root.y },
        readField<float>(ref, VrGame::RefHeading));
      held.target = candidate.point;
    }

    held.turning = false;
    held.heldTime = 0.0f;
    held.furthest = 0.0f;

    if (!held.limb)
      addTurn(held);

    // The other hand took the body already, the game saw it taken
    if (shared) {
      held.filter        = other.filter;
      held.groupedFilter = other.groupedFilter;
      held.grouped       = other.grouped;
      return true;
    }

    // Held, the body neither pushes nor hurts the player
    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);
    uint32_t group = playerFilter >> VrGame::FilterGroupShift;

    held.filter = readField<uint32_t>(body, VrGame::BodyFilter);

    // A body let go of a moment ago still has the player's group
    for (auto r = m_released.begin(); r != m_released.end(); r++) {
      if (r->body == body) {
        held.filter = r->filter;
        releaseSpring(r->spring);
        m_released.erase(r);
        break;
      }
    }

    held.groupedFilter = (held.filter & VrGame::FilterKeptBits) | (group << VrGame::FilterGroupShift);
    held.grouped       = group != 0 && !candidate.limb;

    if (held.grouped)
      updateFilter(body, held.groupedFilter);
    else if (group == 0 && !m_loggedNoGroup) {
      Logger::warn("VR: The player has no collision group, held objects collide with the player");
      m_loggedNoGroup = true;
    }

    // As the game's grab does; a living actor is not moved for good, nor
    // taken from its owner
    void* extraData = ref + VrGame::RefExtraData;
    reinterpret_cast<SetActionFlagFn>(VrGame::SetActionFlag)(nullptr, extraData, VrGame::ActionOnGrab);

    if (held.living)
      return true;

    virtualFunction<AddChangeFn>(ref, VrGame::RefAddChangeSlot)(ref, nullptr, VrGame::ChangeMoved);
    reinterpret_cast<NotifyGuardsFn>(VrGame::NotifyGuards)(extraData, nullptr, ref, player);
    reinterpret_cast<RemoveExtraFn>(VrGame::RemoveExtra)(extraData, nullptr, VrGame::ExtraGuarded);
    return true;
  }


  void VrPhysicsGrab::readSpring(uint8_t* hkWorld) {
    m_spring.damping       = settingFloat(VrGame::SpringDampingSetting);
    m_spring.elasticity    = settingFloat(VrGame::SpringElasticitySetting);
    m_spring.objectDamping = ObjectDamping;
    m_fullAcceleration     = settingFloat(VrGame::SpringMaxForceSetting);
    m_gravity              = vrLength(readField<VrVector3>(hkWorld, VrGame::WorldGravity));

    if (!m_loggedSpring) {
      Logger::info(str::format("VR: Hand springs: damping ", m_spring.damping,
        ", elasticity ", m_spring.elasticity,
        ", maximum force ", m_fullAcceleration,
        ", object damping ", m_spring.objectDamping,
        " (the game's ", settingFloat(VrGame::SpringObjectDampingSetting), ")",
        ", gravity ", m_gravity,
        ", Havok scale ", havokScale(),
        ", aimed ", StaleFrames + vrGrabSpringLagSteps(m_spring), " frames of the player's move ahead"));
      m_loggedSpring = true;
    }
  }


  void VrPhysicsGrab::showGroupedBodies(bool own) {
    // Only read by the rays cast before the next step, which sees the
    // filters as they were
    for (const Held& held : m_hands) {
      if (held.grab.holding() && held.grouped)
        writeField(held.body, VrGame::BodyFilter, own ? held.filter : held.groupedFilter);
    }

    for (const Released& released : m_released)
      writeField(released.body, VrGame::BodyFilter, own ? released.filter : released.groupedFilter);
  }


  uint32_t VrPhysicsGrab::handsOn(const uint8_t* body) const {
    uint32_t hands = 0;

    for (const Held& held : m_hands)
      hands += held.grab.holding() && held.body == body ? 1 : 0;

    return hands;
  }


  bool VrPhysicsGrab::stillHeld(const Held& held) const {
    uint8_t* action = springAction(held.spring);
    uint8_t* world  = action ? havokWorld(action) : nullptr;

    if (held.anchored ? !havokWorld(held.body) : (!world || world != havokWorld(held.body)))
      return false;

    return reinterpret_cast<FormByIdFn>(VrGame::FormById)(held.refId) == held.ref;
  }


  bool VrPhysicsGrab::moveTarget(Held& held, const VrGameCameraPose& pose) {
    VrVector3 target = held.grab.target(pose) + m_lead;
    VrVector3 point  = bodyPointToWorld(held.body, held.local) * (1.0f / havokScale());

    held.target = target;

    float distance = vrLength(point - target);
    held.furthest = std::max(held.furthest, distance);

    if (distance > (held.living && !held.knocked && !held.inPlace ? PullFreeStretch : MaxStretch))
      return false;

    if (held.anchored)
      return true;

    // A second hand on the body, or one let go, changes its strength
    writeField(springAction(held.spring), VrGame::SpringActionMaxForce,
      vrGrabMaxAcceleration(held.mass, handsOn(held.body), m_fullAcceleration, m_gravity));

    reinterpret_cast<SpringSetTargetFn>(VrGame::SpringSetTarget)(held.spring, nullptr, &target);
    return true;
  }


  void VrPhysicsGrab::addTurn(Held& held) {
    uint8_t* action = springAction(held.spring);

    for (uint32_t i = 0; i < held.callbacks.size(); i++)
      held.callbacks[i] = readField<uint32_t>(action, VrGame::SpringActionCallbacks + 4 * i);

    // Not the action's to free; Havok steps after the player's update
    writeField(action, VrGame::SpringActionCallbacks, reinterpret_cast<uintptr_t>(g_turnCallbacks));
    writeField(action, VrGame::SpringActionCallbacks + 0x8, VrGame::HavokArrayForeignStorage | 1u);
    writeField(action, VrGame::SpringActionCallbacks + 0x4, 1u);
  }


  void VrPhysicsGrab::updateTurn(VrHand hand, Held& held, const VrGameCameraPose& pose) {
    bool turning = !held.limb && handsOn(held.body) == 1;

    if (!turning) {
      stopTurn(hand, held);
      return;
    }

    VrGrabFrame handFrame = vrGrabHandFrame(pose);

    // Grabbed, or the other hand let go: the rotation the body has now
    if (!held.turning)
      held.frame = vrGrabFrameToLocal(handFrame, bodyFrame(held.body));

    held.turning = true;

    float strength = m_fullAcceleration > 0.0f
      ? vrGrabMaxAcceleration(held.mass, 1, m_fullAcceleration, m_gravity) / m_fullAcceleration
      : 1.0f;

    std::lock_guard<std::mutex> lock(g_turnLock);
    Turn& turn = g_turns[uint32_t(hand)];
    turn.action = springAction(held.spring);
    turn.target = vrGrabFrameFromLocal(handFrame, held.frame);
    turn.local  = held.local;
    turn.spring = m_spring;
    turn.maxAcceleration = MaxTurnAcceleration * strength;
  }


  void VrPhysicsGrab::updateCorrection(uint8_t* player, VrHand hand, Held& held, const VrGameCameraPose& pose) {
    // A pulled object is on its way to the hand
    if (held.grab.pulling())
      return;

    VrVector3 point = bodyPointToWorld(held.body, held.local) * (1.0f / havokScale());

    VrGrabFrame onBody;

    if (held.turning)
      onBody = vrGrabFrameFromLocal(bodyFrame(held.body), vrGrabFrameInverse(held.frame));

    Correction& correction = m_corrections[uint32_t(hand)];
    correction.value = vrGrabHandCorrection(pose, held.grab.offset(), point, held.turning ? &onBody : nullptr);
    correction.value.palm  = held.palm;
    correction.value.pivot = held.grab.offset();
    correction.valid = true;

    // Once, where the hand keeps the object
    if (!held.fitted && m_hasRig[uint32_t(hand)])
      fitFingers(player, hand, held, vrGrabCorrectHand(pose, correction.value));
  }


  void VrPhysicsGrab::fitFingers(uint8_t* player, VrHand hand, Held& held, const VrGameCameraPose& drawn) {
    uint8_t* world = nullptr;

    if (!referenceWorld(player, &world) || !world)
      return;

    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);

    // As the probe: the held body has the player's group
    uint32_t group     = playerFilter >> VrGame::FilterGroupShift;
    uint32_t rayFilter = (group << VrGame::FilterGroupShift) | VrGame::LayerItemPick;
    uint32_t rays = 0;

    auto ray = [world, rayFilter, &rays] (const VrVector3& from, const VrVector3& to) {
      alignas(16) uint8_t pick[VrGame::PickDataSize];
      VrVector3 length = to - from;
      rays += 1;

      reinterpret_cast<PickConstructFn>(VrGame::PickDataConstruct)(pick, nullptr);
      reinterpret_cast<PickSetFilterFn>(VrGame::PickDataSetFilter)(pick, nullptr, rayFilter);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetFrom)(pick, nullptr, &from);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetLength)(pick, nullptr, &length);

      return virtualFunction<PickObjectFn>(world, VrGame::WorldPickObjectSlot)(world, nullptr, pick);
    };

    showGroupedBodies(true);
    held.fingers = vrFitFingers(m_rigs[uint32_t(hand)], hand, drawn, ray);
    showGroupedBodies(false);
    held.fitted = true;

    if (m_loggedFits < MaxLoggedFits) {
      const VrFingerCurls& c = held.fingers;
      Logger::info(str::format("VR: The ", handName(hand), " hand's fingers close on ", hexId(held.refId),
        " to ", c[0], " ", c[1], " ", c[2], " ", c[3], " ", c[4], " (thumb to little finger) with ", rays,
        " rays, the palm turned ", int32_t(vrLength(held.palm) * 57.29578f), " degrees towards it"));
      m_loggedFits++;
    }
  }


  bool VrPhysicsGrab::storable(const Held& held, VrHand hand) const {
    if (!m_canStore || held.limb || !held.ref)
      return false;

    // Not while the other hand still holds it
    const Held& other = m_hands[uint32_t(hand) ^ 1u];

    if (other.grab.holding() && other.body == held.body)
      return false;

    if (reinterpret_cast<FormByIdFn>(VrGame::FormById)(held.refId) != held.ref)
      return false;

    uint8_t* base = readField<uint8_t*>(held.ref, VrGame::RefBaseForm);

    if (!base)
      return false;

    uint8_t type = readField<uint8_t>(base, VrGame::FormType);

    for (uint8_t item : VrGame::InventoryFormTypes) {
      if (type == item)
        return true;
    }

    return false;
  }


  void VrPhysicsGrab::store(uint8_t* player, VrHand hand, Held& held) {
    uint8_t* ref   = held.ref;
    uint32_t refId = held.refId;

    // Let go first: picking the item up takes its reference out of the world
    release(held);
    held.grab.drop();

    bool taken = reinterpret_cast<RefActivateFn>(VrGame::RefActivate)(ref, nullptr, player, false, nullptr, 1);

    if (taken)
      m_events.stored[uint32_t(hand)] = true;
    else
      m_events.released[uint32_t(hand)] = true;

    if (m_loggedStores < MaxLoggedStores) {
      Logger::info(str::format("VR: The ", handName(hand), " hand let go of ", hexId(refId), " at the chest, ",
        taken ? "the game picked it up" : "the game's activation refused it, it falls"));
      m_loggedStores++;
    }
  }


  void VrPhysicsGrab::setFingerRig(VrHand hand, const VrFingerRig* rig) {
    m_hasRig[uint32_t(hand)] = rig != nullptr;

    if (rig)
      m_rigs[uint32_t(hand)] = *rig;
  }


  void VrPhysicsGrab::stopTurn(VrHand hand, Held& held) {
    held.turning = false;

    std::lock_guard<std::mutex> lock(g_turnLock);
    g_turns[uint32_t(hand)].action = nullptr;
  }


  void VrPhysicsGrab::release(Held& held) {
    if (!held.spring)
      return;

    VrHand hand = &held == &m_hands[0] ? VrHand(0) : VrHand(1);
    stopTurn(hand, held);

    if (uint8_t* action = springAction(held.spring); action && !held.limb) {
      for (uint32_t i = 0; i < held.callbacks.size(); i++)
        writeField(action, VrGame::SpringActionCallbacks + 4 * i, held.callbacks[i]);
    }

    const Held& other = &held == &m_hands[0] ? m_hands[1] : m_hands[0];
    bool shared = other.grab.holding() && other.body == held.body;

    uint8_t* action = springAction(held.spring);

    if (action && havokWorld(action)) {
      virtualFunction<SpringRemoveFn>(held.spring, VrGame::SpringRemoveSlot)(held.spring, nullptr);

      // So that it does not sleep where it was let go
      if (havokWorld(held.body))
        reinterpret_cast<ActivateFn>(VrGame::EntityActivate)(held.body, nullptr);
    }

    // Still held by the other hand, whose spring keeps the body alive
    if (shared) {
      releaseSpring(held.spring);
      held.body    = nullptr;
      held.ref     = nullptr;
      held.refId   = 0;
      held.mass    = 0.0f;
      held.grouped = false;
      return;
    }

    if (reinterpret_cast<FormByIdFn>(VrGame::FormById)(held.refId) == held.ref) {
      reinterpret_cast<SetActionFlagFn>(VrGame::SetActionFlag)(nullptr,
        held.ref + VrGame::RefExtraData, VrGame::ActionOnRelease);
    }

    // The spring keeps the body alive until its own filter is back
    if (held.grouped) {
      Released released;
      released.spring = held.spring;
      released.body   = held.body;
      released.filter = held.filter;
      released.groupedFilter = held.groupedFilter;
      m_released.push_back(released);
      held.spring = nullptr;
    } else {
      releaseSpring(held.spring);
    }

    held.body    = nullptr;
    held.ref     = nullptr;
    held.refId   = 0;
    held.mass    = 0.0f;
    held.grouped = false;
  }


  void VrPhysicsGrab::restoreFilters(uint8_t* player) {
    VrVector3 playerPosition = readField<VrVector3>(player, VrGame::RefLocation);
    float toGame = 1.0f / havokScale();

    for (size_t i = 0; i < m_released.size(); ) {
      Released& released = m_released[i];

      if (havokWorld(released.body)) {
        VrVector3 centre = readField<VrVector3>(released.body, VrGame::BodyPosition) * toGame;

        if (!vrGrabClearOfPlayer(centre, playerPosition, ClearDistance)) {
          i++;
          continue;
        }

        updateFilter(released.body, released.filter);
      }

      releaseSpring(released.spring);
      m_released.erase(m_released.begin() + i);
    }
  }

}
