#include "test.h"

#include "../vr_grab.h"
#include "../vr_math.h"

using namespace dxvk;

TEST_CASE(hand_body_reaches_the_hand_in_one_step) {
  VrVector3 body = { 1.0f, 2.0f, 3.0f };
  VrVector3 hand = { 1.5f, 2.0f, 2.0f };
  float dt = 0.02f;

  VrHandBodyStep step = vrHandBodyStep(body, hand, dt, 100.0f);

  CHECK(!step.warp);
  CHECK_NEAR(vrLength(body + step.velocity * dt - hand), 0.0f, 1e-5f);
}

TEST_CASE(hand_body_still_hand_stops_the_body) {
  VrVector3 hand = { 4.0f, -1.0f, 0.5f };
  VrHandBodyStep step = vrHandBodyStep(hand, hand, 0.011f, 10.0f);

  CHECK(!step.warp);
  CHECK_NEAR(vrLength(step.velocity), 0.0f, 1e-6f);
}

TEST_CASE(hand_body_jumps_faster_than_a_hand) {
  // Half a metre in a frame: a snap turn, not a swing
  VrHandBodyStep step = vrHandBodyStep({ 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.011f, 12.0f);

  CHECK(step.warp);
  CHECK_NEAR(vrLength(step.velocity), 0.0f, 1e-6f);

  // A fast punch is followed
  step = vrHandBodyStep({ 0.0f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f }, 0.011f, 12.0f);
  CHECK(!step.warp);
}

TEST_CASE(hand_body_jumps_without_time) {
  CHECK(vrHandBodyStep({ 0.0f, 0.0f, 0.0f }, { 0.01f, 0.0f, 0.0f }, 0.0f, 12.0f).warp);
}

TEST_CASE(hand_body_gate_waits_for_a_free_hand) {
  VrHandBodyGate gate;
  float dt = 0.05f;
  float time = 0.0f;

  while (time + dt < VrHandBodyGate::SettleTime - 1e-4f) {
    CHECK(!gate.update(true, dt));
    time += dt;
  }

  CHECK(gate.update(true, dt));
  CHECK(gate.update(true, dt));

  // The grip closes: no push at once, and the wait starts again
  CHECK(!gate.update(false, dt));
  CHECK(!gate.update(true, dt));

  gate.reset();
  CHECK(!gate.update(true, dt));
}
