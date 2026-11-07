#include "test.h"

#include "../vr_head_hud.h"

using namespace dxvk;

namespace {

  // Headset resolution, 2 pixels per interface unit
  const VrVector2 Image = { 2560.0f, 1920.0f };
  const VrVector2 Interface = { 1280.0f, 960.0f };

  bool overlaps(const VrInterfaceRect& a, const VrInterfaceRect& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
  }

}


TEST_CASE(xp_meter_crop_reaches_the_right_side) {
  // The game's position without safe zone, moved to the messages' row
  VrInterfaceRect crop = vrComputeXpMeterCrop({ 890.0f, 25.0f, 890.0f, 25.0f }, Interface);

  CHECK_NEAR(crop.left, 878.0f, 1e-3f);
  CHECK_NEAR(crop.top, 25.0f, 1e-3f);
  CHECK_NEAR(crop.right, 1280.0f, 1e-3f);
  CHECK_NEAR(crop.bottom, 281.0f, 1e-3f);
}


TEST_CASE(xp_meter_crop_stays_on_screen) {
  VrInterfaceRect crop = vrComputeXpMeterCrop({ 5.0f, 800.0f, 5.0f, 800.0f }, Interface);

  CHECK_NEAR(crop.left, 0.0f, 1e-3f);
  CHECK_NEAR(crop.bottom, 960.0f, 1e-3f);
}


TEST_CASE(head_hud_without_xp_meter_shifts_the_upper_half) {
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 400.0f, nullptr, 0.0f);
  CHECK(layout.count == 2);

  // Lower half unchanged
  CHECK_NEAR(layout.copies[0].source.top, 960.0f, 1e-3f);
  CHECK_NEAR(layout.copies[0].target.left, 0.0f, 1e-3f);
  CHECK_NEAR(layout.copies[0].target.right, 2560.0f, 1e-3f);

  // Upper half moved right, cut at the image's side
  CHECK_NEAR(layout.copies[1].source.right, 2160.0f, 1e-3f);
  CHECK_NEAR(layout.copies[1].target.left, 400.0f, 1e-3f);
  CHECK_NEAR(layout.copies[1].target.right, 2560.0f, 1e-3f);
}


TEST_CASE(head_hud_moves_the_xp_meter_towards_the_centre) {
  const VrInterfaceRect xp = { 1756.0f, 50.0f, 2560.0f, 562.0f };
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 400.0f, &xp, 0.0f);
  CHECK(layout.count == 3);

  // The upper half stops before the meter, so the meter is copied once
  CHECK_NEAR(layout.copies[1].source.right, 1756.0f, 1e-3f);
  CHECK_NEAR(layout.copies[1].target.right, 2156.0f, 1e-3f);

  const VrHudCopy& meter = layout.copies[2];
  CHECK_NEAR(meter.source.left, 1756.0f, 1e-3f);
  CHECK_NEAR(meter.target.left, 1356.0f, 1e-3f);
  CHECK_NEAR(meter.target.right, 2160.0f, 1e-3f);
  CHECK_NEAR(meter.target.top, 50.0f, 1e-3f);
  CHECK_NEAR(meter.target.bottom, 562.0f, 1e-3f);

  // The meter is copied last, over the empty right part of the upper half,
  // and leaves the lower half's subtitles alone
  CHECK(!overlaps(meter.target, layout.copies[0].target));
}


TEST_CASE(head_hud_keeps_the_xp_meter_in_the_upper_half) {
  const VrInterfaceRect xp = { 1756.0f, 800.0f, 2560.0f, 1312.0f };
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 400.0f, &xp, 0.0f);
  CHECK(layout.count == 3);
  CHECK_NEAR(layout.copies[2].source.bottom, 960.0f, 1e-3f);
}


TEST_CASE(head_hud_corner_right_covers_messages_and_objectives) {
  // The game's positions with a safe zone of 15 units
  const VrInterfaceRect messages = { 25.0f, 55.0f, 485.0f, 145.0f };
  const VrInterfaceRect objectives = { 30.0f, 150.0f, 430.0f, 350.0f };

  CHECK_NEAR(vrComputeHudCornerRight(&messages, &objectives), 495.0f, 1e-3f);
  CHECK_NEAR(vrComputeHudCornerRight(nullptr, &objectives), 440.0f, 1e-3f);
  CHECK_NEAR(vrComputeHudCornerRight(nullptr, nullptr), 0.0f, 1e-3f);
}


TEST_CASE(head_hud_meters_stop_before_long_messages) {
  // Safe zone of 15 units: the messages from 25 to 485 units, the meters'
  // column from 848 units, in the messages' row; the default offset of
  // 0.25 m on a 1.5 m panel moves the corners by 427 pixels
  const VrInterfaceRect meters = { 1696.0f, 110.0f, 2560.0f, 862.0f };
  const VrInterfaceRect messages = { 25.0f, 55.0f, 485.0f, 145.0f };

  float cornerRight = 2.0f * vrComputeHudCornerRight(&messages, nullptr);
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 427.0f, &meters, cornerRight);
  CHECK(layout.count == 3);

  // The messages keep the whole shift
  CHECK_NEAR(layout.copies[1].target.left, 427.0f, 1e-3f);

  VrInterfaceRect movedMessages = {
    2.0f * messages.left  + 427.0f, 2.0f * messages.top,
    2.0f * messages.right + 427.0f, 2.0f * messages.bottom };

  // The meters move less, and their copy leaves the messages alone
  const VrHudCopy& column = layout.copies[2];
  CHECK_NEAR(column.target.left, 970.0f + 20.0f + 427.0f, 1e-3f);
  CHECK(column.target.left < meters.left);
  CHECK(!overlaps(column.target, movedMessages));
}


TEST_CASE(head_hud_meters_keep_the_shift_with_room) {
  const VrInterfaceRect meters = { 1696.0f, 110.0f, 2560.0f, 862.0f };
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 400.0f, &meters, 600.0f);
  CHECK_NEAR(layout.copies[2].target.left, 1296.0f, 1e-3f);
}


TEST_CASE(head_hud_meters_stay_with_no_room) {
  const VrInterfaceRect meters = { 1696.0f, 110.0f, 2560.0f, 862.0f };
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 400.0f, &meters, 1500.0f);
  CHECK_NEAR(layout.copies[2].target.left, 1696.0f, 1e-3f);
  CHECK_NEAR(layout.copies[1].target.left, 196.0f, 1e-3f);
}


TEST_CASE(head_hud_keeps_a_long_quest_name_whole) {
  // A quest's name on one line from 30 to 760 units, past where the
  // meters' column starts once the corner moved by 427 pixels
  const VrInterfaceRect meters = { 1696.0f, 110.0f, 2560.0f, 862.0f };
  const VrInterfaceRect objectives = { 30.0f, 150.0f, 760.0f, 350.0f };

  float cornerRight = 2.0f * vrComputeHudCornerRight(nullptr, &objectives);
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 427.0f, &meters, cornerRight);
  CHECK(layout.count == 3);

  // The meters stay, the corner moves only as far as the room allows
  const VrHudCopy& upper  = layout.copies[1];
  const VrHudCopy& column = layout.copies[2];
  CHECK_NEAR(column.target.left, 1696.0f, 1e-3f);
  CHECK_NEAR(upper.target.left, 1696.0f - 1540.0f, 1e-3f);

  VrInterfaceRect movedName = {
    2.0f * objectives.left  + upper.target.left, 2.0f * objectives.top,
    2.0f * objectives.right + upper.target.left, 2.0f * objectives.bottom };

  CHECK(movedName.right <= upper.target.right);
  CHECK(!overlaps(column.target, movedName));
}


TEST_CASE(head_hud_without_meters_keeps_the_shift_for_a_long_name) {
  const VrInterfaceRect objectives = { 30.0f, 150.0f, 760.0f, 350.0f };

  float cornerRight = 2.0f * vrComputeHudCornerRight(nullptr, &objectives);
  VrHeadHudLayout layout = vrComputeHeadHudLayout(Image, 427.0f, nullptr, cornerRight);
  CHECK(layout.count == 2);
  CHECK_NEAR(layout.copies[1].target.left, 427.0f, 1e-3f);
  CHECK_NEAR(layout.copies[1].target.right, 2560.0f, 1e-3f);
}


TEST_CASE(radiation_meter_crop_reaches_the_right_side) {
  // The game's position without safe zone, moved to the messages' row
  VrInterfaceRect crop = vrComputeRadiationMeterCrop({ 873.0f, 25.0f, 1240.0f, 85.0f }, Interface);

  CHECK_NEAR(crop.left, 873.0f, 1e-3f);
  CHECK_NEAR(crop.top, 25.0f, 1e-3f);
  CHECK_NEAR(crop.right, 1280.0f, 1e-3f);
  CHECK_NEAR(crop.bottom, 145.0f, 1e-3f);
}


TEST_CASE(hud_meters_crop_covers_both_meters) {
  // The radiation meter in the messages' row, the XP meter below it
  const VrInterfaceRect radiation = { 873.0f, 25.0f, 1240.0f, 85.0f };
  const VrInterfaceRect xp = { 890.0f, 145.0f, 890.0f, 145.0f };

  VrInterfaceRect crop;
  CHECK(vrComputeHudMetersCrop(&radiation, &xp, Interface, crop));
  CHECK_NEAR(crop.left, 873.0f, 1e-3f);
  CHECK_NEAR(crop.top, 25.0f, 1e-3f);
  CHECK_NEAR(crop.right, 1280.0f, 1e-3f);
  CHECK_NEAR(crop.bottom, 401.0f, 1e-3f);

  // The XP meter starts where the radiation meter ends, so they never overlap
  VrInterfaceRect radiationCrop = vrComputeRadiationMeterCrop(radiation, Interface);
  VrInterfaceRect xpCrop = vrComputeXpMeterCrop(xp, Interface);
  CHECK(!overlaps(radiationCrop, xpCrop));

  // Still within the upper half, which the head panel moves
  CHECK(crop.bottom <= 0.5f * Interface.y);
}


TEST_CASE(hud_meters_crop_with_one_meter) {
  const VrInterfaceRect xp = { 890.0f, 25.0f, 890.0f, 25.0f };

  VrInterfaceRect crop;
  CHECK(vrComputeHudMetersCrop(nullptr, &xp, Interface, crop));
  CHECK_NEAR(crop.left, 878.0f, 1e-3f);
  CHECK_NEAR(crop.bottom, 281.0f, 1e-3f);

  CHECK(!vrComputeHudMetersCrop(nullptr, nullptr, Interface, crop));
}
