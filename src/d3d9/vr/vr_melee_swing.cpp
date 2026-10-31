#include <algorithm>
#include <cstring>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_math.h"
#include "vr_melee_swing.h"

namespace dxvk {

  namespace {

    // Actor::MeleeAttack, see VrGame::MeleeAttack
    using MeleeAttackFn = void (__fastcall*)(uintptr_t actor, void* unused, uint32_t power, uint32_t inCombat);

    // Actor::CombatHit, see VrGame::CombatHit
    using CombatHitFn = void (__fastcall*)(void* attacker, void* unused,
      void* target, uint32_t power, void* projectile, uint32_t meleeEffect);

    // TESObjectREFR::Get3D and IsActor, __thiscall without arguments
    using Get3DFn   = uintptr_t (__fastcall*)(uintptr_t reference, void* unused);
    using IsActorFn = uint32_t (__fastcall*)(uintptr_t reference, void* unused);

    // Actor::IsDead(bool), __thiscall, returns a bool
    using IsDeadFn  = uint32_t (__fastcall*)(uintptr_t actor, void* unused, uint32_t unknown);

    // ActorValueOwner::GetBaseActorFloatValue and GetActorFloatValue
    using GetAvFloatFn = float (__fastcall*)(uintptr_t owner, void* unused, uint32_t index);

    // Hit limb, see findings/melee.md §10 and VrGame::SetTargetedLimb
    using SetTargetedLimbFn   = void (__fastcall*)(uintptr_t hitData, void* unused, void* projectile);
    using GetActorBaseFn      = uintptr_t (__fastcall*)(uintptr_t actor, void* unused);
    using GetBodyPartDataFn   = uintptr_t (__fastcall*)(uintptr_t actorBase, void* unused);
    using GetDamageLocationFn = int32_t (__fastcall*)(uintptr_t actor, void* unused, uintptr_t node);
    using GetDamageNodeFn     = uintptr_t (__fastcall*)(uintptr_t process, void* unused, int32_t limb);
    using GetBodyPartFn       = uintptr_t (__fastcall*)(uintptr_t bodyPartData, void* unused, int32_t limb);
    using GetPartTypeFn       = int32_t (__fastcall*)(uintptr_t bodyPart, void* unused);
    using CalcLimbDamageFn    = float (__cdecl*)(uintptr_t actorValues, uintptr_t bodyPart, float damage);
    using GetLimbDamageMultFn = float (__fastcall*)(uintptr_t weapon, void* unused);
    using SetBonusMultFn      = void (__fastcall*)(uintptr_t hitData, void* unused, float mult);

    // Positions the blade passes through between two frames
    constexpr uint32_t SweepSteps = 4;

    // Radius of the capsule around each bone, about a limb's
    constexpr float BoneRadius = 0.10f * VrGameUnitsPerMetre;

    // Bones further apart are not a limb, and an actor further than this
    // from the blade cannot be reached, in game units
    constexpr float MaxBoneLength = 1.5f * VrGameUnitsPerMetre;
    constexpr float MaxActorDistance = 4.0f * VrGameUnitsPerMetre;

    // The skeleton's deepest bones (fingers) are about twenty levels down
    constexpr uint32_t MaxSkeletonDepth = 32;

    // More entries than this means the list was not read correctly
    constexpr uint32_t MaxProcessActors = 4096;

    // Bones are named like this in the skeletons of characters and of most
    // creatures. Other nodes of an actor's model (armour, attached weapons)
    // are placed at the actor's origin and would join it to every bone.
    constexpr char BonePrefix[] = "Bip01";

    // A damage node further than this from the contact is not the limb hit
    constexpr float MaxDamageNodeDistance = 1.5f * VrGameUnitsPerMetre;

    constexpr uint32_t MaxLoggedHits = 200;

    // Hits the game decided for a running automatic weapon that are dealt
    // in one frame at most, the rest are dropped
    constexpr uint32_t MaxMotorHitsPerFrame = 4;

    // BGSBodyPart::LIMB_ENUM, torso to brain
    constexpr const char* LimbNames[] = {
      "torso", "head 1", "head 2", "left arm 1", "left arm 2", "right arm 1", "right arm 2",
      "left leg 1", "left leg 2", "left leg 3", "right leg 1", "right leg 2", "right leg 3", "brain" };

    static_assert(sizeof(LimbNames) / sizeof(LimbNames[0]) == size_t(VrGame::LimbWeapon),
      "One name per body part before the weapon");

    VrMeleeSwing* g_meleeSwing = nullptr;

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    template<typename T>
    void writeField(uintptr_t base, uintptr_t offset, const T& value) {
      *reinterpret_cast<T*>(base + offset) = value;
    }

    VrVector3 readVector(uintptr_t base, uintptr_t offset) {
      VrVector3 v;
      std::memcpy(&v, reinterpret_cast<const void*>(base + offset), sizeof(v));
      return v;
    }

    void writeVector(uintptr_t base, uintptr_t offset, const VrVector3& v) {
      std::memcpy(reinterpret_cast<void*>(base + offset), &v, sizeof(v));
    }

    template<typename Fn>
    Fn virtualFunction(uintptr_t object, uintptr_t slot) {
      return reinterpret_cast<Fn>(readField<uintptr_t>(readField<uintptr_t>(object, 0), slot));
    }

    const char* nodeName(uintptr_t object) {
      auto name = readField<const char*>(object, VrGame::ObjectName);
      return name ? name : "(unnamed)";
    }

    /**
     * \brief Checks the functions the hit limb uses by their calls in SetTargetedLimb
     */
    bool findLimbFunctions() {
      struct Call {
        uintptr_t site;
        uintptr_t target;
      };

      const Call calls[] = {
        { VrGame::ActorGetBaseCallSite,             VrGame::ActorGetBase },
        { VrGame::ActorGetDamageLocationCallSite,   VrGame::ActorGetDamageLocation },
        { VrGame::BodyPartDataGetPartCallSite,      VrGame::BodyPartDataGetPart },
        { VrGame::BodyPartGetTypeCallSite,          VrGame::BodyPartGetType },
        { VrGame::CalcTargetedLimbDamageCallSite,   VrGame::CalcTargetedLimbDamage },
        { VrGame::WeaponGetLimbDamageMultCallSite,  VrGame::WeaponGetLimbDamageMult },
        { VrGame::HitDataSetBonusMultCallSite,      VrGame::HitDataSetBonusMult },
      };

      for (const Call& call : calls) {
        if (!VrGameMemory::callsTo(call.site, call.target))
          return false;
      }

      return true;
    }

    bool isBone(uintptr_t object) {
      auto name = readField<const char*>(object, VrGame::ObjectName);
      return name && !std::strncmp(name, BonePrefix, sizeof(BonePrefix) - 1);
    }

    /**
     * \brief Calls a function for each bone and its parent bone
     */
    template<typename Fn>
    bool forEachBone(uintptr_t node, uint32_t depth, const Fn& fn) {
      auto children = readField<const uintptr_t*>(node, VrGame::NodeChildren);
      auto count    = readField<uint16_t>(node, VrGame::NodeChildCount);
      bool parentIsBone = isBone(node);

      for (uint32_t i = 0; children && i < count; i++) {
        uintptr_t child = children[i] ? vrGameAsNode(children[i]) : 0;

        if (!child)
          continue;

        if (parentIsBone && isBone(child) && fn(node, child))
          return true;

        if (depth && forEachBone(child, depth - 1, fn))
          return true;
      }

      return false;
    }

  }


  VrMeleeSwing::~VrMeleeSwing() {
    g_meleeSwing = nullptr;
  }


  std::unique_ptr<VrMeleeSwing> VrMeleeSwing::install(const VrSwingConfig& config) {
    if (g_meleeSwing)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::CombatHit, VrGame::CombatHitPrologue, sizeof(VrGame::CombatHitPrologue))
      && VrGameMemory::readable(VrGame::ProcessActors, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::ProcessHighEnd, sizeof(uint32_t));

    if (!found)
      return nullptr;

    // Without the limb hook, the game picks the limb along the view
    bool limbFound = findLimbFunctions()
      && VrGameMemory::redirectCall(VrGame::SetTargetedLimbCallSite, VrGame::SetTargetedLimb,
           reinterpret_cast<const void*>(&VrMeleeSwing::setTargetedLimbHook));

    Logger::info(str::format("VR: Melee weapons hit when swung faster than ", config.hitSpeed,
      " m/s, power attacks when charged"));

    if (!limbFound)
      Logger::warn("VR: The limb of melee hits could not be patched, the game picks it along the view");

    // Without it, automatic melee weapons are swung like the others and
    // the attack control is held back
    bool motorFound = VrGameMemory::redirectCall(VrGame::MeleeAttackCallSite, VrGame::MeleeAttack,
      reinterpret_cast<const void*>(&VrMeleeSwing::meleeAttackHook));

    if (motorFound)
      Logger::info("VR: Automatic melee weapons run while the trigger is held and hit what the blade touches");
    else
      Logger::warn("VR: The game's melee hits could not be patched, automatic melee weapons are only swung");

    std::unique_ptr<VrMeleeSwing> swing(new VrMeleeSwing(config, motorFound));
    g_meleeSwing = swing.get();
    return swing;
  }


  VrMeleeHit VrMeleeSwing::update(
    const VrGameCameraPose*     hand,
    const VrBlade*              blade,
          bool                  automatic,
          bool                  power,
    const VrGameCameraPose&     head,
    const VrGameCameraPose&     body,
          float                 dt) {
    VrMeleeHit result;

    auto player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

    if (!hand || !blade || !player) {
      m_hasBlade  = false;
      m_automatic = false;
      m_motorHits = 0;
      m_swing.reset();
      m_guard.reset();
      return result;
    }

    // Where the blade was on the previous frame, moved and turned with the
    // body since
    VrSegment previous = {
      vrPointFromHand(body, m_blade.a),
      vrPointFromHand(body, m_blade.b) };
    bool hadBlade = m_hasBlade;

    VrSegment current = vrBladeInWorld(*hand, *blade);

    m_blade    = { vrPointToHand(body, current.a), vrPointToHand(body, current.b) };
    m_hasBlade = true;
    m_automatic = automatic && m_motorFound;

    bool wasGuarding = m_guard.guarding();

    m_swing.update(m_blade.b, dt);
    m_guard.update(vrIsGuardPose(head, current), m_swing.swinging(), dt);

    if (m_guard.guarding() && !wasGuarding && m_loggedGuards < MaxLoggedHits) {
      Logger::info("VR: The melee weapon is held as a guard, the game's block control is held");
      m_loggedGuards++;
    }

    // Hits the game decided since the last frame, dropped if the weapon in
    // the hand is no longer automatic
    uint32_t motorHits = std::min(m_motorHits.exchange(0), MaxMotorHitsPerFrame);

    for (uint32_t i = 0; i < motorHits && m_automatic; i++)
      result.count += hitWithMotor(player, current);

    if (!hadBlade || !m_swing.swinging())
      return result;

    uint32_t swingHits = hitActors(player, m_swing, previous, current, false, power);
    result.count += swingHits;
    result.power  = swingHits && power;

    // A swing that touched no actor may still hit an object
    if (!swingHits && m_objectHit)
      m_objectHit->strike(0, previous, current, power);

    if (swingHits && m_loggedHits < MaxLoggedHits) {
      Logger::info(str::format("VR: Melee swing at ", m_swing.speed(), " m/s hit ", swingHits,
        swingHits > 1 ? " actors" : " actor", power ? " with a power attack" : ""));
      m_loggedHits++;
    }

    return result;
  }


  std::array<uint32_t, VrHandCount> VrMeleeSwing::updateFists(
    const std::array<VrFistInput, VrHandCount>& fists,
          bool                  out,
    const VrGameCameraPose&     body,
          float                 dt) {
    std::array<uint32_t, VrHandCount> counts = { };

    auto player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    m_fistsOut = out && player;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrFistInput& fist = fists[i];

      if (!m_fistsOut || !fist.hand || !fist.closed) {
        m_hasFist[i] = false;
        m_fistSwings[i].reset();
        continue;
      }

      // As the blade: the fist of the previous frame, moved with the body
      VrSegment previous = {
        vrPointFromHand(body, m_fists[i].a),
        vrPointFromHand(body, m_fists[i].b) };
      bool hadFist = m_hasFist[i];

      VrSegment current = vrBladeInWorld(*fist.hand, vrFistBlade());

      m_fists[i]   = { vrPointToHand(body, current.a), vrPointToHand(body, current.b) };
      m_hasFist[i] = true;
      m_fistSwings[i].update(m_fists[i].b, dt);

      if (!hadFist || !m_fistSwings[i].swinging())
        continue;

      // The game's unarmed attack, never a power attack: the trigger is
      // the fist's index
      counts[i] = hitActors(player, m_fistSwings[i], previous, current, false, false);

      if (!counts[i] && m_objectHit)
        m_objectHit->strike(1 + i, previous, current, false);

      if (counts[i] && m_loggedPunches < MaxLoggedHits) {
        Logger::info(str::format("VR: The ", i == uint32_t(VrHand::Left) ? "left" : "right", " fist punched at ",
          m_fistSwings[i].speed(), " m/s and hit ", counts[i], counts[i] > 1 ? " actors" : " actor"));
        m_loggedPunches++;
      }
    }

    return counts;
  }


  uint32_t VrMeleeSwing::hitWithMotor(uint8_t* player, const VrSegment& blade) {
    // The blade where it is now, however slowly it moves
    uint32_t count = hitActors(player, m_swing, blade, blade, true, false);

    if (count && m_loggedMotorHits < MaxLoggedHits) {
      Logger::info(str::format("VR: The running automatic melee weapon hit ", count,
        count > 1 ? " actors" : " actor"));
      m_loggedMotorHits++;
    }

    return count;
  }


  uint32_t VrMeleeSwing::hitActors(uint8_t* player, VrSwingTracker& swing, const VrSegment& from, const VrSegment& to,
      bool motor, bool power) {
    auto actors = *reinterpret_cast<const uintptr_t* const*>(VrGame::ProcessActors);
    auto end    = *reinterpret_cast<const uint32_t*>(VrGame::ProcessHighEnd);

    if (!actors || end > MaxProcessActors)
      return 0;

    auto combatHit = reinterpret_cast<CombatHitFn>(VrGame::CombatHit);
    uint32_t count = 0;

    for (uint32_t i = 0; i < end; i++) {
      uintptr_t actor = actors[i];

      // The running weapon hits at the game's pace, a swing once per target
      if (!actor || actor == reinterpret_cast<uintptr_t>(player)
       || (!motor && swing.hasHit(reinterpret_cast<void*>(actor))))
        continue;

      auto vtable = readField<uintptr_t>(actor, 0);

      if (!(reinterpret_cast<IsActorFn>(readField<uintptr_t>(vtable, VrGame::RefIsActorSlot))(actor, nullptr) & 0xFF))
        continue;

      Contact contact;

      if (!findContact(actor, from, to, contact))
        continue;

      swing.addHit(reinterpret_cast<void*>(actor));

      // The game asks for the limb while it computes the hit, see setTargetedLimb
      m_contact = contact;
      m_dealt   = Dealt();
      m_dealing = true;
      // A power attack only when charged, see findings/melee.md §12
      combatHit(player, nullptr, reinterpret_cast<void*>(actor), power ? 1 : 0, nullptr, 0);
      m_dealing = false;

      logDealt(actor);

      count++;
    }

    return count;
  }


  bool VrMeleeSwing::findContact(uintptr_t actor, const VrSegment& from, const VrSegment& to, Contact& contact) const {
    VrVector3 location = readVector(actor, VrGame::RefLocation);
    VrVector3 middle   = (to.a + to.b) * 0.5f;

    if (vrLength(middle - location) > MaxActorDistance)
      return false;

    auto vtable = readField<uintptr_t>(actor, 0);
    uintptr_t root = reinterpret_cast<Get3DFn>(readField<uintptr_t>(vtable, VrGame::RefGet3DSlot))(actor, nullptr);
    uintptr_t node = root ? vrGameAsNode(root) : 0;

    if (!node)
      return false;

    bool found = false;

    // The bone the blade reaches first in the sweep, the nearest one if
    // several are reached at once
    forEachBone(node, MaxSkeletonDepth, [&] (uintptr_t parent, uintptr_t child) {
      VrSegment bone = {
        readVector(parent, VrGame::ObjectWorldTranslation),
        readVector(child,  VrGame::ObjectWorldTranslation) };

      if (vrLength(bone.b - bone.a) > MaxBoneLength)
        return false;

      VrSweepContact sweep = vrSweptSegmentContact(from, to, bone, SweepSteps, BoneRadius);

      if (!sweep.touches)
        return false;

      if (found && (sweep.time > contact.time
                || (sweep.time == contact.time && sweep.distance >= contact.distance)))
        return false;

      // The capsule from a bone to its child is the bone's limb
      contact.actor    = actor;
      contact.bone     = parent;
      contact.time     = sweep.time;
      contact.distance = sweep.distance;
      contact.hit      = vrComputeBladeHit(sweep, BoneRadius);
      found = true;
      return false;
    });

    return found;
  }


  void __fastcall VrMeleeSwing::meleeAttackHook(uintptr_t actor, void* unused, uint32_t power, uint32_t inCombat) {
    auto player = *reinterpret_cast<const uintptr_t*>(VrGame::Player);

    // The player's hit with an automatic weapon in the hand lands where the
    // blade is on the next frame, not on the actor in front of the head
    if (g_meleeSwing && actor == player && g_meleeSwing->m_automatic) {
      g_meleeSwing->m_motorHits++;
      return;
    }

    reinterpret_cast<MeleeAttackFn>(VrGame::MeleeAttack)(actor, nullptr, power, inCombat);
  }


  void __fastcall VrMeleeSwing::setTargetedLimbHook(uintptr_t hitData, void* unused, void* projectile) {
    if (g_meleeSwing && g_meleeSwing->setTargetedLimb(hitData, projectile))
      return;

    reinterpret_cast<SetTargetedLimbFn>(VrGame::SetTargetedLimb)(hitData, nullptr, projectile);
  }


  bool VrMeleeSwing::setTargetedLimb(uintptr_t hitData, void* projectile) {
    auto player = *reinterpret_cast<const uintptr_t*>(VrGame::Player);

    // Only the hit dealt by hitActors, not the game's own hits
    if (!m_dealing || projectile
     || readField<uintptr_t>(hitData, VrGame::HitAggressor) != player
     || readField<uintptr_t>(hitData, VrGame::HitTarget) != m_contact.actor
     || readField<uintptr_t>(hitData, VrGame::HitVatsCommand))
      return false;

    // Damage after the power bonus, critical and armour, which the game
    // computes before the limb, see findings/melee.md §10.1
    m_dealt.seen         = true;
    m_dealt.healthDamage = readField<float>(hitData, VrGame::HitHealthDamage);
    m_dealt.flags        = readField<uint32_t>(hitData, VrGame::HitFlags);

    uintptr_t actor = m_contact.actor;
    uintptr_t actorBase = reinterpret_cast<GetActorBaseFn>(VrGame::ActorGetBase)(actor, nullptr);

    if (!actorBase)
      return false;

    uintptr_t bodyParts = virtualFunction<GetBodyPartDataFn>(actorBase,
      VrGame::ActorBaseBodyPartDataSlot)(actorBase, nullptr);

    if (!bodyParts)
      return false;

    int32_t limb = limbOfContact(bodyParts);

    if (limb < 0)
      return false;

    auto getBodyPart = reinterpret_cast<GetBodyPartFn>(VrGame::BodyPartDataGetPart);
    uintptr_t bodyPart = getBodyPart(bodyParts, nullptr, limb);

    if (!bodyPart)
      return false;

    // What the game's routine does once it has picked a body part, see
    // findings/melee.md §10.1. The target is never the player, so the
    // player's limb damage multiplier does not apply.
    int32_t partType = reinterpret_cast<GetPartTypeFn>(VrGame::BodyPartGetType)(bodyPart, nullptr);

    writeField<int32_t>(hitData, VrGame::HitDamageLimb, partType);
    writeVector(hitData, VrGame::HitLocation, m_contact.hit.location);
    writeVector(hitData, VrGame::HitDirection, m_contact.hit.direction);

    float limbDamage = reinterpret_cast<CalcLimbDamageFn>(VrGame::CalcTargetedLimbDamage)(
      actor + VrGame::ActorValueOwnerBase, bodyPart, readField<float>(hitData, VrGame::HitHealthDamage));

    uintptr_t weapon = readField<uintptr_t>(hitData, VrGame::HitWeapon);

    if (weapon)
      limbDamage *= reinterpret_cast<GetLimbDamageMultFn>(VrGame::WeaponGetLimbDamageMult)(weapon, nullptr);

    writeField<float>(hitData, VrGame::HitLimbDamage, limbDamage);

    // The game ignores the body part's damage multiplier for melee weapons
    reinterpret_cast<SetBonusMultFn>(VrGame::HitDataSetBonusMult)(hitData, nullptr, 1.0f);

    m_dealt.limbSet    = true;
    m_dealt.limb       = partType >= 0 && partType < VrGame::LimbWeapon ? LimbNames[partType] : "another part";
    m_dealt.limbDamage = limbDamage;
    return true;
  }


  void VrMeleeSwing::logDealt(uintptr_t actor) {
    if (m_loggedDamage >= MaxLoggedHits)
      return;

    auto vtable = readField<uintptr_t>(actor, 0);
    bool dead = reinterpret_cast<IsDeadFn>(readField<uintptr_t>(vtable, VrGame::ActorIsDeadSlot))(actor, nullptr, 0) & 0xFF;

    uintptr_t values = actor + VrGame::ActorValueOwnerBase;
    float health     = virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerFloatSlot)(values, nullptr, VrGame::AvHealth);
    float baseHealth = virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerBaseFloatSlot)(values, nullptr, VrGame::AvHealth);

    std::string limb = m_dealt.limbSet
      ? str::format("the ", m_dealt.limb, " at bone ", nodeName(m_contact.bone), ", limb damage ", m_dealt.limbDamage)
      : std::string("a limb the game picked along the view");

    std::string damage = m_dealt.seen
      ? str::format(", health damage ", m_dealt.healthDamage,
          (m_dealt.flags & VrGame::HitFlagCritical) ? ", critical" : "",
          (m_dealt.flags & VrGame::HitFlagSneakAttack) ? ", sneak attack" : "")
      : std::string();

    Logger::info(str::format("VR: The hit struck ", limb, damage,
      ", health ", health, " of ", baseHealth, " left",
      dead ? ", the target is dead" : ""));
    m_loggedDamage++;
  }


  int32_t VrMeleeSwing::limbOfContact(uintptr_t bodyParts) const {
    uintptr_t actor = m_contact.actor;
    auto getBodyPart = reinterpret_cast<GetBodyPartFn>(VrGame::BodyPartDataGetPart);

    // The game's own mapping walks up from the bone to a body part's node
    int32_t limb = reinterpret_cast<GetDamageLocationFn>(VrGame::ActorGetDamageLocation)(
      actor, nullptr, m_contact.bone);

    if (limb >= 0 && limb < VrGame::LimbWeapon && getBodyPart(bodyParts, nullptr, limb))
      return limb;

    // Bones above no body part's node, such as the pelvis and the lower
    // spine under the torso's node: the nearest body part's node instead
    uintptr_t process = readField<uintptr_t>(actor, VrGame::ActorProcess);

    if (!process)
      return -1;

    auto getDamageNode = virtualFunction<GetDamageNodeFn>(process, VrGame::ProcessDamageNodeSlot);

    int32_t nearest = -1;
    float nearestDistance = MaxDamageNodeDistance;

    for (int32_t i = 0; i < VrGame::LimbWeapon; i++) {
      uintptr_t node = getDamageNode(process, nullptr, i);

      if (!node || !getBodyPart(bodyParts, nullptr, i))
        continue;

      float distance = vrLength(readVector(node, VrGame::ObjectWorldTranslation) - m_contact.hit.location);

      if (distance < nearestDistance) {
        nearest = i;
        nearestDistance = distance;
      }
    }

    return nearest;
  }

}
