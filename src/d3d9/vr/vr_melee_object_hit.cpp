#include <cstdio>
#include <cstring>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_math.h"
#include "vr_melee_object_hit.h"

namespace dxvk {

  namespace {

    using ObjectHitFn       = bool (__fastcall*)(uint8_t* actor, void* unused, uint32_t power);
    using EyeVectorFn       = void (__fastcall*)(uint8_t* actor, void* unused,
      VrVector3* position, VrVector3* direction, uint32_t thirdPerson);
    using WeaponPositionFn  = VrVector3* (__fastcall*)(uint8_t* actor, void* unused, VrVector3* out, uint32_t unknown);
    using WeaponLastPosFn   = const VrVector3* (__fastcall*)(uint8_t* process, void* unused);
    using Get3DFn           = uint8_t* (__fastcall*)(uint8_t* ref, void* unused);
    using GetPartFn         = uint8_t* (__fastcall*)(uint8_t* object, void* unused);
    using IsActorFn         = bool (__fastcall*)(uint8_t* ref, void* unused);
    using PickConstructFn   = void* (__fastcall*)(void* pick, void* unused);
    using PickSetFilterFn   = void (__fastcall*)(void* pick, void* unused, uint32_t filter);
    using PickSetPointFn    = void (__fastcall*)(void* pick, void* unused, const VrVector3* point);
    using PickObjectFn      = bool (__fastcall*)(uint8_t* world, void* unused, void* pick);
    using PickGetBodyFn     = uint8_t* (__fastcall*)(void* pick, void* unused);
    using GetFilterFn       = uint32_t* (__fastcall*)(uint8_t* actor, void* unused, uint32_t* filter);
    using ObjectForBodyFn   = uint8_t* (__cdecl*)(uint8_t* body);
    using FindReferenceFn   = uint8_t* (__cdecl*)(uint8_t* object);

    // The game's cast starts this far before the point the rays met, in
    // game units, so that it meets the surface rather than starting in it
    constexpr float CastBack = 8.0f;

    // Length of the weapon's motion given to the game, which only takes
    // its direction
    constexpr float MotionLength = 10.0f;

    constexpr uint32_t MaxLoggedHits = 100;

    VrMeleeObjectHit* g_objectHit = nullptr;

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      T value;
      std::memcpy(&value, base + offset, sizeof(value));
      return value;
    }

    template<typename Fn>
    Fn virtualFunction(const void* object, uintptr_t slot) {
      auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
      return reinterpret_cast<Fn>(vtable[slot / sizeof(uintptr_t)]);
    }

    std::string hexId(uint32_t id) {
      char text[16];
      std::snprintf(text, sizeof(text), "%08X", id);
      return text;
    }

    /**
     * \brief A vector of the world in the frame of a node: transposed
     *    rotation (NiMatrix3, row-major) and inverse scale
     */
    VrVector3 vectorToNode(const uint8_t* node, const VrVector3& v) {
      float m[9];
      std::memcpy(m, node + VrGame::ObjectWorldRotation, sizeof(m));
      float scale = readField<float>(node, VrGame::ObjectWorldScale);
      float inverse = scale > 0.0f ? 1.0f / scale : 1.0f;

      return VrVector3 {
        m[0] * v.x + m[3] * v.y + m[6] * v.z,
        m[1] * v.x + m[4] * v.y + m[7] * v.z,
        m[2] * v.x + m[5] * v.y + m[8] * v.z } * inverse;
    }

  }


  VrMeleeObjectHit::~VrMeleeObjectHit() {
    // The patched calls stay in place and fall through to the game's
    // functions
    g_objectHit = nullptr;
  }


  std::unique_ptr<VrMeleeObjectHit> VrMeleeObjectHit::install() {
    if (g_objectHit)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::ActorObjectHit,
        VrGame::ActorObjectHitPrologue, sizeof(VrGame::ActorObjectHitPrologue))
      && VrGameMemory::matches(VrGame::PickDataConstruct,
        VrGame::PickDataConstructPrologue, sizeof(VrGame::PickDataConstructPrologue))
      && VrGameMemory::callsTo(VrGame::ObjectHitEyeCallSite, VrGame::ActorGetEyeVector)
      && VrGameMemory::callsTo(VrGame::ObjectHitWeaponCallSite, VrGame::ActorGetWeaponPosition)
      && VrGameMemory::readable(VrGame::HavokScale, sizeof(float));

    if (!found)
      return nullptr;

    if (!VrGameMemory::redirectCall(VrGame::ObjectHitEyeCallSite, VrGame::ActorGetEyeVector,
          reinterpret_cast<const void*>(&VrMeleeObjectHit::eyeVectorHook))
     || !VrGameMemory::redirectCall(VrGame::ObjectHitWeaponCallSite, VrGame::ActorGetWeaponPosition,
          reinterpret_cast<const void*>(&VrMeleeObjectHit::weaponPositionHook)))
      return nullptr;

    std::unique_ptr<VrMeleeObjectHit> objectHit(new VrMeleeObjectHit());
    g_objectHit = objectHit.get();

    Logger::info("VR: Swung melee weapons and fists hit world objects too");
    return objectHit;
  }


  void VrMeleeObjectHit::strike(uint32_t striker, const VrSegment& from, const VrSegment& to, bool power, bool bare) {
    if (striker >= StrikerCount)
      return;

    Strike& strike = m_strikes[striker];
    strike.from    = from;
    strike.to      = to;
    strike.power   = power;
    strike.bare    = bare;
    strike.pending = true;
  }


  void VrMeleeObjectHit::run(uint8_t* player, float dt) {
    for (Strike& strike : m_strikes) {
      strike.lastAge += dt;

      if (strike.pending)
        cast(player, strike);

      strike.pending = false;
    }
  }


  bool VrMeleeObjectHit::cast(uint8_t* player, Strike& strike) {
    uint8_t* cell  = reinterpret_cast<GetPartFn>(VrGame::RefGetParentCell)(player, nullptr);
    uint8_t* world = cell ? reinterpret_cast<GetPartFn>(VrGame::CellGetWorld)(cell, nullptr) : nullptr;

    if (!world)
      return false;

    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);

    // The grab's rays: the item pick layer, which meets clutter, statics
    // and bodies, and the player's group, which skips the player and what
    // the hands hold
    uint32_t group     = playerFilter >> VrGame::FilterGroupShift;
    uint32_t rayFilter = (group << VrGame::FilterGroupShift) | VrGame::LayerItemPick;

    VrVector3 fromMiddle = (strike.from.a + strike.from.b) * 0.5f;
    VrVector3 toMiddle   = (strike.to.a + strike.to.b) * 0.5f;

    // The tip's and the middle's paths, then the striker where it is now
    const VrSegment rays[] = {
      { strike.from.b, strike.to.b },
      { fromMiddle, toMiddle },
      { strike.to.a, strike.to.b } };

    for (const VrSegment& ray : rays) {
      alignas(16) uint8_t pick[VrGame::PickDataSize];
      VrVector3 length = ray.b - ray.a;

      if (vrLength(length) <= 0.0f)
        continue;

      reinterpret_cast<PickConstructFn>(VrGame::PickDataConstruct)(pick, nullptr);
      reinterpret_cast<PickSetFilterFn>(VrGame::PickDataSetFilter)(pick, nullptr, rayFilter);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetFrom)(pick, nullptr, &ray.a);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetLength)(pick, nullptr, &length);

      if (!virtualFunction<PickObjectFn>(world, VrGame::WorldPickObjectSlot)(world, nullptr, pick))
        continue;

      uint8_t* body = reinterpret_cast<PickGetBodyFn>(VrGame::PickDataGetRigidBody)(pick, nullptr);

      if (!body)
        continue;

      // Actors are hit by the bones the striker touches, not here
      uint8_t* object = reinterpret_cast<ObjectForBodyFn>(VrGame::CollisionObjectForBody)(body);
      uint8_t* scene  = object ? readField<uint8_t*>(object, VrGame::CollisionSceneObject) : nullptr;
      uint8_t* ref    = scene ? reinterpret_cast<FindReferenceFn>(VrGame::FindReferenceFor3D)(scene) : nullptr;

      if (ref && virtualFunction<IsActorFn>(ref, VrGame::RefIsActorSlot)(ref, nullptr))
        return false;

      // Once per swing, as an actor
      if (body == strike.lastBody && strike.lastAge < VrSwingTracker::RehitTime)
        return false;

      VrVector3 point  = ray.a + length * readField<float>(pick, VrGame::PickDataHitFraction);
      VrVector3 motion = strike.to.b - strike.from.b;

      if (vrLength(motion) <= 0.0f)
        motion = strike.to.b - strike.to.a;

      m_direction = motion * (1.0f / vrLength(motion));
      m_eye       = point - m_direction * CastBack;

      // A bare fist pushes and sounds as the game's fists
      m_running = true;
      bool hit = false;

      {
        VrBareHand::Strike bareStrike(strike.bare ? m_bareHand : nullptr, player);
        hit = reinterpret_cast<ObjectHitFn>(VrGame::ActorObjectHit)(player, nullptr, strike.power ? 1 : 0);
      }

      m_running = false;

      strike.lastBody = body;
      strike.lastAge  = 0.0f;

      if (m_loggedHits < MaxLoggedHits) {
        Logger::info(str::format("VR: The swing met ", ref ? hexId(readField<uint32_t>(ref, VrGame::FormId)) : std::string("the world"),
          ", the game's object hit ", hit ? "struck it" : "missed it", strike.power ? " with a power attack" : ""));
        m_loggedHits++;
      }

      return true;
    }

    return false;
  }


  void __fastcall VrMeleeObjectHit::eyeVectorHook(uint8_t* actor, void* unused,
      VrVector3* position, VrVector3* direction, uint32_t thirdPerson) {
    if (g_objectHit && g_objectHit->m_running) {
      *position  = g_objectHit->m_eye;
      *direction = g_objectHit->m_direction;
      return;
    }

    reinterpret_cast<EyeVectorFn>(VrGame::ActorGetEyeVector)(actor, nullptr, position, direction, thirdPerson);
  }


  VrVector3* __fastcall VrMeleeObjectHit::weaponPositionHook(uint8_t* actor, void* unused,
      VrVector3* out, uint32_t unknown) {
    // ObjectHit pushes along the weapon's motion, from its last position
    // to this one, both in the frame of the actor's root: the striker's
    if (g_objectHit && g_objectHit->m_running) {
      uint8_t* process = readField<uint8_t*>(actor, VrGame::ActorProcess);
      uint8_t* root    = virtualFunction<Get3DFn>(actor, VrGame::RefGet3DSlot)(actor, nullptr);
      const VrVector3* last = process && root
        ? virtualFunction<WeaponLastPosFn>(process, VrGame::ProcessWeaponLastPosSlot)(process, nullptr)
        : nullptr;

      if (last) {
        *out = *last + vectorToNode(root, g_objectHit->m_direction * MotionLength);
        return out;
      }
    }

    return reinterpret_cast<WeaponPositionFn>(VrGame::ActorGetWeaponPosition)(actor, nullptr, out, unknown);
  }

}
