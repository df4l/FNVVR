#include "vr_npc_temper.h"

namespace dxvk {

  bool vrNpcHotTempered(const VrNpcTemper& temper) {
    return !temper.teammate && !temper.inCombat
        && temper.aggression >= VrNpcAggressive && temper.confidence >= VrNpcFoolhardy;
  }


  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper) {
    if (vrNpcHotTempered(temper) && !temper.voicedLine && !temper.idolized)
      return VrNpcGrabResponse::Attack;

    if (temper.canComment && temper.commentTimer <= 0.0f)
      return VrNpcGrabResponse::Complain;

    return VrNpcGrabResponse::None;
  }


  bool vrNpcHeldInPlace(float playerStrength, float actorStrength) {
    return playerStrength > actorStrength;
  }

}
