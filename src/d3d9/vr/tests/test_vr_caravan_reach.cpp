#include <cmath>

#include "test.h"

#include "../vr_caravan_reach.h"
#include "../vr_math.h"

using namespace dxvk;

namespace {

  bool near(const VrVector3& a, const VrVector3& b, float tolerance = 1e-3f) {
    return vrLength(a - b) < tolerance;
  }

}


TEST_CASE(arm_reach_puts_the_wrist_on_the_point) {
  const VrVector3 shoulder = { 0.0f, 0.0f, 0.0f };
  const VrVector3 elbow    = { 0.0f, 0.0f, -3.0f };
  const VrVector3 wrist    = { 0.0f, 2.0f, -3.0f };
  const VrVector3 target   = { 2.0f, 2.0f, -2.0f };

  VrArmReach reach = vrArmReach(shoulder, elbow, wrist, target);

  VrVector3 newElbow = vrCaravanTransformPoint(reach.upper, elbow);
  VrVector3 newWrist = vrCaravanTransformPoint(reach.fore, vrCaravanTransformPoint(reach.upper, wrist));

  CHECK(near(newWrist, target));
  CHECK(near(vrCaravanTransformPoint(reach.upper, shoulder), shoulder));

  // The bones keep their lengths
  CHECK(std::abs(vrLength(newElbow - shoulder) - 3.0f) < 1e-3f);
  CHECK(std::abs(vrLength(newWrist - newElbow) - 2.0f) < 1e-3f);
}


TEST_CASE(arm_reach_keeps_the_pose_for_its_own_wrist) {
  const VrVector3 shoulder = { 1.0f, 2.0f, 3.0f };
  const VrVector3 elbow    = { 1.0f, 2.0f, 0.0f };
  const VrVector3 wrist    = { 1.0f, 4.0f, 0.5f };

  VrArmReach reach = vrArmReach(shoulder, elbow, wrist, wrist);

  CHECK(near(vrCaravanTransformPoint(reach.upper, elbow), elbow));
  CHECK(near(vrCaravanTransformPoint(reach.fore, vrCaravanTransformPoint(reach.upper, wrist)), wrist));
}


TEST_CASE(arm_reach_stretches_towards_a_point_out_of_reach) {
  const VrVector3 shoulder = { 0.0f, 0.0f, 0.0f };
  const VrVector3 elbow    = { 0.0f, 0.0f, -3.0f };
  const VrVector3 wrist    = { 0.0f, 2.0f, -3.0f };

  VrArmReach reach = vrArmReach(shoulder, elbow, wrist, VrVector3 { 0.0f, 10.0f, 0.0f });
  VrVector3 newWrist = vrCaravanTransformPoint(reach.fore, vrCaravanTransformPoint(reach.upper, wrist));

  CHECK(newWrist.y > 4.9f);
  CHECK(std::abs(newWrist.x) < 1e-2f && std::abs(newWrist.z) < 0.2f);
}


TEST_CASE(caravan_blend_goes_from_one_transform_to_the_other) {
  VrCaravanTransform a;
  VrCaravanTransform b;

  // b: a quarter turn about z, elsewhere, twice as big
  b.rotate[0][0] = 0.0f; b.rotate[0][1] = -1.0f;
  b.rotate[1][0] = 1.0f; b.rotate[1][1] =  0.0f;
  b.translate = { 4.0f, 0.0f, 2.0f };
  b.scale     = 2.0f;

  VrCaravanTransform start = vrCaravanBlend(a, b, 0.0f);
  VrCaravanTransform end   = vrCaravanBlend(a, b, 1.0f);
  VrCaravanTransform half  = vrCaravanBlend(a, b, 0.5f);

  CHECK(near(vrCaravanTransformPoint(start, VrVector3 { 1.0f, 0.0f, 0.0f }), VrVector3 { 1.0f, 0.0f, 0.0f }));
  CHECK(near(vrCaravanTransformPoint(end, VrVector3 { 1.0f, 0.0f, 0.0f }), VrVector3 { 4.0f, 2.0f, 2.0f }));

  // Half way: an eighth of a turn, 1.5 times as big
  float r = 1.5f * std::sqrt(0.5f);
  CHECK(near(vrCaravanTransformPoint(half, VrVector3 { 1.0f, 0.0f, 0.0f }), VrVector3 { 2.0f + r, r, 1.0f }));
}


TEST_CASE(caravan_reach_takes_the_card_then_comes_back) {
  CHECK(vrCaravanReachStep(-1.0f).done);

  VrCaravanReachStep start = vrCaravanReachStep(0.0f);
  CHECK(!start.done && !start.taken && start.weight == 0.0f);

  VrCaravanReachStep atFan = vrCaravanReachStep(VrCaravanReachTime + 1e-4f);
  CHECK(atFan.taken && atFan.weight == 1.0f && atFan.carry < 0.01f);

  VrCaravanReachStep atMat = vrCaravanReachStep(VrCaravanReachTime + VrCaravanCarryTime + 1e-4f);
  CHECK(atMat.carry == 1.0f && atMat.weight > 0.99f);

  float total = VrCaravanReachTime + VrCaravanCarryTime + VrCaravanReturnTime;
  CHECK(!vrCaravanReachStep(total - 0.01f).done);
  CHECK(vrCaravanReachStep(total + 0.01f).done);
}


TEST_CASE(player_carry_goes_from_the_hand_to_the_row_in_time) {
  CHECK(vrCaravanPlayerCarry(0.0f) == 0.0f);
  CHECK(std::abs(vrCaravanPlayerCarry(0.5f * VrCaravanPlayerCarryTime) - 0.5f) < 1e-5f);
  CHECK(vrCaravanPlayerCarry(0.25f) < vrCaravanPlayerCarry(0.5f));
  CHECK(vrCaravanPlayerCarry(VrCaravanPlayerCarryTime) == 1.0f);
  CHECK(vrCaravanPlayerCarry(5.0f) == 1.0f);
  // Within the game's shortest play animation, so the card rests on its
  // row before the game moves on
  CHECK(VrCaravanPlayerCarryTime < 0.97f);
}
