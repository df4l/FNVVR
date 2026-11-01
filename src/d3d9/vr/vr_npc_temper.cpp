#include "vr_npc_temper.h"

namespace dxvk {

  bool vrNpcHotTempered(const VrNpcTemper& temper) {
    return !temper.teammate && !temper.inCombat
        && temper.aggression >= VrNpcAggressive && temper.confidence >= VrNpcFoolhardy;
  }


  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper) {
    if (vrNpcHotTempered(temper) && !temper.voicedLine && !temper.idolized)
      return VrNpcGrabResponse::Attack;

    if (temper.canComment && !temper.complaining && !temper.coolingDown)
      return VrNpcGrabResponse::Complain;

    return VrNpcGrabResponse::None;
  }


  bool VrNpcComplaintCooldown::coolingDown(uint32_t actor, double now) const {
    auto entry = m_silentUntil.find(actor);
    return entry != m_silentUntil.end() && now < entry->second;
  }


  void VrNpcComplaintCooldown::start(uint32_t actor, double now, double seconds) {
    for (auto entry = m_silentUntil.begin(); entry != m_silentUntil.end(); ) {
      if (now < entry->second)
        ++entry;
      else
        entry = m_silentUntil.erase(entry);
    }

    m_silentUntil[actor] = now + seconds;
  }


  float vrNpcGripMargin(float playerStrength, float actorStrength, uint32_t hands) {
    return playerStrength + (hands > 1 ? VrNpcTwoHandsStrength : 0.0f) - actorStrength;
  }

}
