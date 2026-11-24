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

  CHECK(vrNpcGrabResponse(temper(0.0f, 2.0f)) == VrNpcGrabResponse::Complain);
}


TEST_CASE(npc_does_not_restart_the_complaint_it_is_saying) {
  VrNpcTemper saying = temper(0.0f, 2.0f);
  saying.complaining = true;
  CHECK(vrNpcGrabResponse(saying) == VrNpcGrabResponse::None);

  VrNpcTemper hot = temper(1.0f, 4.0f);
  hot.complaining = true;
  CHECK(vrNpcGrabResponse(hot) == VrNpcGrabResponse::Attack);
}


TEST_CASE(npc_cooling_down_stays_silent_but_can_still_attack) {
  VrNpcTemper cooling = temper(0.0f, 2.0f);
  cooling.coolingDown = true;
  CHECK(vrNpcGrabResponse(cooling) == VrNpcGrabResponse::None);

  VrNpcTemper hot = temper(1.0f, 4.0f);
  hot.coolingDown = true;
  CHECK(vrNpcGrabResponse(hot) == VrNpcGrabResponse::Attack);
}


TEST_CASE(npc_complaint_cooldown_silences_the_same_actor_for_its_duration) {
  VrNpcComplaintCooldown cooldown;
  CHECK(!cooldown.coolingDown(1, 0.0));

  cooldown.start(1, 5.0, 10.0);
  CHECK(cooldown.coolingDown(1, 5.0));
  CHECK(cooldown.coolingDown(1, 14.9));
  CHECK(!cooldown.coolingDown(1, 15.0));
}


TEST_CASE(npc_complaint_cooldown_leaves_other_actors_free) {
  VrNpcComplaintCooldown cooldown;
  cooldown.start(1, 0.0, 10.0);
  CHECK(!cooldown.coolingDown(2, 1.0));

  cooldown.start(2, 1.0, 10.0);
  CHECK(cooldown.coolingDown(1, 2.0));
  CHECK(cooldown.coolingDown(2, 2.0));
}


TEST_CASE(npc_complaint_cooldown_can_restart_after_it_ended) {
  VrNpcComplaintCooldown cooldown;
  cooldown.start(1, 0.0, 10.0);
  cooldown.start(2, 20.0, 10.0);
  CHECK(!cooldown.coolingDown(1, 20.0));

  cooldown.start(1, 20.0, 10.0);
  CHECK(cooldown.coolingDown(1, 29.0));
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
