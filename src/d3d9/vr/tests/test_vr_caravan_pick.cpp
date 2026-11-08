#include <cmath>

#include "test.h"

#include "../vr_caravan_pick.h"
#include "../vr_math.h"

using namespace dxvk;

TEST_CASE(caravan_nearest_point_keeps_the_chosen_one) {
  const VrVector2 cards[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f } };

  CHECK(vrNearestPoint(cards, 3, VrVector2 { 0.9f, 0.1f }, 2.0f, -1, 0.3f) == 1);

  // Just past the middle, the chosen card keeps the laser
  CHECK(vrNearestPoint(cards, 3, VrVector2 { 1.6f, 0.0f }, 2.0f, 1, 0.3f) == 1);
  CHECK(vrNearestPoint(cards, 3, VrVector2 { 1.8f, 0.0f }, 2.0f, 1, 0.3f) == 2);

  // Out of reach of all
  CHECK(vrNearestPoint(cards, 3, VrVector2 { 5.0f, 5.0f }, 2.0f, -1, 0.3f) == -1);
  CHECK(vrNearestPoint(cards, 0, VrVector2 { 0.0f, 0.0f }, 2.0f, -1, 0.3f) == -1);
}


namespace {

  VrCaravanCard flatCard(float x, float z, float angle) {
    // Turned about z, lying face up: the hand's fan
    VrCaravanCard card;
    card.rotate[0][0] = std::cos(angle); card.rotate[0][1] = -std::sin(angle);
    card.rotate[1][0] = std::sin(angle); card.rotate[1][1] =  std::cos(angle);
    card.rotate[2][2] = 1.0f;
    card.translate = { x, 0.0f, z };
    card.halfX = 1.75f;
    card.halfY = 1.25f;
    return card;
  }

}


TEST_CASE(caravan_ray_meets_a_card_in_its_rectangle) {
  VrCaravanCard card = flatCard(0.0f, 0.0f, 0.0f);
  const VrVector3 down = { 0.0f, 0.0f, -1.0f };

  CHECK(std::abs(vrRayMeetsCard(VrVector3 { 1.5f, 1.0f, 10.0f }, down, card) - 10.0f) < 1e-4f);
  CHECK(vrRayMeetsCard(VrVector3 { 1.5f, 1.5f, 10.0f }, down, card) < 0.0f);
  CHECK(vrRayMeetsCard(VrVector3 { 0.0f, 0.0f, -10.0f }, down, card) < 0.0f);

  // Turned a quarter, the long side lies along y
  card = flatCard(0.0f, 0.0f, 1.5707963f);
  CHECK(vrRayMeetsCard(VrVector3 { 0.0f, 1.5f, 10.0f }, down, card) > 0.0f);
  CHECK(vrRayMeetsCard(VrVector3 { 1.5f, 0.0f, 10.0f }, down, card) < 0.0f);

  // Scaled up twice
  card = flatCard(0.0f, 0.0f, 0.0f);
  card.scale = 2.0f;
  CHECK(vrRayMeetsCard(VrVector3 { 3.0f, 0.0f, 10.0f }, down, card) > 0.0f);
}


TEST_CASE(caravan_first_card_on_ray_is_the_one_on_top) {
  // A fan: each card a little higher and further right than the one before
  const VrCaravanCard cards[] = {
    flatCard(0.0f, 0.0f, 0.3f), flatCard(0.5f, 0.1f, 0.4f), flatCard(1.0f, 0.2f, 0.5f) };
  const VrVector3 down = { 0.0f, 0.0f, -1.0f };

  // Over the overlap of all three, the top one
  CHECK(vrFirstCardOnRay(cards, 3, VrVector3 { 0.5f, 0.0f, 10.0f }, down) == 2);
  // On the strip of the first card the others leave uncovered
  CHECK(vrFirstCardOnRay(cards, 3, VrVector3 { -1.5f, -0.4f, 10.0f }, down) == 0);
  // From aside, slanted
  CHECK(vrFirstCardOnRay(cards, 3, VrVector3 { -10.5f, 0.0f, 10.0f }, VrVector3 { 1.0f, 0.0f, -1.0f }) == 2);
  CHECK(vrFirstCardOnRay(cards, 3, VrVector3 { 10.0f, 10.0f, 10.0f }, down) == -1);
  CHECK(vrFirstCardOnRay(cards, 0, VrVector3 { 0.0f, 0.0f, 10.0f }, down) == -1);
}


TEST_CASE(caravan_hand_step_starts_one_card_before) {
  VrCaravanStep step = vrCaravanHandStep(3, 0, 5);
  CHECK(step.key == VrCaravanKey::Right);
  CHECK(step.selection == 2);

  // The first card comes from the last one, the menu wraps
  step = vrCaravanHandStep(0, 2, 5);
  CHECK(step.key == VrCaravanKey::Right);
  CHECK(step.selection == 4);

  CHECK(vrCaravanHandStep(2, 2, 5).key == VrCaravanKey::None);
  CHECK(vrCaravanHandStep(5, 2, 5).key == VrCaravanKey::None);
  CHECK(vrCaravanHandStep(0, 0, 0).key == VrCaravanKey::None);
}


TEST_CASE(caravan_slot_step_lands_on_the_row) {
  // The player's rows count down with up, the opponent's up
  VrCaravanStep step = vrCaravanSlotStep(1, 2);
  CHECK(step.key == VrCaravanKey::Up);
  CHECK(step.selection == 3);

  step = vrCaravanSlotStep(4, 0);
  CHECK(step.key == VrCaravanKey::Up);
  CHECK(step.selection == -1);

  step = vrCaravanSlotStep(1, 6);
  CHECK(step.selection == 7);
}


TEST_CASE(caravan_track_key_moves_towards_the_track) {
  CHECK(vrCaravanTrackKey(2, 0) == VrCaravanKey::Right);
  CHECK(vrCaravanTrackKey(0, 1) == VrCaravanKey::Left);
  CHECK(vrCaravanTrackKey(1, 1) == VrCaravanKey::None);
}

TEST_CASE(caravan_row_cards_past_the_row_wait_out_of_the_game) {
  VrCaravanPlacing none;
  CHECK(vrCaravanCardInGame(4, 2, 0, 2, none));
  CHECK(vrCaravanCardInGame(4, 2, 1, 2, none));
  CHECK(!vrCaravanCardInGame(4, 2, 2, 2, none));
  CHECK(!vrCaravanCardInGame(4, 3, 0, 0, none));

  // The card on its way to the row shows, not the others of its model
  VrCaravanPlacing placing;
  placing.track = 4; placing.row = 2; placing.column = 2;
  CHECK(vrCaravanCardInGame(4, 2, 2, 2, placing));
  CHECK(!vrCaravanCardInGame(4, 2, 3, 2, placing));
  CHECK(!vrCaravanCardInGame(4, 1, 2, 2, placing));
  CHECK(!vrCaravanCardInGame(1, 2, 2, 2, placing));
}

TEST_CASE(caravan_back_discards_a_card_on_a_tap_and_a_caravan_on_a_hold) {
  using C = VrCaravanCommand;
  VrCaravanButtons buttons;

  CHECK(buttons.update(true,  false, false, 0.1f) == C::None);
  CHECK(buttons.update(true,  false, false, 0.1f) == C::None);
  CHECK(buttons.update(false, false, false, 0.1f) == C::DiscardCard);
  CHECK(buttons.update(false, false, false, 0.1f) == C::None);

  CHECK(buttons.update(true,  false, false, 0.1f) == C::None);
  CHECK(buttons.update(true,  false, false, 0.7f) == C::DiscardTrack);
  // The menu chooses a caravan now: the same press gives nothing more
  CHECK(buttons.update(true,  false, true,  0.7f) == C::None);
  CHECK(buttons.update(false, false, true,  0.1f) == C::None);
}

TEST_CASE(caravan_back_and_option_cancel_while_a_slot_is_chosen) {
  using C = VrCaravanCommand;
  VrCaravanButtons buttons;

  CHECK(buttons.update(true,  false, true, 0.1f) == C::Cancel);
  CHECK(buttons.update(true,  false, true, 1.0f) == C::None);
  // The card went back to the hand while B was held: no discard on release
  CHECK(buttons.update(false, false, false, 0.1f) == C::None);

  CHECK(buttons.update(false, true,  true,  0.1f) == C::Cancel);
  CHECK(buttons.update(false, true,  true,  0.1f) == C::None);
  CHECK(buttons.update(false, false, false, 0.1f) == C::None);
}


TEST_CASE(caravan_fan_goes_to_the_hand_faces_to_the_eye) {
  const VrVector3 centre = { 18.0f, -9.7f, 1.7f };
  const VrVector3 hold   = { -5.0f, -30.0f, 20.0f };
  const VrVector3 eye    = { -5.0f, -60.0f, 20.0f };

  // Tips up, the eye straight behind the fan on -y
  VrCaravanTransform fan = vrCaravanFanInHand(centre, hold, VrVector3 { 0.0f, 0.0f, 2.0f }, eye, 1.5f);

  VrVector3 c = vrCaravanTransformPoint(fan, centre);
  CHECK_NEAR(c.x, hold.x, 1e-4f);
  CHECK_NEAR(c.y, hold.y, 1e-4f);
  CHECK_NEAR(c.z, hold.z, 1e-4f);

  // The tips (+y) go up, the faces (+z) to the eye, the first card (-x) on
  // the left of someone looking along +y
  VrVector3 tip  = vrCaravanTransformPoint(fan, VrVector3 { centre.x, centre.y + 1.0f, centre.z }) - hold;
  VrVector3 face = vrCaravanTransformPoint(fan, VrVector3 { centre.x, centre.y, centre.z + 1.0f }) - hold;
  VrVector3 left = vrCaravanTransformPoint(fan, VrVector3 { centre.x - 1.0f, centre.y, centre.z }) - hold;
  CHECK_NEAR(tip.z,   1.5f, 1e-4f);
  CHECK_NEAR(face.y, -1.5f, 1e-4f);
  CHECK_NEAR(left.x, -1.5f, 1e-4f);
}


TEST_CASE(caravan_fan_moves_a_card_with_its_rotation) {
  VrCaravanTransform fan = vrCaravanFanInHand(VrVector3 { 0.0f, 0.0f, 0.0f }, VrVector3 { 1.0f, 2.0f, 3.0f },
    VrVector3 { 0.0f, 0.0f, 1.0f }, VrVector3 { 1.0f, -10.0f, 3.0f }, 2.0f);

  VrCaravanTransform card;
  card.translate = { 0.5f, 0.0f, 0.0f };
  card.scale     = 0.5f;
  card.rotate[0][0] = 0.0f; card.rotate[0][1] = -1.0f;
  card.rotate[1][0] = 1.0f; card.rotate[1][1] =  0.0f;

  // A point of the card, moved with the card or after it, lands in the same place
  VrVector3 point = { 1.0f, 0.5f, 0.25f };
  VrVector3 a = vrCaravanTransformPoint(vrCaravanCompose(fan, card), point);
  VrVector3 b = vrCaravanTransformPoint(fan, vrCaravanTransformPoint(card, point));
  CHECK_NEAR(a.x, b.x, 1e-4f);
  CHECK_NEAR(a.y, b.y, 1e-4f);
  CHECK_NEAR(a.z, b.z, 1e-4f);
  CHECK_NEAR(vrCaravanCompose(fan, card).scale, 1.0f, 1e-6f);
}


namespace {

  VrCaravanTransform cardAlong(float x, float y) {
    // A card lying face up, its long axis (+X) along (x, y)
    VrCaravanTransform card;
    card.rotate[0][0] = x; card.rotate[0][1] = -y;
    card.rotate[1][0] = y; card.rotate[1][1] =  x;
    return card;
  }

}


TEST_CASE(caravan_spread_turns_cards_away_from_the_middle) {
  const VrVector3 pivot = { 18.0f, -13.0f, 1.0f };

  // The middle card stays where the game put it
  VrCaravanTransform middle = vrCaravanSpreadCard(cardAlong(0.0f, 1.0f), pivot, 0.0f, 1.5f);
  VrVector3 point = vrCaravanTransformPoint(middle, VrVector3 { 17.0f, -9.0f, 2.0f });
  CHECK_NEAR(point.x, 17.0f, 1e-4f);
  CHECK_NEAR(point.y, -9.0f, 1e-4f);
  CHECK_NEAR(point.z,  2.0f, 1e-4f);

  // A card leaning 30 degrees left leans 45 degrees left, around the pivot
  float lean = 0.5235988f;
  VrCaravanTransform card   = cardAlong(-std::sin(lean), std::cos(lean));
  VrCaravanTransform spread = vrCaravanSpreadCard(card, pivot, 0.0f, 1.5f);
  VrCaravanTransform opened = vrCaravanCompose(spread, card);
  CHECK_NEAR(std::atan2(-opened.rotate[0][0], opened.rotate[1][0]), 0.7853982f, 1e-4f);

  VrVector3 still = vrCaravanTransformPoint(spread, pivot);
  CHECK_NEAR(still.x, pivot.x, 1e-4f);
  CHECK_NEAR(still.y, pivot.y, 1e-4f);

  // The game's fan as it is
  VrCaravanTransform same = vrCaravanSpreadCard(card, pivot, 0.0f, 1.0f);
  CHECK_NEAR(same.rotate[0][0], 1.0f, 1e-6f);
  CHECK_NEAR(same.translate.x, 0.0f, 1e-4f);
}
