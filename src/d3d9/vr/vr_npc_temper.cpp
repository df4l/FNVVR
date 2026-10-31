#include "vr_npc_temper.h"

namespace dxvk {

  VrNpcGrabResponse vrNpcGrabResponse(const VrNpcTemper& temper) {
    if (!temper.teammate && !temper.inCombat
     && temper.aggression >= VrNpcAggressive && temper.confidence >= VrNpcFoolhardy)
      return VrNpcGrabResponse::Attack;

    if (temper.canComment && temper.commentTimer <= 0.0f)
      return VrNpcGrabResponse::Complain;

    return VrNpcGrabResponse::None;
  }

}
