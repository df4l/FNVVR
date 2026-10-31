#include <cstdio>
#include <cstring>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_npc_reaction.h"

namespace dxvk {

  namespace {

    using ActorTestFn     = bool (__fastcall*)(uint8_t* actor, void* unused);
    using SkipsBehaviorFn = bool (__fastcall*)(uint8_t* actor, void* unused, uint32_t behavior);
    using IsDeadFn        = uint32_t (__fastcall*)(uint8_t* actor, void* unused, uint32_t unknown);
    using SitStateFn      = uint32_t (__fastcall*)(uint8_t* actor, void* unused);
    using GetAvFloatFn    = float (__fastcall*)(uint8_t* owner, void* unused, uint32_t index);
    using GetTopicFn      = void* (__cdecl*)(uint32_t type, uint32_t index);
    using GreetFn         = void (__fastcall*)(uint8_t* actor, void* unused, void* topic);
    using EnterCombatFn   = void (__fastcall*)(uint8_t* process, void* unused, uint8_t* actor, uint8_t* target,
      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t aggressor,
      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

    constexpr uint32_t MaxLoggedReactions = 50;

    VrNpcReaction* g_npcReaction = nullptr;

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

    bool actorTest(uintptr_t function, uint8_t* actor) {
      return reinterpret_cast<ActorTestFn>(function)(actor, nullptr);
    }

    float actorValue(uint8_t* actor, uint32_t index) {
      uint8_t* values = actor + VrGame::ActorValueOwnerBase;
      return virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerFloatSlot)(values, nullptr, index);
    }

    float* commentTimer() {
      return reinterpret_cast<float*>(VrGame::ProcessListsSingleton + VrGame::ProcessListsCommentTimer);
    }

    std::string hexId(uint32_t id) {
      char text[16];
      std::snprintf(text, sizeof(text), "%08X", id);
      return text;
    }

  }


  VrNpcReaction::~VrNpcReaction() {
    g_npcReaction = nullptr;
  }


  std::unique_ptr<VrNpcReaction> VrNpcReaction::install() {
    if (g_npcReaction)
      return nullptr;

    // Each function is checked by its call in CommentOnPlayerAction
    bool found = VrGameMemory::callsTo(VrGame::TopicGetTopicCallSite, VrGame::TopicGetTopic)
      && VrGameMemory::callsTo(VrGame::ActorStartGreetingPlayerCallSite, VrGame::ActorStartGreetingPlayer)
      && VrGameMemory::callsTo(VrGame::ActorCanSpeakCallSite, VrGame::ActorCanSpeak)
      && VrGameMemory::callsTo(VrGame::ActorIsInCombatCallSite, VrGame::ActorIsInCombat)
      && VrGameMemory::callsTo(VrGame::ActorSkipsBehaviorCallSite, VrGame::ActorSkipsBehavior)
      && VrGameMemory::callsTo(VrGame::ActorIsPlayerTeammateCallSite, VrGame::ActorIsPlayerTeammate)
      && VrGameMemory::readable(VrGame::ProcessListsSingleton + VrGame::ProcessListsCommentTimer, sizeof(float))
      && VrGameMemory::readable(VrGame::CommentOnPlayerActionsTimerSetting, 2 * sizeof(float));

    if (!found)
      return nullptr;

    std::unique_ptr<VrNpcReaction> reaction(new VrNpcReaction());
    g_npcReaction = reaction.get();

    Logger::info("VR: Grabbed actors complain as when aimed at, foolhardy aggressive ones attack");
    return reaction;
  }


  void VrNpcReaction::grabbed(uint8_t* actor) {
    VrNpcTemper temper = readTemper(actor);
    VrNpcGrabResponse response = vrNpcGrabResponse(temper);

    switch (response) {
      case VrNpcGrabResponse::Attack:   attack(actor);   break;
      case VrNpcGrabResponse::Complain: complain(actor); break;
      case VrNpcGrabResponse::None:                      break;
    }

    if (m_loggedReactions < MaxLoggedReactions) {
      const char* what = response == VrNpcGrabResponse::Attack ? "attacks the player"
        : response == VrNpcGrabResponse::Complain ? "complains"
        : temper.canComment ? "says nothing, another comment was made a moment ago"
        : "says nothing, it cannot comment now";

      Logger::info(str::format("VR: Grabbed actor ", hexId(readField<uint32_t>(actor, VrGame::FormId)),
        " (aggression ", temper.aggression, ", confidence ", temper.confidence,
        temper.teammate ? ", companion" : "", temper.inCombat ? ", in combat" : "", ") ", what));
      m_loggedReactions++;
    }
  }


  VrNpcTemper VrNpcReaction::readTemper(uint8_t* actor) const {
    VrNpcTemper temper;
    temper.aggression = actorValue(actor, VrGame::AvAggression);
    temper.confidence = actorValue(actor, VrGame::AvConfidence);
    temper.teammate   = actorTest(VrGame::ActorIsPlayerTeammate, actor);
    temper.inCombat   = actorTest(VrGame::ActorIsInCombat, actor);

    // The checks of CommentOnPlayerAction for the actor aimed at; the
    // distance is that of the hand
    bool dead = virtualFunction<IsDeadFn>(actor, VrGame::ActorIsDeadSlot)(actor, nullptr, 0) & 0xFF;
    bool asleep = virtualFunction<SitStateFn>(actor, VrGame::GetSitStateSlot)(actor, nullptr) == VrGame::SitSleepSleeping;
    bool skips = reinterpret_cast<SkipsBehaviorFn>(VrGame::ActorSkipsBehavior)(
      actor, nullptr, VrGame::BehaviorReactToPlayerActions);

    temper.canComment = !dead && !asleep && !temper.inCombat
      && actorTest(VrGame::ActorCanSpeak, actor)
      && (!skips || temper.teammate);
    temper.commentTimer = *commentTimer();
    return temper;
  }


  void VrNpcReaction::complain(uint8_t* actor) {
    void* topic = reinterpret_cast<GetTopicFn>(VrGame::TopicGetTopic)(
      VrGame::DialogueTypeTopic, VrGame::TopicPlayerAiming);

    if (!topic)
      return;

    reinterpret_cast<GreetFn>(VrGame::ActorStartGreetingPlayer)(actor, nullptr, topic);
    *commentTimer() = readField<float>(reinterpret_cast<const uint8_t*>(VrGame::CommentOnPlayerActionsTimerSetting),
      VrGame::SettingValue);
  }


  void VrNpcReaction::attack(uint8_t* actor) {
    uint8_t* process = readField<uint8_t*>(actor, VrGame::ActorProcess);
    uint8_t* player  = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

    if (!process || !player)
      return;

    // As the script command StartCombat
    bool guard = virtualFunction<ActorTestFn>(actor, VrGame::ActorIsGuardSlot)(actor, nullptr);

    virtualFunction<EnterCombatFn>(process, VrGame::ProcessEnterCombatSlot)(process, nullptr,
      actor, player, 1, 1, 0, 0, 0, guard ? 0 : 1, 0, 1, 0, 1, 0);
  }

}
