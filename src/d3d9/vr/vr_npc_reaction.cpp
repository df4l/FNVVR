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
    using MatchingInfoFn  = uint8_t* (__fastcall*)(void* topic, void* unused, uint8_t* speaker, uint8_t* target);
    using LoadResponsesFn = void* (__fastcall*)(uint8_t* info, void* unused, void** wrapper);
    using DestroyListFn   = void (__fastcall*)(void** wrapper, void* unused);
    using AudioPathFn     = bool (__fastcall*)(void* response, void* unused, uint8_t* speaker, void* quest,
                                               void* topic, uint8_t* info, void* path);
    using StringFn        = void (__fastcall*)(void* string, void* unused);
    using LocateFn        = int32_t (__cdecl*)(const char* name, char* path, uint32_t skip, int32_t archive);
    using ActorBaseFn     = uint8_t* (__fastcall*)(uint8_t* actor, void* unused);
    using FactionChangesFn = uint8_t* (__fastcall*)(void* extraData, void* unused);
    using ListFactionsFn  = uint32_t (__fastcall*)(uint8_t* actor, void* unused, uint8_t** factions, uint32_t size,
                                                   void* list, void* changes);
    using ReputationFn    = uint8_t (__fastcall*)(uint8_t* reputation, void* unused, uint32_t axis);
    using EnterCombatFn   = void (__fastcall*)(uint8_t* process, void* unused, uint8_t* actor, uint8_t* target,
      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t aggressor,
      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

    constexpr uint32_t MaxLoggedReactions = 50;

    // The game picks at random among an actor's random lines, some of which
    // may have no audio for its voice: asked this often, one with audio
    // comes up unless they are very few
    constexpr uint32_t VoicedLineTries = 8;

    constexpr uint32_t MaxLoggedAudioMisses = 20;

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

    // Each function is checked by a call of the game's own
    bool found = VrGameMemory::callsTo(VrGame::TopicGetTopicCallSite, VrGame::TopicGetTopic)
      && VrGameMemory::callsTo(VrGame::ActorStartGreetingPlayerCallSite, VrGame::ActorStartGreetingPlayer)
      && VrGameMemory::callsTo(VrGame::ActorCanSpeakCallSite, VrGame::ActorCanSpeak)
      && VrGameMemory::callsTo(VrGame::ActorIsInCombatCallSite, VrGame::ActorIsInCombat)
      && VrGameMemory::callsTo(VrGame::ActorSkipsBehaviorCallSite, VrGame::ActorSkipsBehavior)
      && VrGameMemory::callsTo(VrGame::ActorIsPlayerTeammateCallSite, VrGame::ActorIsPlayerTeammate)
      && VrGameMemory::callsTo(VrGame::TopicGetMatchingInfoCallSite, VrGame::TopicGetMatchingInfo)
      && VrGameMemory::callsTo(VrGame::TopicInfoLoadResponsesCallSite, VrGame::TopicInfoLoadResponses)
      && VrGameMemory::callsTo(VrGame::ResponseListDestroyCallSite, VrGame::ResponseListDestroy)
      && VrGameMemory::callsTo(VrGame::ResponseGetAudioFullPathCallSite, VrGame::ResponseGetAudioFullPath)
      && VrGameMemory::callsTo(VrGame::BSStringConstructCallSite, VrGame::BSStringConstruct)
      && VrGameMemory::callsTo(VrGame::BSStringDestroyCallSite, VrGame::BSStringDestroy)
      && VrGameMemory::callsTo(VrGame::FileFinderLocateCallSite, VrGame::FileFinderLocate)
      && VrGameMemory::callsTo(VrGame::ActorGetActorBaseCallSite, VrGame::ActorGetActorBase)
      && VrGameMemory::callsTo(VrGame::ExtraGetFactionChangesCallSite, VrGame::ExtraGetFactionChanges)
      && VrGameMemory::callsTo(VrGame::ActorIntegrateFactionListsCallSite, VrGame::ActorIntegrateFactionLists)
      && VrGameMemory::callsTo(VrGame::ReputationGetLevelCallSite, VrGame::ReputationGetLevel)
      && VrGameMemory::readable(VrGame::ProcessListsSingleton + VrGame::ProcessListsCommentTimer, sizeof(float))
      && VrGameMemory::readable(VrGame::CommentOnPlayerActionsTimerSetting, 2 * sizeof(float));

    if (!found)
      return nullptr;

    std::unique_ptr<VrNpcReaction> reaction(new VrNpcReaction());
    g_npcReaction = reaction.get();

    Logger::info("VR: Grabbed actors complain as when aimed at, foolhardy aggressive ones with no recorded line attack unless idolized");
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

      uint8_t* player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

      Logger::info(str::format("VR: Grabbed actor ", hexId(readField<uint32_t>(actor, VrGame::FormId)),
        " (aggression ", temper.aggression, ", confidence ", temper.confidence,
        ", strength ", actorValue(actor, VrGame::AvStrength),
        " against the player's ", player ? actorValue(player, VrGame::AvStrength) : 0.0f,
        !vrNpcHotTempered(temper) ? "" : temper.voicedLine ? ", a recorded line" : ", no recorded line",
        temper.idolized ? ", idolizes the player" : "",
        temper.teammate ? ", companion" : "", temper.inCombat ? ", in combat" : "", ") ", what));
      m_loggedReactions++;
    }
  }


  VrNpcTemper VrNpcReaction::readTemper(uint8_t* actor) {
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

    if (vrNpcHotTempered(temper)) {
      temper.idolized   = factionIdolizesPlayer(actor);
      temper.voicedLine = hasVoicedLine(actor);
    }

    return temper;
  }


  bool VrNpcReaction::hasVoicedLine(uint8_t* actor) {
    void* topic = reinterpret_cast<GetTopicFn>(VrGame::TopicGetTopic)(
      VrGame::DialogueTypeTopic, VrGame::TopicPlayerAiming);
    uint8_t* player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

    if (!topic || !player)
      return false;

    for (uint32_t i = 0; i < VoicedLineTries; i++) {
      uint8_t* info = reinterpret_cast<MatchingInfoFn>(VrGame::TopicGetMatchingInfo)(topic, nullptr, actor, player);

      if (!info)
        return false;

      if (isVoiced(actor, topic, info))
        return true;
    }

    return false;
  }


  bool VrNpcReaction::isVoiced(uint8_t* actor, void* topic, uint8_t* info) {
    void* responses = nullptr;
    reinterpret_cast<LoadResponsesFn>(VrGame::TopicInfoLoadResponses)(info, nullptr, &responses);

    bool voiced = false;

    if (responses) {
      alignas(4) uint8_t path[VrGame::BSStringSize];
      reinterpret_cast<StringFn>(VrGame::BSStringConstruct)(path, nullptr);

      reinterpret_cast<AudioPathFn>(VrGame::ResponseGetAudioFullPath)(responses, nullptr, actor,
        readField<void*>(info, VrGame::TopicInfoQuest), topic, info, path);

      const char* name = readField<const char*>(path, 0);
      voiced = name && reinterpret_cast<LocateFn>(VrGame::FileFinderLocate)(name, nullptr, 0, -1);

      if (!voiced && m_loggedAudioMisses < MaxLoggedAudioMisses) {
        Logger::info(str::format("VR: No audio for ", hexId(readField<uint32_t>(actor, VrGame::FormId)),
          ": ", name ? name : "no path"));
        m_loggedAudioMisses++;
      }

      reinterpret_cast<StringFn>(VrGame::BSStringDestroy)(path, nullptr);
    }

    reinterpret_cast<DestroyListFn>(VrGame::ResponseListDestroy)(&responses, nullptr);
    return voiced;
  }


  bool VrNpcReaction::factionIdolizesPlayer(uint8_t* actor) const {
    uint8_t* base = reinterpret_cast<ActorBaseFn>(VrGame::ActorGetActorBase)(actor, nullptr);

    if (!base)
      return false;

    uint8_t* changes = reinterpret_cast<FactionChangesFn>(VrGame::ExtraGetFactionChanges)(actor + VrGame::RefExtraData, nullptr);

    uint8_t* factions[VrGame::MaxFactions];
    uint32_t count = reinterpret_cast<ListFactionsFn>(VrGame::ActorIntegrateFactionLists)(actor, nullptr,
      factions, VrGame::MaxFactions, base + VrGame::ActorBaseFactions,
      changes ? readField<void*>(changes, VrGame::FactionChangesList) : nullptr);

    for (uint32_t i = 0; i < count; i++) {
      uint8_t* reputation = factions[i] ? readField<uint8_t*>(factions[i], VrGame::FactionReputation) : nullptr;

      if (!reputation)
        continue;

      auto level = reinterpret_cast<ReputationFn>(VrGame::ReputationGetLevel);

      if (level(reputation, nullptr, VrGame::ReputationFame) == VrGame::ReputationTopLevel
       && level(reputation, nullptr, VrGame::ReputationInfamy) == 0)
        return true;
    }

    return false;
  }


  bool VrNpcReaction::heldInPlace(uint8_t* actor, uint8_t* player) const {
    return vrNpcHeldInPlace(actorValue(player, VrGame::AvStrength), actorValue(actor, VrGame::AvStrength));
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
