#include <cmath>

#include "test.h"

#include "../vr_game_space.h"
#include "../vr_npc_drag.h"

using namespace dxvk;

namespace {

  constexpr float Step = 1.0f / 60.0f;
  constexpr float Pi   = 3.14159265f;
  constexpr float MetresPerSecond = VrGameUnitsPerMetre;

  VrNpcDragInput input(float pullX, float pullY) {
    VrNpcDragInput in;
    in.pull     = { pullX * VrGameUnitsPerMetre, pullY * VrGameUnitsPerMetre };
    in.maxSpeed = 2.0f * MetresPerSecond;
    in.dt       = Step;
    return in;
  }

  void run(VrNpcDrag& drag, const VrNpcDragInput& in, uint32_t steps) {
    for (uint32_t i = 0; i < steps; i++)
      drag.update(in);
  }

  bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
  }

}


TEST_CASE(npc_ground_vectors_follow_the_actor_heading) {
  // Facing east, ahead is east and the right is south
  VrNpcGroundVector ahead = vrNpcToWorld({ 0.0f, 1.0f }, Pi / 2.0f);
  CHECK(near(ahead.x, 1.0f));
  CHECK(near(ahead.y, 0.0f));

  VrNpcGroundVector right = vrNpcToWorld({ 1.0f, 0.0f }, Pi / 2.0f);
  CHECK(near(right.x, 0.0f));
  CHECK(near(right.y, -1.0f));

  VrNpcGroundVector back = vrNpcToActor(vrNpcToWorld({ 0.3f, -0.7f }, 1.2f), 1.2f);
  CHECK(near(back.x, 0.3f));
  CHECK(near(back.y, -0.7f));

  CHECK(near(vrNpcHeadingOf({ 1.0f, 0.0f }), Pi / 2.0f));
  CHECK(near(vrNpcTurnBetween(3.0f, -3.0f), 2.0f * Pi - 6.0f));
}


TEST_CASE(npc_drag_speed_grows_with_the_margin_of_strength) {
  CHECK(near(vrNpcDragMaxSpeed(1.0f) / MetresPerSecond, 1.4f));
  CHECK(near(vrNpcDragMaxSpeed(2.0f) / MetresPerSecond, 2.1f));
  CHECK(near(vrNpcDragMaxSpeed(20.0f) / MetresPerSecond, 4.0f));
}


TEST_CASE(npc_stays_within_the_dead_band_of_the_hand) {
  VrNpcDrag drag;
  run(drag, input(0.10f, 0.0f), 30);

  CHECK(!drag.translating());
  CHECK(drag.moveMode() == 0);
  CHECK(drag.velocity().x == 0.0f);
  CHECK(!drag.turned());
}


TEST_CASE(npc_follows_a_pull_and_stops_once_the_hand_is_reached) {
  VrNpcDrag drag;
  run(drag, input(0.0f, 0.5f), 30);

  CHECK(drag.translating());
  CHECK(drag.velocity().y > 1.5f * MetresPerSecond);
  CHECK(drag.velocity().y <= 2.0f * MetresPerSecond);

  // Inside the dead band only a pull already started goes on
  run(drag, input(0.0f, 0.10f), 1);
  CHECK(drag.translating());

  run(drag, input(0.0f, 0.02f), 60);
  CHECK(!drag.translating());
  CHECK(drag.moveMode() == 0);
  CHECK(drag.velocity().y == 0.0f);
}


TEST_CASE(npc_follows_the_player_without_a_pull) {
  VrNpcDrag drag;
  VrNpcDragInput in = input(0.0f, 0.0f);
  in.player = { 0.0f, 1.0f * MetresPerSecond };
  run(drag, in, 60);

  CHECK(drag.translating());
  CHECK(near(drag.velocity().y / MetresPerSecond, 0.8f));
}


TEST_CASE(npc_walks_or_runs_with_its_speed) {
  VrNpcDragInput slow = input(0.0f, 0.5f);
  slow.maxSpeed = 1.4f * MetresPerSecond;

  VrNpcDrag walker;
  run(walker, slow, 60);
  CHECK(walker.moveMode() == (VrNpcMoveForward | VrNpcMoveWalking));

  VrNpcDragInput fast = input(0.0f, 0.5f);
  fast.maxSpeed = 4.0f * MetresPerSecond;

  VrNpcDrag runner;
  run(runner, fast, 60);
  CHECK(runner.moveMode() == (VrNpcMoveForward | VrNpcMoveRunning));
}


TEST_CASE(npc_turns_its_front_to_a_move_from_the_side) {
  VrNpcDrag drag;
  VrNpcDragInput in = input(0.5f, 0.0f);

  drag.update(in);
  CHECK(drag.moveMode() == 0 || (drag.moveMode() & VrNpcMoveRight));

  for (uint32_t i = 0; i < 120; i++) {
    drag.update(in);
    in.heading = drag.heading();
  }

  CHECK(near(in.heading, Pi / 2.0f, 0.05f));
  CHECK(drag.moveMode() & VrNpcMoveForward);
}


TEST_CASE(npc_backs_away_without_turning_round) {
  VrNpcDrag drag;
  VrNpcDragInput in = input(0.0f, -0.5f);

  for (uint32_t i = 0; i < 120; i++) {
    drag.update(in);
    in.heading = drag.heading();
  }

  CHECK(near(in.heading, 0.0f, 0.05f));
  CHECK(drag.moveMode() & VrNpcMoveBackward);
}


TEST_CASE(npc_is_twisted_by_both_hands_past_a_dead_band) {
  VrNpcDrag drag;
  VrNpcDragInput in = input(0.0f, 0.0f);

  in.twist = 0.5f;
  drag.update(in);
  CHECK(!drag.turned());

  in.twist = 1.0f;
  drag.update(in);
  CHECK(drag.turned());
  CHECK(near(drag.heading(), 2.0f * Step, 0.001f));

  // Started, it goes on down to the lower limit
  in.twist = 0.5f;
  drag.update(in);
  CHECK(drag.turned());

  in.twist = 0.05f;
  drag.update(in);
  CHECK(!drag.turned());
}
