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

TEST_CASE(caravan_opponent_rows_fill_from_the_right) {
  CHECK(vrCaravanColumnNode(0, 0) == 1);
  CHECK(vrCaravanColumnNode(2, 3) == 4);
  CHECK(vrCaravanColumnNode(3, 0) == 4);
  CHECK(vrCaravanColumnNode(5, 1) == 3);
  CHECK(vrCaravanColumnNode(4, 3) == 1);
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


TEST_CASE(caravan_fan_goes_to_the_hand_faces_along_their_direction) {
  const VrVector3 centre = { 18.0f, -9.7f, 1.7f };
  const VrVector3 hold   = { -5.0f, -30.0f, 20.0f };
  const VrVector3 faces  = { 0.0f, -3.0f, 1.0f };

  // Tips up, the faces towards -y and a little up, which the tips flatten
  VrCaravanTransform fan = vrCaravanFanInHand(centre, hold, VrVector3 { 0.0f, 0.0f, 2.0f }, faces, 1.5f);

  VrVector3 c = vrCaravanTransformPoint(fan, centre);
  CHECK_NEAR(c.x, hold.x, 1e-4f);
  CHECK_NEAR(c.y, hold.y, 1e-4f);
  CHECK_NEAR(c.z, hold.z, 1e-4f);

  // The tips (+y) go up, the faces (+z) to -y, the first card (-x) on
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
    VrVector3 { 0.0f, 0.0f, 1.0f }, VrVector3 { 0.0f, -1.0f, 0.0f }, 2.0f);

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


TEST_CASE(caravan_deck_picker_scrolls_then_adds) {
  VrCaravanDeckPicker picker;

  // Three slots right of the middle: three steps right, then A
  CHECK(picker.click(VrCaravanDeckRow::Available, 14, 20, 54));
  CHECK(picker.target() == 23);

  VrCaravanDeckAction action = picker.update(20, false, true, 0.01f);
  CHECK(action.stick == 1);
  CHECK(!action.add && !action.remove);

  CHECK(picker.update(22, false, false, 0.01f).stick == 1);

  // At the card, the last animation still runs: nothing yet
  action = picker.update(23, false, false, 0.01f);
  CHECK(action.stick == 0);
  CHECK(!action.add);

  action = picker.update(23, false, true, 0.01f);
  CHECK(action.add);
  CHECK(!action.remove);
  CHECK(picker.target() == -1);
  CHECK(picker.update(23, false, true, 0.01f).stick == 0);
}


TEST_CASE(caravan_deck_picker_removes_only_what_is_in_the_deck) {
  VrCaravanDeckPicker picker;

  // The middle card of the deck's row: X at once
  CHECK(picker.click(VrCaravanDeckRow::Deck, VrCaravanDeckPicker::CurrentSlot, 5, 54));
  VrCaravanDeckAction action = picker.update(5, true, true, 0.01f);
  CHECK(action.remove);
  CHECK(!action.add);

  // A card already out of the deck is only brought to the middle
  CHECK(picker.click(VrCaravanDeckRow::Deck, 9, 5, 54));
  CHECK(picker.update(5, false, true, 0.01f).stick == -1);
  action = picker.update(3, false, true, 0.01f);
  CHECK(!action.add && !action.remove);
  CHECK(picker.target() == -1);

  // Nor is a card already in it added again
  CHECK(picker.click(VrCaravanDeckRow::Available, VrCaravanDeckPicker::CurrentSlot, 3, 54));
  action = picker.update(3, true, true, 0.01f);
  CHECK(!action.add && !action.remove);
}


TEST_CASE(caravan_deck_picker_ignores_empty_slots_and_stalls) {
  VrCaravanDeckPicker picker;

  // Before the first card and past the last one
  CHECK(!picker.click(VrCaravanDeckRow::Available, 2, 3, 54));
  CHECK(!picker.click(VrCaravanDeckRow::Available, 20, 50, 54));
  CHECK(!picker.click(VrCaravanDeckRow::Available, VrCaravanDeckPicker::Slots, 20, 54));
  CHECK(picker.target() == -1);

  // A meter that does not move is given up
  CHECK(picker.click(VrCaravanDeckRow::Available, 12, 3, 54));
  CHECK(picker.update(3, false, true, 1.5f).stick == 1);
  CHECK(picker.update(3, false, true, 1.0f).stick == 0);
  CHECK(picker.target() == -1);
}


TEST_CASE(caravan_inverse_undoes_a_transform) {
  VrCaravanTransform t;
  t.rotate[0][0] = 0.0f; t.rotate[0][1] = -1.0f;
  t.rotate[1][0] = 1.0f; t.rotate[1][1] =  0.0f;
  t.translate = { 3.0f, -2.0f, 5.0f };
  t.scale     = 2.0f;

  VrVector3 p = { 1.0f, 2.0f, 3.0f };
  VrVector3 back = vrCaravanTransformPoint(vrCaravanInverse(t), vrCaravanTransformPoint(t, p));

  CHECK(std::abs(back.x - p.x) < 1e-4f);
  CHECK(std::abs(back.y - p.y) < 1e-4f);
  CHECK(std::abs(back.z - p.z) < 1e-4f);
}


TEST_CASE(caravan_opponent_cards_fan_out_backs_to_the_player) {
  // Held upright, backs towards -Y (the player's side of the mat)
  const VrVector3 hold = { 10.0f, 20.0f, 30.0f };
  const VrVector3 up   = { 0.0f, 0.0f, 1.0f };
  const VrVector3 face = { 0.0f, -1.0f, 0.0f };

  VrCaravanTransform middle = vrCaravanOpponentCard(hold, up, face, 1, 3, 0.2f, 1.5f, 0.05f, 1.0f);

  // Long axis up, back towards the player, centre above the hold point
  CHECK(std::abs(middle.rotate[2][0] - 1.0f) < 1e-4f);
  CHECK(std::abs(middle.rotate[1][2] + 1.0f) < 1e-4f);
  CHECK(std::abs(middle.translate.x - 10.0f) < 1e-4f);
  CHECK(std::abs(middle.translate.z - 31.5f) < 1e-4f);
  CHECK(std::abs(middle.translate.y - (20.0f - 0.05f)) < 1e-4f);

  // The first and last cards lean to opposite sides by the spread
  VrCaravanTransform first = vrCaravanOpponentCard(hold, up, face, 0, 3, 0.2f, 1.5f, 0.05f, 1.0f);
  VrCaravanTransform last  = vrCaravanOpponentCard(hold, up, face, 2, 3, 0.2f, 1.5f, 0.05f, 1.0f);

  CHECK(std::abs(first.rotate[0][0] + last.rotate[0][0]) < 1e-4f);
  CHECK(std::abs(std::abs(first.rotate[0][0]) - std::sin(0.2f)) < 1e-4f);
  CHECK(std::abs(first.rotate[2][0] - std::cos(0.2f)) < 1e-4f);

  // Each card a layer nearer the player, the axes right-handed
  CHECK(last.translate.y < middle.translate.y && middle.translate.y < first.translate.y);

  const auto& r = last.rotate;
  float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
            - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
            + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
  CHECK(std::abs(det - 1.0f) < 1e-4f);
}
