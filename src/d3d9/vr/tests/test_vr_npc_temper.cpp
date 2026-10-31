#include "test.h"

#include "../vr_npc_temper.h"

using namespace dxvk;

namespace {

  VrNpcTemper temper(float aggression, float confidence) {
    VrNpcTemper t;
    t.aggression = aggression;
    t.confidence = confidence;
    t.canComment = true;
    return t;
  }

}


TEST_CASE(npc_foolhardy_and_aggressive_attacks) {
  CHECK(vrNpcGrabResponse(temper(1.0f, 4.0f)) == VrNpcGrabResponse::Attack);
  CHECK(vrNpcGrabResponse(temper(3.0f, 4.0f)) == VrNpcGrabResponse::Attack);
}


TEST_CASE(npc_short_of_foolhardy_or_aggressive_complains) {
  CHECK(vrNpcGrabResponse(temper(0.0f, 4.0f)) == VrNpcGrabResponse::Complain);
  CHECK(vrNpcGrabResponse(temper(3.0f, 3.0f)) == VrNpcGrabResponse::Complain);
}


TEST_CASE(npc_companion_or_fighting_actor_does_not_attack) {
  VrNpcTemper companion = temper(1.0f, 4.0f);
  companion.teammate = true;
  CHECK(vrNpcGrabResponse(companion) == VrNpcGrabResponse::Complain);

  VrNpcTemper fighting = temper(1.0f, 4.0f);
  fighting.inCombat   = true;
  fighting.canComment = false;
  CHECK(vrNpcGrabResponse(fighting) == VrNpcGrabResponse::None);
}


TEST_CASE(npc_complains_only_under_the_game_comment_rules) {
  VrNpcTemper silent = temper(0.0f, 2.0f);
  silent.canComment = false;
  CHECK(vrNpcGrabResponse(silent) == VrNpcGrabResponse::None);

  VrNpcTemper waiting = temper(0.0f, 2.0f);
  waiting.commentTimer = 2.0f;
  CHECK(vrNpcGrabResponse(waiting) == VrNpcGrabResponse::None);
}
