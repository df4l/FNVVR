#include <cmath>

#include "test.h"

#include "../vr_game_space.h"
#include "../vr_math.h"
#include "../vr_npc_drag.h"
#include "../vr_npc_manhandle.h"

using namespace dxvk;

namespace {

  constexpr float Step = 1.0f / 60.0f;
  constexpr float MetresPerSecond = VrGameUnitsPerMetre;

  bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
  }

}


TEST_CASE(hand_motion_finds_the_fastest_of_the_last_frames) {
  VrHandMotion motion;
  float speeds[] = { 1.0f, 3.0f, 5.0f, 3.0f, 1.0f };

  for (float speed : speeds)
    motion.update({ speed * Step, 0.0f, 0.0f }, Step);

  // Around the peak
  CHECK(near(motion.peak().x, 11.0f / 3.0f));
  CHECK(near(motion.average().x, 13.0f / 5.0f));

  motion.reset();
  CHECK(motion.peak().x == 0.0f);
}


TEST_CASE(hand_motion_keeps_a_peak_at_the_end_of_its_frames) {
  VrHandMotion motion;
  motion.update({ 1.0f, 0.0f, 0.0f }, 1.0f);
  CHECK(near(motion.peak().x, 1.0f));

  // A frame of no time teaches nothing
  motion.update({ 5.0f, 0.0f, 0.0f }, 0.0f);
  CHECK(near(motion.peak().x, 1.0f));
}


TEST_CASE(yank_and_shove_need_a_fast_hand) {
  CHECK(!vrNpcYankIsFast({ 3.9f * MetresPerSecond, 0.0f, 0.0f }));
  CHECK(vrNpcYankIsFast({ 0.0f, 4.1f * MetresPerSecond, 0.0f }));
  CHECK(!vrNpcShoveIsFast({ 2.4f * MetresPerSecond, 0.0f, 0.0f }));
  CHECK(vrNpcShoveIsFast({ 2.6f * MetresPerSecond, 0.0f, 0.0f }));
}


TEST_CASE(yank_comes_from_behind_the_hands_move) {
  VrVector3 centre = { 10.0f, 20.0f, 30.0f };
  VrNpcKnockPush push = vrNpcYankPush({ 5.0f * MetresPerSecond, 0.0f, 0.0f }, centre, 0.1f);

  CHECK(push.source.x < centre.x - 50.0f * VrGameUnitsPerMetre);
  CHECK(near(push.source.y, centre.y));
  CHECK(near(push.source.z, centre.z));
  CHECK(near(push.force, 5.0f * MetresPerSecond * 0.45f * 0.1f));

  VrNpcKnockPush still = vrNpcYankPush({ }, centre, 0.1f);
  CHECK(still.force == 0.0f);
  CHECK(near(still.source.x, centre.x));
}


TEST_CASE(only_tiny_actors_are_knocked_down_by_the_grip) {
  CHECK(vrNpcKnockedDownByGrip(5.0f));
  CHECK(!vrNpcKnockedDownByGrip(6.0f));
  CHECK(!vrNpcKnockedDownByGrip(0.0f));
}


TEST_CASE(holding_costs_more_for_a_fighter_and_a_healthy_actor) {
  CHECK(near(vrNpcGripCost(false, 1.0f), 3.0f));
  CHECK(near(vrNpcGripCost(true, 1.0f), 10.0f));
  CHECK(near(vrNpcGripCost(false, 0.0f), 0.6f));
}


TEST_CASE(speed_limit_follows_health_breath_and_friendship) {
  float full = vrNpcGripSpeedLimit(1.0f, 1.0f, 1.0f, false);
  CHECK(near(full, vrNpcDragMaxSpeed(1.0f)));

  CHECK(vrNpcGripSpeedLimit(1.0f, 0.0f, 1.0f, false) > full);
  CHECK(near(vrNpcGripSpeedLimit(1.0f, 1.0f, 0.0f, false), full * 0.5f));
  CHECK(near(vrNpcGripSpeedLimit(1.0f, 1.0f, 1.0f, true), full * 2.5f));
  CHECK(near(vrNpcGripSpeedLimit(20.0f, 0.0f, 1.0f, true), 6.0f * MetresPerSecond));
}


TEST_CASE(player_brake_holds_then_fades) {
  VrNpcPlayerBrake brake;
  CHECK(brake.strength() == 0.0f);

  brake.update(true, 100.0f, Step);
  CHECK(brake.strength() == 1.0f);
  CHECK(near(vrNpcPlayerMoveFactor(200.0f, brake.limit(), brake.strength()), 0.5f));
  CHECK(near(vrNpcPlayerMoveFactor(50.0f, brake.limit(), brake.strength()), 1.0f));

  brake.update(false, 0.0f, 2.5f);
  CHECK(near(brake.strength(), 0.5f));
  CHECK(near(vrNpcPlayerMoveFactor(200.0f, brake.limit(), brake.strength()), 0.75f));

  brake.update(false, 0.0f, 10.0f);
  CHECK(brake.strength() == 0.0f);
  CHECK(vrNpcPlayerMoveFactor(200.0f, brake.limit(), brake.strength()) == 1.0f);
}


TEST_CASE(anger_grows_from_complaint_to_assault) {
  VrNpcAnger anger;

  CHECK(anger.update(true, false, 0.4f) == VrNpcAngerAction::None);
  CHECK(anger.update(true, false, 0.2f) == VrNpcAngerAction::Complain);
  CHECK(anger.update(true, false, 8.0f) == VrNpcAngerAction::Complain);

  // Once
  CHECK(anger.update(true, false, 1.0f) == VrNpcAngerAction::Assault);
  CHECK(anger.update(true, false, 1.0f) == VrNpcAngerAction::Complain);
}


TEST_CASE(knocking_down_is_an_assault_at_once) {
  VrNpcAnger anger;
  anger.add(VrNpcKnockDownAnger);
  CHECK(anger.update(false, false, Step) == VrNpcAngerAction::Assault);
}


TEST_CASE(anger_fades_when_left_alone) {
  VrNpcAnger anger;
  anger.update(true, false, 3.0f);

  // Not at first
  anger.update(false, false, 9.0f);
  CHECK(near(anger.anger(), 3.0f));

  anger.update(false, false, 1.0f);
  CHECK(near(anger.anger(), 2.0f));

  anger.update(false, false, 5.0f);
  CHECK(anger.anger() == 0.0f);
}


TEST_CASE(a_fight_settles_the_anger) {
  VrNpcAnger anger;
  anger.add(VrNpcKnockDownAnger);

  CHECK(anger.update(false, true, Step) == VrNpcAngerAction::None);
  CHECK(anger.update(true, true, Step) == VrNpcAngerAction::None);

  CHECK(anger.update(false, false, Step) == VrNpcAngerAction::None);
  CHECK(anger.anger() == 0.0f);
}
