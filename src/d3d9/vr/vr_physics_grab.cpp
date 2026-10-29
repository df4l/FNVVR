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
    using GetActivateRefFn   = uint8_t* (__cdecl*)();
    using Get3DFn            = uint8_t* (__fastcall*)(uint8_t* ref, void* unused);
    using FindCollisionFn    = uint8_t* (__cdecl*)(uint8_t* object);
    using SetVelocityFn      = void (__fastcall*)(uint8_t* body, void* unused, const float* velocity);
    using SpringCallbackFn   = void (__cdecl*)(uint8_t* action, const uint8_t* stepInfo, const void* impulse);

    static_assert(sizeof(VrVector3) == 12, "The game's NiPoint3 is three floats");

    VrPhysicsGrab* g_physicsGrab = nullptr;

    // How far around the grip's centre the hand touches what it holds
    constexpr float ProbeRadius = 0.10f * VrGameUnitsPerMetre;

    // The game's grab lets go when the held point is this far from where
    // it belongs (findings/grab.md §2)
    constexpr float MaxStretch = 96.0f;

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

    // How often the lead is logged while the player moves
    constexpr float LeadLogInterval = 5.0f;
    constexpr float LeadLogMinSpeed = 1.0f * VrGameUnitsPerMetre;

    // Fastest a hand turns a body of up to VrGrabLightMass, in radians per
    // second squared: a quick turn of the wrist
    constexpr float MaxTurnAcceleration = 300.0f;

    constexpr uint32_t MaxLoggedGrabs  = 50;
    constexpr uint32_t MaxLoggedMisses = 20;
    constexpr uint32_t MaxLoggedLosses = 20;
    constexpr uint32_t MaxLoggedLeads  = 10;

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

    Logger::info("VR: Each free hand holds what it touches or pulls what it points at with its grip");
    return grab;
  }


  void VrPhysicsGrab::setHand(
          VrHand                hand,
    const VrGameCameraPose*     pose,
          bool                  grip,
          bool                  free,
          bool                  aimed) {
    HandInput& input = m_input[uint32_t(hand)];
    input.tracked = pose != nullptr;
    input.grip    = grip;
    input.free    = free;
    input.aimed   = aimed;

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


  void VrPhysicsGrab::update(uint8_t* player, float dt) {
    // The scale is set when the game starts Havok
    if (!(havokScale() > 0.0f))
      return;

    updateLead(player, dt);

    for (uint32_t i = 0; i < VrHandCount; i++)
      updateHand(player, VrHand(i), dt);

    restoreFilters(player);
  }


  void VrPhysicsGrab::updateLead(uint8_t* player, float dt) {
    VrVector3 position = readField<VrVector3>(player, VrGame::RefLocation);
    VrVector3 step = m_hasPlayerPosition ? position - m_playerPosition : VrVector3();

    m_playerPosition    = position;
    m_hasPlayerPosition = true;
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
        release(held);
        held.grab.drop();
        m_events.released[uint32_t(hand)] = true;
        break;

      case VrGrabRequest::None:
        if (!held.grab.holding())
          break;

        if (moveTarget(held, input.pose)) {
          updateTurn(hand, held, input.pose);
        } else {
          if (m_loggedLosses < MaxLoggedLosses) {
            Logger::info(str::format("VR: The ", handName(hand), " hand let go of ", hexId(held.refId),
              ", held too far from the hand"));
            m_loggedLosses++;
          }

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

    if (m_loggedGrabs < MaxLoggedGrabs) {
      uint32_t hands = handsOn(held.body);
      float acceleration = vrGrabMaxAcceleration(best.mass, hands, m_fullAcceleration, m_gravity);

      Logger::info(str::format("VR: The ", handName(hand), best.pulled ? " hand pulls " : " hand holds ",
        hexId(held.refId), " (", best.mass, " kg), ", int32_t(best.distance * 100.0f / VrGameUnitsPerMetre),
        " cm from the hand's centre", best.corpse ? ", a limb of a corpse" : "",
        hands > 1 ? ", with the other hand" : "",
        ", up to ", m_gravity > 0.0f ? acceleration / m_gravity : acceleration, m_gravity > 0.0f ? " g" : "",
        held.grouped || best.corpse ? "" : ", still colliding with the player",
        best.corpse ? "" : ", turned with the hand"));
      m_loggedGrabs++;
    }

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
     || candidate.corpse)
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

    if (motion == VrGame::MotionKeyframed || motion == VrGame::MotionFixed)
      return false;

    float inverseMass = readField<float>(body, VrGame::BodyInverseMass);

    if (!(inverseMass > 0.0f) || 1.0f / inverseMass > maxMass)
      return false;

    uint8_t* object = reinterpret_cast<ObjectForBodyFn>(VrGame::CollisionObjectForBody)(body);
    uint8_t* scene  = object ? readField<uint8_t*>(object, VrGame::CollisionSceneObject) : nullptr;
    uint8_t* ref    = scene ? reinterpret_cast<FindReferenceFn>(VrGame::FindReferenceFor3D)(scene) : nullptr;

    if (!ref)
      return false;

    // The living are refused, as by the game's grab. A dead actor is held
    // by the limb touched; its ragdoll's constraints bring the rest.
    bool actor = virtualFunction<IsActorFn>(ref, VrGame::RefIsActorSlot)(ref, nullptr);

    if (actor && !(virtualFunction<IsDeadFn>(ref, VrGame::ActorIsDeadSlot)(ref, nullptr, 0) & 0xFF))
      return false;

    uint8_t* base = reinterpret_cast<GetPartFn>(VrGame::RefGetBaseForm)(ref, nullptr);

    if (base && (base == *reinterpret_cast<uint8_t* const*>(VrGame::AshPileForm1)
              || base == *reinterpret_cast<uint8_t* const*>(VrGame::AshPileForm2)))
      return false;

    candidate.body   = body;
    candidate.ref    = ref;
    candidate.mass   = 1.0f / inverseMass;
    candidate.corpse = actor;
    return true;
  }


  bool VrPhysicsGrab::attach(uint8_t* player, VrHand hand, const Candidate& candidate) {
    Held& held        = m_hands[uint32_t(hand)];
    const Held& other = m_hands[uint32_t(hand) ^ 1u];

    uint8_t* body = candidate.body;
    uint8_t* ref  = candidate.ref;
    bool shared   = other.grab.holding() && other.body == body;

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
      vrGrabMaxAcceleration(candidate.mass, shared ? 2 : 1, m_fullAcceleration, m_gravity));
    writeField(info, VrGame::SpringInfoObjectDamping, m_spring.objectDamping);

    void* memory = reinterpret_cast<NiNewFn>(VrGame::NiNew)(VrGame::SpringSize);

    if (!memory)
      return false;

    void* spring = reinterpret_cast<SpringConstructFn>(VrGame::SpringConstruct)(memory, nullptr, info);
    reinterpret_cast<SpringAssignFn>(VrGame::SpringAssign)(&held.spring, nullptr, spring);

    virtualFunction<SpringAddFn>(held.spring, VrGame::SpringAddSlot)(held.spring, nullptr, world);

    uint8_t* action = springAction(held.spring);

    if (!action || havokWorld(action) != hkWorld) {
      releaseSpring(held.spring);
      return false;
    }

    held.body  = body;
    held.ref   = ref;
    held.refId = readField<uint32_t>(ref, VrGame::FormId);
    held.mass  = candidate.mass;
    held.corpse = candidate.corpse;
    held.turning = false;

    if (!held.corpse)
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
    held.grouped       = group != 0 && !candidate.corpse;

    if (held.grouped)
      updateFilter(body, held.groupedFilter);
    else if (group == 0 && !m_loggedNoGroup) {
      Logger::warn("VR: The player has no collision group, held objects collide with the player");
      m_loggedNoGroup = true;
    }

    // As the game's grab does
    void* extraData = ref + VrGame::RefExtraData;
    reinterpret_cast<SetActionFlagFn>(VrGame::SetActionFlag)(nullptr, extraData, VrGame::ActionOnGrab);
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

    if (!world || world != havokWorld(held.body))
      return false;

    return reinterpret_cast<FormByIdFn>(VrGame::FormById)(held.refId) == held.ref;
  }


  bool VrPhysicsGrab::moveTarget(Held& held, const VrGameCameraPose& pose) {
    VrVector3 target = held.grab.target(pose) + m_lead;
    VrVector3 point  = bodyPointToWorld(held.body, held.local) * (1.0f / havokScale());

    if (vrLength(point - target) > MaxStretch)
      return false;

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
    bool turning = !held.corpse && handsOn(held.body) == 1;

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

    if (uint8_t* action = springAction(held.spring); action && !held.corpse) {
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
