#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_hand_body.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    using NiNewFn             = void* (__cdecl*)(size_t size);
    using SphereConstructFn   = void* (__fastcall*)(void* shape, void* unused, float radius, bool gameUnits);
    using BodyInfoFn          = void* (__fastcall*)(void* info, void* unused);
    using BodyInfoSetShapeFn  = void (__fastcall*)(void* info, void* unused, void* shape);
    using RigidBodyConstructFn = void* (__fastcall*)(void* body, void* unused, const void* info);
    using AssignFn            = void* (__fastcall*)(void** pointer, void* unused, void* object);
    using AddFn               = bool (__fastcall*)(void* object, void* unused, uint8_t* world);
    using RemoveFn            = void (__fastcall*)(void* object, void* unused);
    using SetPositionFn       = void (__fastcall*)(void* object, void* unused, const float* position);
    using SetVelocityFn       = void (__fastcall*)(uint8_t* body, void* unused, const float* velocity);
    using GetFilterFn         = uint32_t* (__fastcall*)(uint8_t* actor, void* unused, uint32_t* filter);
    using GetPartFn           = uint8_t* (__fastcall*)(uint8_t* object, void* unused);
    using IsBatchingAddsFn    = int32_t (__fastcall*)(uint8_t* world, void* unused);
    using UpdateFilterFn      = void (__fastcall*)(uint8_t* world, void* unused, uint8_t* entity,
                                                   int32_t mode, int32_t collections);

    template<typename T>
    T readField(const void* base, uintptr_t offset) {
      T value;
      std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(value));
      return value;
    }

    template<typename T>
    void writeField(void* base, uintptr_t offset, const T& value) {
      std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
    }

    template<typename Fn>
    Fn virtualFunction(const void* object, uintptr_t slot) {
      auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
      return reinterpret_cast<Fn>(vtable[slot / sizeof(uintptr_t)]);
    }

    float havokScale() {
      return *reinterpret_cast<const float*>(VrGame::HavokScale);
    }

    /**
     * \brief The hkpRigidBody of a bhkRigidBody
     */
    uint8_t* havokBody(const void* object) {
      return object ? readField<uint8_t*>(object, VrGame::RefObjectHavok) : nullptr;
    }

    uint8_t* havokWorld(const uint8_t* object) {
      return readField<uint8_t*>(object, VrGame::HavokObjectWorld);
    }

    /**
     * \brief The hkpWorld of the player's cell and its bhkWorld
     */
    uint8_t* playerWorld(uint8_t* player, uint8_t** bhkWorld) {
      uint8_t* cell = reinterpret_cast<GetPartFn>(VrGame::RefGetParentCell)(player, nullptr);
      uint8_t* world = cell ? reinterpret_cast<GetPartFn>(VrGame::CellGetWorld)(cell, nullptr) : nullptr;
      *bhkWorld = world;
      return world ? readField<uint8_t*>(world, VrGame::RefObjectHavok) : nullptr;
    }

    void toVector4(const VrVector3& v, float (&out)[4]) {
      out[0] = v.x;
      out[1] = v.y;
      out[2] = v.z;
      out[3] = 0.0f;
    }

    void setVelocity(uint8_t* body, uintptr_t setter, const VrVector3& velocity) {
      alignas(16) float value[4];
      toVector4(velocity, value);
      reinterpret_cast<SetVelocityFn>(setter)(body, nullptr, value);
    }

    void setPosition(void* object, const VrVector3& position) {
      alignas(16) float value[4];
      toVector4(position, value);
      virtualFunction<SetPositionFn>(object, VrGame::RigidBodySetPositionSlot)(object, nullptr, value);
    }

    void assign(void*& pointer, void* object) {
      reinterpret_cast<AssignFn>(VrGame::SpringAssign)(&pointer, nullptr, object);
    }

    const char* handName(VrHand hand) {
      return hand == VrHand::Left ? "left" : "right";
    }

  }


  std::unique_ptr<VrHandBodies> VrHandBodies::create() {
    bool found = VrGameMemory::matches(VrGame::SphereShapeConstruct,
        VrGame::SphereShapeConstructPrologue, sizeof(VrGame::SphereShapeConstructPrologue))
      && VrGameMemory::matches(VrGame::BodyInfoConstruct,
        VrGame::BodyInfoConstructPrologue, sizeof(VrGame::BodyInfoConstructPrologue))
      && VrGameMemory::matches(VrGame::BodyInfoDestruct,
        VrGame::BodyInfoDestructPrologue, sizeof(VrGame::BodyInfoDestructPrologue))
      && VrGameMemory::matches(VrGame::BodyInfoSetShape,
        VrGame::BodyInfoSetShapePrologue, sizeof(VrGame::BodyInfoSetShapePrologue))
      && VrGameMemory::matches(VrGame::RigidBodyConstruct,
        VrGame::RigidBodyConstructPrologue, sizeof(VrGame::RigidBodyConstructPrologue))
      && VrGameMemory::matches(VrGame::NiNew, VrGame::NiNewPrologue, sizeof(VrGame::NiNewPrologue))
      && VrGameMemory::matches(VrGame::WorldUpdateFilter,
        VrGame::WorldUpdateFilterPrologue, sizeof(VrGame::WorldUpdateFilterPrologue))
      && VrGameMemory::matches(VrGame::BodySetLinearVelocity,
        VrGame::BodySetVelocityPrologue, sizeof(VrGame::BodySetVelocityPrologue))
      && VrGameMemory::matches(VrGame::BodySetAngularVelocity,
        VrGame::BodySetVelocityPrologue, sizeof(VrGame::BodySetVelocityPrologue))
      && VrGameMemory::readable(VrGame::RigidBodyVtable, VrGame::RigidBodySetPositionSlot + sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::HavokScale, sizeof(float))
      && VrGameMemory::matches(VrGame::WorldIsBatchingAdds,
        VrGame::WorldIsBatchingAddsBytes, sizeof(VrGame::WorldIsBatchingAddsBytes));

    if (!found)
      return nullptr;

    return std::unique_ptr<VrHandBodies>(new VrHandBodies());
  }


  void VrHandBodies::update(
          uint8_t*              player,
          VrHand                hand,
    const VrVector3*            position,
          bool                  free,
          float                 dt) {
    Body& body = m_bodies[uint32_t(hand)];
    float scale = havokScale();

    uint8_t* bhkWorld = nullptr;
    uint8_t* world = playerWorld(player, &bhkWorld);

    if (!position || !world || !(scale > 0.0f)) {
      halt(body);
      return;
    }

    // The player's group keeps the sphere off the player and what the
    // hands hold
    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);
    uint32_t group = playerFilter >> VrGame::FilterGroupShift;
    uint32_t filter = VrGame::LayerDebrisSmall | (group << VrGame::FilterGroupShift);

    VrVector3 target = *position * scale;
    uint8_t* hkBody = havokBody(body.object);

    // Moved to another cell's world: a new sphere there
    if (hkBody && havokWorld(hkBody) != world) {
      if (havokWorld(hkBody))
        virtualFunction<RemoveFn>(body.object, VrGame::RigidBodyRemoveSlot)(body.object, nullptr);

      drop(body);
      hkBody = nullptr;
    }

    if (!hkBody) {
      // While a cell loads, the world would only queue the sphere: it is
      // made once the world takes bodies again
      if (reinterpret_cast<IsBatchingAddsFn>(VrGame::WorldIsBatchingAdds)(bhkWorld, nullptr))
        return;

      if (!make(body, target, filter | VrGame::FilterNoCollision))
        return;

      virtualFunction<AddFn>(body.object, VrGame::RigidBodyAddSlot)(body.object, nullptr, bhkWorld);
      hkBody = havokBody(body.object);

      if (!hkBody || havokWorld(hkBody) != world) {
        if (!m_loggedFailed) {
          Logger::warn(str::format("VR: The ", handName(hand), " hand's sphere was not added to the world, the hands do not push objects"));
          m_loggedFailed = true;
        }

        drop(body);
        return;
      }

      if (!m_loggedCreated) {
        Logger::info(str::format("VR: Each free hand pushes objects with a keyframed sphere of ",
          Radius * 100.0f, " cm"));
        m_loggedCreated = true;
      }

      body.gate.reset();
    }

    VrHandBodyStep step = vrHandBodyStep(readField<VrVector3>(hkBody, VrGame::BodyPosition),
      target, dt, MaxSpeed * VrGameUnitsPerMetre * scale);

    if (step.warp) {
      halt(body);
      setPosition(body.object, target);
      body.gate.reset();
    } else {
      setVelocity(hkBody, VrGame::BodySetLinearVelocity, step.velocity);
      setVelocity(hkBody, VrGame::BodySetAngularVelocity, VrVector3());
    }

    // Without a group of its own, the sphere would push the player
    bool solid = body.gate.update(free && !step.warp, dt) && group != 0;
    setFilter(body, solid ? filter : filter | VrGame::FilterNoCollision);
  }


  void VrHandBodies::stop() {
    for (Body& body : m_bodies)
      halt(body);
  }


  bool VrHandBodies::leaveWorld(const uint8_t* world) {
    bool left = false;

    for (Body& body : m_bodies) {
      uint8_t* hkBody = havokBody(body.object);

      if (!hkBody)
        continue;

      uint8_t* current = havokWorld(hkBody);

      if (world && current == world)
        continue;

      if (current)
        virtualFunction<RemoveFn>(body.object, VrGame::RigidBodyRemoveSlot)(body.object, nullptr);

      drop(body);
      left = true;
    }

    return left;
  }


  bool VrHandBodies::make(Body& body, const VrVector3& position, uint32_t filter) {
    void* shapeMemory = reinterpret_cast<NiNewFn>(VrGame::NiNew)(VrGame::SphereShapeSize);

    if (!shapeMemory)
      return false;

    void* shape = reinterpret_cast<SphereConstructFn>(VrGame::SphereShapeConstruct)(
      shapeMemory, nullptr, Radius * VrGameUnitsPerMetre, true);
    writeField<uint32_t>(shape, VrGame::ShapeMaterial, VrGame::MaterialSkin);
    assign(body.shape, shape);

    alignas(16) uint8_t info[VrGame::BodyInfoSize];
    reinterpret_cast<BodyInfoFn>(VrGame::BodyInfoConstruct)(info, nullptr);

    writeField(info, VrGame::BodyInfoFilter, filter);
    writeField(info, VrGame::BodyInfoHavokFilter, filter);
    reinterpret_cast<BodyInfoSetShapeFn>(VrGame::BodyInfoSetShape)(info, nullptr, shape);

    alignas(16) float origin[4];
    toVector4(position, origin);
    std::memcpy(info + VrGame::BodyInfoPosition, origin, sizeof(origin));
    writeField(info, VrGame::BodyInfoMotionType, VrGame::MotionKeyframed);

    void* memory = reinterpret_cast<NiNewFn>(VrGame::NiNew)(VrGame::RigidBodySize);
    void* object = memory
      ? reinterpret_cast<RigidBodyConstructFn>(VrGame::RigidBodyConstruct)(memory, nullptr, info)
      : nullptr;

    reinterpret_cast<BodyInfoFn>(VrGame::BodyInfoDestruct)(info, nullptr);

    if (object)
      assign(body.object, object);

    if (!object || readField<uintptr_t>(object, 0) != VrGame::RigidBodyVtable || !havokBody(object)) {
      if (!m_loggedFailed) {
        Logger::warn("VR: The hands' spheres were not made, the hands do not push objects");
        m_loggedFailed = true;
      }

      drop(body);
      return false;
    }

    body.filter = filter;
    return true;
  }


  void VrHandBodies::drop(Body& body) {
    assign(body.object, nullptr);
    assign(body.shape, nullptr);
    body.filter = 0;
    body.gate.reset();
  }


  void VrHandBodies::setFilter(Body& body, uint32_t filter) {
    uint8_t* hkBody = havokBody(body.object);

    if (!hkBody || filter == body.filter)
      return;

    writeField(hkBody, VrGame::BodyFilter, filter);

    if (uint8_t* world = havokWorld(hkBody))
      reinterpret_cast<UpdateFilterFn>(VrGame::WorldUpdateFilter)(world, nullptr, hkBody, 0, 1);

    body.filter = filter;
  }


  void VrHandBodies::halt(Body& body) {
    body.gate.reset();

    uint8_t* hkBody = havokBody(body.object);

    if (!hkBody || !havokWorld(hkBody))
      return;

    setFilter(body, body.filter | VrGame::FilterNoCollision);

    // The setters wake the body: only when it still moves
    if (vrLength(readField<VrVector3>(hkBody, VrGame::BodyLinearVelocity)) > 0.0f)
      setVelocity(hkBody, VrGame::BodySetLinearVelocity, VrVector3());

    if (vrLength(readField<VrVector3>(hkBody, VrGame::BodyAngularVelocity)) > 0.0f)
      setVelocity(hkBody, VrGame::BodySetAngularVelocity, VrVector3());
  }

}
