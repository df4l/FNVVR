#include "test.h"

#include "../vr_caravan_pick.h"

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

TEST_CASE(caravan_row_models_by_name) {
  CHECK(vrIsCaravanRowModel("Player_1_01"));
  CHECK(vrIsCaravanRowModel("Opponent_3_07"));
  CHECK(!vrIsCaravanRowModel("Player_4_01"));
  CHECK(!vrIsCaravanRowModel("Player_1_08"));
  CHECK(!vrIsCaravanRowModel("Player_1_01 NonAccum"));
  CHECK(!vrIsCaravanRowModel("Player_Deck"));
  CHECK(!vrIsCaravanRowModel("Player_4_Shadow"));
}
