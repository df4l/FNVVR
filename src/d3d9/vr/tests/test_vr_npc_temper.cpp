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


TEST_CASE(npc_hot_tempered_without_a_recorded_line_attacks) {
  CHECK(vrNpcGrabResponse(temper(1.0f, 4.0f)) == VrNpcGrabResponse::Attack);
  CHECK(vrNpcGrabResponse(temper(3.0f, 4.0f)) == VrNpcGrabResponse::Attack);
}


TEST_CASE(npc_hot_tempered_with_a_recorded_line_complains) {
  VrNpcTemper voiced = temper(1.0f, 4.0f);
  voiced.voicedLine = true;
  CHECK(vrNpcGrabResponse(voiced) == VrNpcGrabResponse::Complain);
}


TEST_CASE(npc_does_not_attack_a_player_its_faction_idolizes) {
  VrNpcTemper idolized = temper(1.0f, 4.0f);
  idolized.idolized = true;
  CHECK(vrNpcGrabResponse(idolized) == VrNpcGrabResponse::Complain);
}


TEST_CASE(npc_short_of_foolhardy_or_aggressive_complains) {
  CHECK(vrNpcGrabResponse(temper(0.0f, 4.0f)) == VrNpcGrabResponse::Complain);
  CHECK(vrNpcGrabResponse(temper(3.0f, 3.0f)) == VrNpcGrabResponse::Complain);
  CHECK(!vrNpcHotTempered(temper(0.0f, 4.0f)));
  CHECK(!vrNpcHotTempered(temper(3.0f, 3.0f)));
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


TEST_CASE(npc_is_held_only_by_a_stronger_grip) {
  CHECK(vrNpcGripMargin(6.0f, 5.0f, 1) > 0.0f);
  CHECK(!(vrNpcGripMargin(5.0f, 5.0f, 1) > 0.0f));
  CHECK(!(vrNpcGripMargin(4.0f, 5.0f, 1) > 0.0f));
}


TEST_CASE(npc_grip_of_both_hands_adds_one_point_of_strength) {
  CHECK(vrNpcGripMargin(5.0f, 5.0f, 2) > 0.0f);
  CHECK(!(vrNpcGripMargin(4.0f, 5.0f, 2) > 0.0f));
}
