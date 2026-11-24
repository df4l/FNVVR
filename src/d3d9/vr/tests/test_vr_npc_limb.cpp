#include <array>
#include <cmath>

#include "test.h"

#include "../vr_npc_limb.h"

using namespace dxvk;

namespace {

  // The human ragdoll (findings/grab.md §19.5), legs left out: the root and
  // the two lower spine bodies follow the animation
  enum Body : uint32_t { Root, Spine, Spine1, Spine2, Neck, Head, LUpper, LFore, LHand, RUpper, RFore, RHand, Count };

  constexpr uint16_t None = 0xFFFF;

  const std::array<uint16_t, Count> Parents = {
    None, Root, Spine, Spine1, Spine2, Neck, Spine2, LUpper, LFore, Spine2, RUpper, RFore };

  const std::array<bool, Count> Dynamic = {
    false, false, false, true, true, true, true, true, true, true, true, true };

  std::array<float, Count> weightsHolding(std::initializer_list<uint32_t> held) {
    std::array<float, Count> weights;
    weights.fill(1.0f);

    for (uint32_t body : held)
      vrLimbLoosen(Count, Parents.data(), Dynamic.data(), body, weights.data());

    return weights;
  }

}


TEST_CASE(a_hand_held_frees_its_arm_and_softens_the_chest) {
  auto weights = weightsHolding({ LHand });

  CHECK(weights[LHand]  == VrLimbFreeWeight);
  CHECK(weights[LFore]  == VrLimbFreeWeight);
  CHECK(weights[LUpper] == VrLimbFreeWeight);
  CHECK(weights[Spine2] == VrLimbSoftWeight);

  // The rest of the actor keeps its pose
  CHECK(weights[Neck]   == 1.0f);
  CHECK(weights[Head]   == 1.0f);
  CHECK(weights[RUpper] == 1.0f);
  CHECK(weights[RHand]  == 1.0f);
}


TEST_CASE(the_head_held_frees_the_neck) {
  auto weights = weightsHolding({ Head });

  CHECK(weights[Head]   == VrLimbFreeWeight);
  CHECK(weights[Neck]   == VrLimbFreeWeight);
  CHECK(weights[Spine2] == VrLimbSoftWeight);
  CHECK(weights[LUpper] == 1.0f);
}


TEST_CASE(what_hangs_from_the_limb_follows_softly) {
  auto weights = weightsHolding({ LFore });

  CHECK(weights[LFore] == VrLimbFreeWeight);
  CHECK(weights[LHand] == VrLimbSoftWeight);
}


TEST_CASE(bodies_the_animation_places_keep_their_drive) {
  auto weights = weightsHolding({ Spine2 });

  CHECK(weights[Spine2] == VrLimbFreeWeight);
  CHECK(weights[Spine1] == 1.0f);
  CHECK(weights[Spine]  == 1.0f);
  CHECK(weights[Root]   == 1.0f);

  // Nor does a hand on one of them change it
  auto keyframed = weightsHolding({ Spine1 });
  CHECK(keyframed[Spine1] == 1.0f);
  CHECK(keyframed[Spine2] == VrLimbSoftWeight);
}


TEST_CASE(two_hands_add_up_on_a_ragdoll) {
  auto weights = weightsHolding({ LHand, Head });

  CHECK(weights[LHand]  == VrLimbFreeWeight);
  CHECK(weights[Head]   == VrLimbFreeWeight);
  CHECK(weights[Spine2] == VrLimbSoftWeight);

  // The order of the hands does not matter
  CHECK(weightsHolding({ Head, LHand }) == weights);

  // A soft body stays free when the other hand frees it
  auto held = weightsHolding({ Spine2, LHand });
  CHECK(held[Spine2] == VrLimbFreeWeight);
}


TEST_CASE(a_limb_goes_back_to_its_full_drive) {
  CHECK(vrLimbReturnWeight(0.0f, 0.0f) == 0.0f);
  CHECK(vrLimbReturnWeight(0.0f, 0.5f) == 0.5f);
  CHECK(vrLimbReturnWeight(0.5f, 1.0f) == 1.0f);
  CHECK(vrLimbReturnWeight(1.0f, 0.3f) == 1.0f);
}


TEST_CASE(the_limb_spring_keeps_up_with_a_moving_hand) {
  VrGrabSpringConstants object;
  object.damping       = 0.5f;
  object.elasticity    = 0.2f;
  object.objectDamping = 0.9f;

  VrGrabSpringConstants limb = vrLimbSpring(object);

  CHECK(limb.objectDamping == object.objectDamping);
  CHECK(vrGrabSpringLagSteps(limb) < 0.5f * vrGrabSpringLagSteps(object));

  // hkpMouseSpringAction::applyAction, in steps: a hand at constant speed
  float point = 0.0f;
  float velocity = 0.0f;
  float target = 0.0f;
  float overshoot = 0.0f;

  for (uint32_t i = 0; i < 200; i++) {
    target += 1.0f;
    velocity *= limb.objectDamping;
    velocity += -limb.damping * velocity + limb.elasticity * (target - point);
    point += velocity;
    overshoot = std::max(overshoot, point - target);
  }

  // The lag counts from the target's move, before the point's own: one step
  CHECK(std::fabs((target - point + 1.0f) - vrGrabSpringLagSteps(limb)) < 0.01f);
  CHECK(overshoot <= 0.0f);

  // And stops with the hand
  float largest = 0.0f;

  for (uint32_t i = 0; i < 200; i++) {
    velocity *= limb.objectDamping;
    velocity += -limb.damping * velocity + limb.elasticity * (target - point);
    point += velocity;
    largest = std::max(largest, point - target);
  }

  CHECK(std::fabs(target - point) < 0.01f);
  CHECK(largest < 0.25f * vrGrabSpringLagSteps(limb));
}


TEST_CASE(the_spring_carries_the_whole_branch) {
  CHECK(vrLimbMaxAcceleration(750.0f, 15.0f, 3.0f) == 3750.0f);

  // Never weaker than on the limb alone
  CHECK(vrLimbMaxAcceleration(750.0f, 2.0f, 3.0f) == 750.0f);
  CHECK(vrLimbMaxAcceleration(750.0f, 2.0f, 0.0f) == 750.0f);
}
