#include <cmath>
#include <vector>

#include "test.h"

#include "../vr_magazine.h"

using namespace dxvk;

namespace {

  constexpr float M = VrGameUnitsPerMetre;

  // Head at 1.7 m, looking north
  VrGameCameraPose northHead() {
    VrGameCameraPose pose;
    pose.forward  = { 0.0f, 1.0f, 0.0f };
    pose.up       = { 0.0f, 0.0f, 1.0f };
    pose.right    = { 1.0f, 0.0f, 0.0f };
    pose.position = { 0.0f, 0.0f, 1.7f * M };
    return pose;
  }

  // Over the left shoulder: 15 cm behind the head, 15 cm to its left
  const VrVector3 Pouch = { -0.15f * M, -0.15f * M, 1.6f * M };

  // Magazine well in front of the body: the magazine comes out downwards
  const VrVector3 Well = { 0.0f, 0.4f * M, 1.2f * M };
  const VrVector3 Down = { 0.0f, 0.0f, -1.0f };

  // The magazine is held at the hand
  VrMagazineInput input(bool grip, const VrVector3& hand) {
    VrMagazineInput in;
    in.active     = true;
    in.grip       = grip;
    in.hand       = hand;
    in.held       = hand;
    in.well       = Well;
    in.axis       = Down;
    in.head       = northHead();
    in.roundsHeld = true;
    return in;
  }

  // A point beside the well, or below it along the way out
  VrVector3 nearWell(float metres) {
    return { Well.x + metres * M, Well.y, Well.z };
  }

  VrVector3 belowWell(float metres) {
    return { Well.x, Well.y, Well.z - metres * M };
  }

  // Rack part above the well, pulled back towards the player (south)
  const VrVector3 Rack = { 0.0f, 0.45f * M, 1.3f * M };
  const VrVector3 Back = { 0.0f, -1.0f, 0.0f };
  constexpr float Travel = 0.04f;

  VrRackInput rackInput(bool grip, const VrVector3& hand, float rest = 0.0f) {
    VrRackInput in;
    in.active     = true;
    in.grip       = grip;
    in.rest       = rest;
    in.hand       = hand;
    in.rack       = Rack;
    in.axis       = Back;
    in.travel     = Travel;
    return in;
  }

  // A point pulled back from the rack part by some metres, or beside it
  VrVector3 behindRack(float metres) {
    return { Rack.x, Rack.y - metres * M, Rack.z };
  }

  VrVector3 besideRack(float metres) {
    return { Rack.x + metres * M, Rack.y, Rack.z };
  }

  // Takes the magazine out and lets go of it
  void emptyWeapon(VrMagazineHand& magazine) {
    magazine.update(input(true, nearWell(0.0f)));
    magazine.update(input(true, belowWell(0.1f)));
    magazine.update(input(false, belowWell(0.3f)));
  }

}


TEST_CASE(magazine_parts_are_found_by_keyword) {
  CHECK(vrClassifyWeaponPart("##Clip") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##nmClip") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##SMGClip:0") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##Magazine") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##MicroFusionCell") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##PPEnergyCell") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##PLRBattery") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##CSDrum") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##MinigunAmmo:1") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##SMMG_Ammobox") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##RGCanister") == VrWeaponPartKind::Magazine);
  CHECK(vrClassifyWeaponPart("##TCECCell") == VrWeaponPartKind::Magazine);
}


TEST_CASE(parts_next_to_the_magazine_are_not_magazines) {
  CHECK(vrClassifyWeaponPart("##LR_CellRelease") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##LR_CellReleaseLatch") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##MagRelease") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##ClipPeg") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##MBatteryBay") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##PowerCellAssembly") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##MoonClip") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##SRClipHigh") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##TCClip") == VrWeaponPartKind::None);
}


TEST_CASE(the_lock_over_a_box_is_a_lock) {
  CHECK(vrClassifyWeaponPart("##MinigunAmmoLock") == VrWeaponPartKind::Lock);
  CHECK(vrClassifyWeaponPart("##MinigunAmmoLock:0") == VrWeaponPartKind::Lock);
}


TEST_CASE(a_launchers_round_is_a_projectile) {
  CHECK(vrClassifyWeaponPart("##NifRound") == VrWeaponPartKind::Projectile);
  CHECK(vrClassifyWeaponPart("##NifRound:0") == VrWeaponPartKind::Projectile);
  CHECK(!vrIsCartridgePart("##NifRound"));
}


TEST_CASE(other_parts_are_not_magazines) {
  CHECK(vrClassifyWeaponPart("##Cover") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##Cylinder") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("Clip") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("ProjectileNode") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart(nullptr) == VrWeaponPartKind::None);
}


TEST_CASE(pouch_is_behind_the_left_shoulder) {
  VrMagazinePouch pouch;
  VrGameCameraPose head = northHead();

  CHECK(vrIsAtMagazinePouch(head, Pouch, pouch));
  // In front of the head
  CHECK(!vrIsAtMagazinePouch(head, { -0.15f * M, 0.2f * M, 1.6f * M }, pouch));
  // Behind the right shoulder
  CHECK(!vrIsAtMagazinePouch(head, { 0.2f * M, -0.15f * M, 1.6f * M }, pouch));
  // At the hip, behind the back
  CHECK(!vrIsAtMagazinePouch(head, { -0.15f * M, -0.15f * M, 1.0f * M }, pouch));
  // Out of reach
  CHECK(!vrIsAtMagazinePouch(head, { -0.5f * M, -0.5f * M, 1.7f * M }, pouch));
}


TEST_CASE(pouch_follows_the_heading) {
  VrMagazinePouch pouch;
  VrGameCameraPose head = northHead();

  // Facing east, behind is west and left is north
  head.forward = { 1.0f, 0.0f, 0.0f };
  head.right   = { 0.0f, -1.0f, 0.0f };

  CHECK(vrIsAtMagazinePouch(head, { -0.15f * M, 0.15f * M, 1.6f * M }, pouch));
  CHECK(!vrIsAtMagazinePouch(head, Pouch, pouch));
}


TEST_CASE(pouch_works_looking_down) {
  VrMagazinePouch pouch;
  VrGameCameraPose head = northHead();

  // Looking straight down: the top of the head faces north
  head.forward = { 0.0f, 0.0f, -1.0f };
  head.up      = { 0.0f, 1.0f, 0.0f };

  CHECK(vrIsAtMagazinePouch(head, Pouch, pouch));
}


TEST_CASE(magazine_is_ejected_near_the_well) {
  VrMagazineHand magazine;

  // Too far, then a press near the well
  CHECK(magazine.update(input(true, nearWell(0.3f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(false, nearWell(0.05f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, nearWell(0.05f))) == VrMagazineEvent::Ejected);
  CHECK(magazine.state() == VrMagazineState::Seating);
  CHECK(magazine.seat() == 0.0f);
}


TEST_CASE(magazine_is_ejected_from_below) {
  VrMagazineHand magazine;

  // The left hand cannot come near a pistol's magazine, inside the right
  // hand: it takes it from below, along the way out of the well
  VrVector3 below = belowWell(0.14f);
  below.x += 0.1f * M;
  CHECK(magazine.update(input(true, below)) == VrMagazineEvent::Ejected);

  VrMagazineHand far;
  CHECK(far.update(input(true, belowWell(0.3f))) == VrMagazineEvent::None);
}


TEST_CASE(holding_the_grip_does_not_eject) {
  VrMagazineHand magazine;

  CHECK(magazine.update(input(true, nearWell(0.3f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, nearWell(0.05f))) == VrMagazineEvent::None);
  CHECK(magazine.state() == VrMagazineState::InWeapon);
}


TEST_CASE(ejected_magazine_slides_out_of_its_well) {
  VrMagazineHand magazine;

  // Taken 5 cm beside the magazine: the hand moves it from there
  VrVector3 hand = nearWell(0.05f);
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::Ejected);

  hand.z -= 0.02f * M;
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::None);
  CHECK(std::abs(magazine.seat() - 0.02f) < 1e-4f);

  // Pushing up does not go past its place
  hand.z += 0.01f * M;
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::None);
  CHECK(std::abs(magazine.seat() - 0.01f) < 1e-4f);

  hand.z -= 0.05f * M;
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::Cleared);
  CHECK(magazine.state() == VrMagazineState::InHand);
}


TEST_CASE(magazine_comes_out_sideways) {
  VrMagazineHand magazine;

  CHECK(magazine.update(input(true, nearWell(0.0f))) == VrMagazineEvent::Ejected);
  CHECK(magazine.update(input(true, nearWell(0.05f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, nearWell(0.07f))) == VrMagazineEvent::Cleared);
}


TEST_CASE(ejected_magazine_pushed_back_goes_in) {
  VrMagazineHand magazine;

  // Pushed back before it moved, it stays where it is
  CHECK(magazine.update(input(true, nearWell(0.0f))) == VrMagazineEvent::Ejected);
  CHECK(magazine.update(input(true, belowWell(-0.01f))) == VrMagazineEvent::None);

  CHECK(magazine.update(input(true, belowWell(0.02f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.0f))) == VrMagazineEvent::Inserted);
  CHECK(magazine.state() == VrMagazineState::InWeapon);
}


TEST_CASE(magazine_let_go_of_is_out) {
  VrMagazineHand magazine;

  CHECK(magazine.update(input(true, nearWell(0.0f))) == VrMagazineEvent::Ejected);
  CHECK(magazine.update(input(false, belowWell(0.01f))) == VrMagazineEvent::Dropped);
  CHECK(magazine.state() == VrMagazineState::Out);

  // Pressing at the well takes nothing: the weapon is empty
  CHECK(magazine.update(input(true, nearWell(0.0f))) == VrMagazineEvent::None);
  CHECK(magazine.state() == VrMagazineState::Out);
}


TEST_CASE(new_magazine_is_taken_behind_the_back) {
  VrMagazineHand magazine;
  emptyWeapon(magazine);

  CHECK(magazine.update(input(true, Pouch)) == VrMagazineEvent::Taken);
  CHECK(magazine.state() == VrMagazineState::InHand);

  // Brought from below: it enters the well at its mouth, 3 cm out
  CHECK(magazine.update(input(true, belowWell(0.2f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.06f))) == VrMagazineEvent::Engaged);
  CHECK(magazine.state() == VrMagazineState::Seating);
  CHECK(std::abs(magazine.seat() - 0.03f) < 1e-4f);

  CHECK(magazine.update(input(true, belowWell(0.045f))) == VrMagazineEvent::None);
  CHECK(std::abs(magazine.seat() - 0.015f) < 1e-4f);
  CHECK(magazine.update(input(true, belowWell(0.03f))) == VrMagazineEvent::Inserted);
  CHECK(magazine.state() == VrMagazineState::InWeapon);
}


TEST_CASE(magazine_pulled_out_of_the_well_does_not_enter_at_once) {
  VrMagazineHand magazine;
  emptyWeapon(magazine);

  magazine.update(input(true, Pouch));
  CHECK(magazine.update(input(true, belowWell(0.04f))) == VrMagazineEvent::Engaged);
  CHECK(magazine.update(input(true, belowWell(0.1f))) == VrMagazineEvent::Cleared);

  // Back near the mouth: it must move away from the way in first
  CHECK(magazine.update(input(true, belowWell(0.05f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.2f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.05f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.25f))) == VrMagazineEvent::None);
  CHECK(magazine.update(input(true, belowWell(0.05f))) == VrMagazineEvent::Engaged);
}


TEST_CASE(magazine_enters_from_the_way_in) {
  VrMagazineHand magazine;
  emptyWeapon(magazine);
  magazine.update(input(true, Pouch));

  // 10 cm below the mouth and 5 cm aside, it enters at the mouth, and the
  // hand pushes it in from there
  VrVector3 hand = belowWell(0.13f);
  hand.x += 0.05f * M;
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::Engaged);
  CHECK(std::abs(magazine.seat() - 0.03f) < 1e-4f);

  hand.z += 0.03f * M;
  CHECK(magazine.update(input(true, hand)) == VrMagazineEvent::Inserted);
}


TEST_CASE(no_magazine_without_rounds) {
  VrMagazineHand magazine;
  emptyWeapon(magazine);

  VrMagazineInput empty = input(true, Pouch);
  empty.roundsHeld = false;

  CHECK(magazine.update(empty) == VrMagazineEvent::NoRounds);
  CHECK(magazine.state() == VrMagazineState::Out);
}


TEST_CASE(magazine_is_dropped_when_the_hand_is_busy) {
  VrMagazineHand magazine;

  magazine.update(input(true, nearWell(0.0f)));

  VrMagazineInput busy = input(true, belowWell(0.01f));
  busy.active = false;

  CHECK(magazine.update(busy) == VrMagazineEvent::Dropped);
  CHECK(magazine.state() == VrMagazineState::Out);
}


TEST_CASE(reset_puts_the_magazine_back) {
  VrMagazineHand magazine;
  emptyWeapon(magazine);
  magazine.reset();

  CHECK(magazine.state() == VrMagazineState::InWeapon);
}


TEST_CASE(magazine_timing_follows_the_animation) {
  // Out at 0.5 s, dropped 20 cm, a new one held 6 cm out from 1.0 s,
  // pushed in at 1.3 s, along the weapon's -z
  std::vector<VrMagazineSample> samples;

  for (int32_t i = 0; i <= 100; i++) {
    float t = float(i) * 0.02f;
    float out = 0.0f;

    if (t >= 0.5f && t < 0.8f)
      out = (t - 0.48f) * 0.6f;
    else if (t >= 0.8f && t < 1.2f)
      out = 0.06f;
    else if (t >= 1.2f && t < 1.3f)
      out = 0.06f * (1.3f - t) / 0.1f;

    samples.push_back({ t, { 0.0f, 0.0f, -out } });
  }

  VrMagazineTiming timing;
  CHECK(vrFindMagazineTiming(samples.data(), samples.size(), timing));
  CHECK(std::abs(timing.out - 0.5f) < 0.011f);
  CHECK(std::abs(timing.in - 1.3f) < 0.011f);
  CHECK(timing.grip > 1.2f && timing.grip < 1.25f);
  CHECK(std::abs(timing.axis.z + 1.0f) < 1e-4f);
}


TEST_CASE(magazine_timing_needs_the_magazine_back) {
  std::vector<VrMagazineSample> still = { { 0.0f, { } }, { 0.5f, { } }, { 1.0f, { } } };
  std::vector<VrMagazineSample> gone  = { { 0.0f, { } }, { 0.5f, { 0.0f, 0.0f, -0.1f } } };

  VrMagazineTiming timing;
  CHECK(!vrFindMagazineTiming(still.data(), still.size(), timing));
  CHECK(!vrFindMagazineTiming(gone.data(), gone.size(), timing));
}


TEST_CASE(sounds_are_matched_by_time) {
  // The 10mm pistol: out, in, chamber
  const float times[] = { 0.4f, 1.25f, 1.7f };

  CHECK(vrFindNearestSound(times, 3, 0.5f, 0.35f, -1) == 0);
  CHECK(vrFindNearestSound(times, 3, 1.3f, 0.35f, 0) == 1);
  CHECK(vrFindNearestSound(times, 3, 3.0f, 0.35f, -1) == -1);
  CHECK(vrFindNearestSound(times, 3, 0.45f, 0.35f, 0) == -1);
}


namespace {

  VrGameTransform heldAt(float x, float turn) {
    VrGameTransform t;
    t.translate = { x, 0.0f, 0.0f };
    t.rotate.m[0][0] = std::cos(turn);
    t.rotate.m[0][1] = -std::sin(turn);
    t.rotate.m[1][0] = std::sin(turn);
    t.rotate.m[1][1] = std::cos(turn);
    t.rotate.m[2][2] = 1.0f;
    return t;
  }

}


TEST_CASE(magazine_hold_is_where_the_hand_carries_it) {
  std::vector<VrGameTransform> samples;

  // Falling away from the hand, carried 8 cm from the hand bone, then
  // pushed in by the palm 12 cm from it
  for (int32_t i = 0; i < 10; i++)
    samples.push_back(heldAt(0.05f + 0.02f * float(i), 0.0f));
  for (int32_t i = 0; i < 10; i++)
    samples.push_back(heldAt(0.08f, 0.5f));
  for (int32_t i = 0; i < 10; i++)
    samples.push_back(heldAt(0.12f, 1.0f));

  size_t hold = vrFindMagazineHold(samples.data(), samples.size());
  CHECK(hold >= 10 && hold < 20);
}


TEST_CASE(magazine_hold_needs_the_hand_to_carry_it) {
  std::vector<VrGameTransform> samples;

  // Turning in the hand all the time
  for (int32_t i = 0; i < 10; i++)
    samples.push_back(heldAt(0.08f, 0.1f * float(i)));

  CHECK(vrFindMagazineHold(samples.data(), samples.size()) == samples.size());
  CHECK(vrFindMagazineHold(samples.data(), 0) == 0);
}


TEST_CASE(magazine_pushed_in_upside_down_is_turned_upright) {
  // Hand bone turned a quarter about the controller's forward: its x axis
  // along the controller's up
  VrGameRotation hand;
  hand.m[0][0] = 0.0f; hand.m[0][1] = -1.0f; hand.m[0][2] = 0.0f;
  hand.m[1][0] = 1.0f; hand.m[1][1] =  0.0f; hand.m[1][2] = 0.0f;
  hand.m[2][0] = 0.0f; hand.m[2][1] =  0.0f; hand.m[2][2] = 1.0f;

  VrVector3 up        = { 0.0f, 1.0f, 0.0f };
  VrVector3 insertion = { 0.0f, 0.0f, 1.0f };
  VrVector3 centre    = { 0.0f, 0.0f, 2.0f };

  // The end that goes in along the hand's -x: down in the controller
  VrGameTransform inHand;
  inHand.rotate.m[0][0] = 0.0f; inHand.rotate.m[0][1] = 0.0f; inHand.rotate.m[0][2] = -1.0f;
  inHand.rotate.m[1][0] = 0.0f; inHand.rotate.m[1][1] = 1.0f; inHand.rotate.m[1][2] =  0.0f;
  inHand.rotate.m[2][0] = 1.0f; inHand.rotate.m[2][1] = 0.0f; inHand.rotate.m[2][2] =  0.0f;
  inHand.translate = { 3.0f, 1.0f, 0.0f };

  VrVector3 middle = inHand.translate + inHand.rotate * centre;

  float angle = 0.0f;
  CHECK(vrUprightMagazineHold(hand, up, insertion, centre, inHand, angle));
  CHECK(std::abs(angle - 3.14159265f) < 1e-3f);

  VrVector3 top = hand * (inHand.rotate * insertion);
  CHECK(vrDot(top, up) > 0.999f);

  VrVector3 turnedMiddle = inHand.translate + inHand.rotate * centre;
  CHECK(vrLength(turnedMiddle - middle) < 1e-4f);
}


TEST_CASE(magazine_pushed_in_upright_is_kept) {
  VrGameRotation hand;
  VrVector3 up        = { 0.0f, 1.0f, 0.0f };
  VrVector3 insertion = { 0.0f, 1.0f, 0.0f };

  // Tilted forward by 60 degrees: still pointing up
  VrGameTransform inHand;
  float c = 0.5f, s = 0.8660254f;
  inHand.rotate.m[0][0] = c;    inHand.rotate.m[0][1] = -s;   inHand.rotate.m[0][2] = 0.0f;
  inHand.rotate.m[1][0] = s;    inHand.rotate.m[1][1] = c;    inHand.rotate.m[1][2] = 0.0f;
  inHand.rotate.m[2][0] = 0.0f; inHand.rotate.m[2][1] = 0.0f; inHand.rotate.m[2][2] = 1.0f;
  inHand.translate = { 1.0f, 2.0f, 3.0f };

  VrGameTransform before = inHand;
  float angle = 0.0f;
  CHECK(!vrUprightMagazineHold(hand, up, insertion, VrVector3 { 0.0f, 1.0f, 0.0f }, inHand, angle));
  CHECK(std::abs(angle - 1.0471976f) < 1e-3f);
  CHECK(vrLength(inHand.translate - before.translate) == 0.0f);
}


TEST_CASE(rack_parts_are_found_by_keyword) {
  CHECK(vrClassifyWeaponPart("##Slide") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##nmBar") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##ChargingHandle") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##127SmgChargingHandle:0") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##HRBolt") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##Bolt") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##SRLever") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##G3Lever") == VrWeaponPartKind::Rack);
  CHECK(vrClassifyWeaponPart("##cmSlide") == VrWeaponPartKind::Rack);
}


TEST_CASE(parts_next_to_the_rack_are_not_rack_parts) {
  CHECK(vrClassifyWeaponPart("##nmBarLock") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##BoltLock") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##MinigunBarrel") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##Barrels") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##LR_TriggerSlide") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##dgLeverPin") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##CockingMech") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##Hammer") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##Clip") == VrWeaponPartKind::Magazine);
}


TEST_CASE(rack_pulled_back_all_the_way_racks) {
  VrRackHand rack;

  CHECK(rack.update(rackInput(true, besideRack(0.05f))) == VrRackEvent::Grabbed);
  CHECK(rack.held());

  CHECK(rack.update(rackInput(true, behindRack(0.02f))) == VrRackEvent::None);
  CHECK(std::abs(rack.pull() - 0.02f) < 1e-3f);

  // Not further than its travel
  CHECK(rack.update(rackInput(true, behindRack(0.10f))) == VrRackEvent::None);
  CHECK(std::abs(rack.pull() - Travel) < 1e-4f);

  CHECK(rack.update(rackInput(false, behindRack(0.10f))) == VrRackEvent::Racked);
  CHECK(!rack.held());
  CHECK(rack.pull() == 0.0f);
}


TEST_CASE(rack_let_go_early_does_not_rack) {
  VrRackHand rack;

  CHECK(rack.update(rackInput(true, Rack)) == VrRackEvent::Grabbed);
  CHECK(rack.update(rackInput(true, behindRack(0.02f))) == VrRackEvent::None);
  CHECK(rack.update(rackInput(false, behindRack(0.02f))) == VrRackEvent::Released);
}


TEST_CASE(rack_pulled_back_and_pushed_home_racks) {
  VrRackHand rack;

  rack.update(rackInput(true, Rack));
  rack.update(rackInput(true, behindRack(Travel)));
  rack.update(rackInput(true, Rack));
  CHECK(rack.pull() == 0.0f);
  CHECK(rack.update(rackInput(false, Rack)) == VrRackEvent::Racked);
}


TEST_CASE(rack_locked_back_racks_when_let_go) {
  VrRackHand rack;

  CHECK(rack.update(rackInput(true, Rack, Travel)) == VrRackEvent::Grabbed);
  CHECK(std::abs(rack.pull() - Travel) < 1e-4f);
  CHECK(rack.update(rackInput(false, Rack, Travel)) == VrRackEvent::Racked);
}


TEST_CASE(rack_jammed_is_taken_where_it_rests_and_pulled_all_the_way) {
  VrRackHand rack;
  float rest = 0.3f * Travel;

  CHECK(rack.update(rackInput(true, Rack, rest)) == VrRackEvent::Grabbed);
  CHECK(std::abs(rack.pull() - rest) < 1e-4f);
  CHECK(rack.update(rackInput(false, Rack, rest)) == VrRackEvent::Released);

  rack.update(rackInput(true, Rack, rest));
  rack.update(rackInput(true, behindRack(Travel), rest));
  CHECK(std::abs(rack.pull() - Travel) < 1e-4f);
  CHECK(rack.update(rackInput(false, behindRack(Travel), rest)) == VrRackEvent::Racked);
}


TEST_CASE(rack_is_let_go_when_the_hand_leaves_sideways) {
  VrRackHand rack;

  rack.update(rackInput(true, Rack));
  CHECK(rack.update(rackInput(true, besideRack(0.15f))) == VrRackEvent::Released);
  CHECK(!rack.held());
}


TEST_CASE(rack_needs_a_press_near_it) {
  VrRackHand rack;

  CHECK(rack.update(rackInput(true, besideRack(0.2f))) == VrRackEvent::None);
  // Holding the trigger while the hand comes near takes nothing
  CHECK(rack.update(rackInput(true, Rack)) == VrRackEvent::None);
  CHECK(rack.update(rackInput(false, Rack)) == VrRackEvent::None);

  VrRackInput blocked = rackInput(true, Rack);
  blocked.blocked = true;
  CHECK(rack.update(blocked) == VrRackEvent::None);
  CHECK(!rack.held());

  rack.update(rackInput(false, Rack));
  // Along the way back counts as near it
  CHECK(rack.update(rackInput(true, behindRack(Travel))) == VrRackEvent::Grabbed);
}


TEST_CASE(magazine_is_not_ejected_when_the_rack_takes_the_press) {
  VrMagazineHand magazine;
  VrMagazineInput in = input(true, nearWell(0.0f));
  in.blocked = true;

  CHECK(magazine.update(in) == VrMagazineEvent::None);
  CHECK(magazine.state() == VrMagazineState::InWeapon);
}


TEST_CASE(rack_motion_is_found_where_it_goes_furthest_back) {
  std::vector<VrMagazineSample> samples(40);

  // Home, pulled back 4 cm from sample 10 to 20, home again at 30
  for (size_t i = 0; i < samples.size(); i++) {
    float back = i < 10 ? 0.0f : i < 20 ? 0.004f * float(i - 10 + 1) : i < 30 ? 0.004f * float(30 - i) : 0.0f;
    samples[i].time   = float(i) / 60.0f;
    samples[i].offset = { -back, 0.0f, 0.0f };
  }

  VrRackMotion motion;
  CHECK(vrFindRackMotion(samples.data(), samples.size(), motion));
  CHECK(motion.back == 19);
  CHECK(motion.start == 10);
  CHECK(motion.home == 30);
  CHECK(std::abs(motion.travel - 0.04f) < 1e-4f);
  CHECK(std::abs(motion.axis.x + 1.0f) < 1e-4f);
}


TEST_CASE(rack_motion_stays_back_to_the_end) {
  std::vector<VrMagazineSample> samples(30);

  // Locked back at sample 10 and never sent home
  for (size_t i = 0; i < samples.size(); i++)
    samples[i].offset = { i < 10 ? 0.0f : -0.05f, 0.0f, 0.0f };

  VrRackMotion motion;
  CHECK(vrFindRackMotion(samples.data(), samples.size(), motion));
  CHECK(motion.start == 10);
  CHECK(motion.back == 10);
  CHECK(motion.home == 29);
}


TEST_CASE(rack_sound_is_found_while_the_part_is_away) {
  // Slide locked back at 0.1 s, sent home with its sound at 1.4 s
  const float times[] = { 0.0f, 0.8f, 1.4f };

  CHECK(vrFindSoundDuring(times, 3, 0.1f, 0.5f, 0.4f, -1) == 0);
  CHECK(vrFindSoundDuring(times, 3, 1.0f, 1.45f, 0.4f, -1) == 2);
  CHECK(vrFindSoundDuring(times, 3, 1.6f, 1.7f, 0.1f, -1) == -1);
  CHECK(vrFindSoundDuring(times, 3, 1.6f, 1.7f, 0.4f, -1) == 2);
}


TEST_CASE(rack_motion_needs_the_part_to_move) {
  std::vector<VrMagazineSample> samples(20);

  for (size_t i = 0; i < samples.size(); i++)
    samples[i].offset = { 0.002f, 0.0f, 0.0f };

  VrRackMotion motion;
  CHECK(!vrFindRackMotion(samples.data(), samples.size(), motion));
  CHECK(!vrFindRackMotion(samples.data(), 0, motion));
}


TEST_CASE(rounds_are_found_by_keyword) {
  CHECK(vrClassifyWeaponPart("##357Shell") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##Shell") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##Shells") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##ShellFull") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##Bullet1") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##ShellEmpty") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##swmCasing") == VrWeaponPartKind::None);
}


TEST_CASE(round_timing_finds_its_place) {
  // As the lever-action shotgun: carried in from 8 cm out along +z over
  // 0.3 s, still for 0.7 s, then moved away again (hidden by the game)
  std::vector<VrMagazineSample> samples;

  for (int32_t i = 0; i <= 90; i++) {
    float t = float(i) / 60.0f;
    float out = 0.0f;

    if (t < 0.3f)
      out = 0.08f * (0.3f - t) / 0.3f;
    else if (t > 1.0f)
      out = -0.02f * (t - 1.0f) / 0.5f;

    samples.push_back({ t, { 0.1f, 0.05f, out } });
  }

  VrMagazineTiming timing;
  size_t place = 0;
  CHECK(vrFindRoundTiming(samples.data(), samples.size(), timing, place));
  CHECK(std::abs(timing.in - 0.3f) < 0.02f);
  CHECK(std::abs(samples[place].offset.z) < 0.003f);
  CHECK(timing.axis.z > 0.999f);
  CHECK(timing.grip > 0.1f && timing.grip < 0.17f);
}


TEST_CASE(round_timing_from_a_round_that_starts_in_its_place) {
  // As the .357 revolver: at its place, out 13 cm, back at 0.33 s
  std::vector<VrMagazineSample> samples;

  for (int32_t i = 0; i <= 60; i++) {
    float t = float(i) / 60.0f;
    float out = 0.0f;

    if (t > 0.0f && t < 0.33f)
      out = 0.13f * std::sin(3.14159265f * t / 0.33f);

    samples.push_back({ t, { -out, 0.0f, 0.0f } });
  }

  VrMagazineTiming timing;
  size_t place = 0;
  CHECK(vrFindRoundTiming(samples.data(), samples.size(), timing, place));
  CHECK(timing.in > 0.3f && timing.in < 0.36f);
  CHECK(timing.axis.x < -0.999f);
}


TEST_CASE(round_timing_needs_the_round_to_move_in_and_stop) {
  std::vector<VrMagazineSample> still, moving;

  for (int32_t i = 0; i <= 30; i++) {
    float t = float(i) / 60.0f;
    still.push_back({ t, { } });
    moving.push_back({ t, { 0.0f, 0.0f, 0.01f * float(i) } });
  }

  VrMagazineTiming timing;
  size_t place = 0;
  CHECK(!vrFindRoundTiming(still.data(), still.size(), timing, place));
  CHECK(!vrFindRoundTiming(moving.data(), moving.size(), timing, place));
}


TEST_CASE(hinges_are_found_by_keyword) {
  CHECK(vrClassifyWeaponPart("##DBStock") == VrWeaponPartKind::Hinge);
  CHECK(vrClassifyWeaponPart("##CVSHStock") == VrWeaponPartKind::Hinge);
  CHECK(vrClassifyWeaponPart("##sosgStock") == VrWeaponPartKind::Hinge);
  CHECK(vrClassifyWeaponPart("##DBShells") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##CVSHShell1") == VrWeaponPartKind::Round);
  CHECK(vrClassifyWeaponPart("##DBLatch") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##CVSHSwitch") == VrWeaponPartKind::None);
}


namespace {

  // The barrel points north from the breech, which is at the well
  const VrVector3 North = { 0.0f, 1.0f, 0.0f };

  VrBreakInput breakInput(bool grip, const VrVector3& hand) {
    VrBreakInput in;
    in.active = true;
    in.grip   = grip;
    in.hand   = hand;
    in.breech = Well;
    in.barrel = North;
    return in;
  }

  // A point along the barrel, and beside it
  VrVector3 alongBarrel(float metres, float beside) {
    return { Well.x + beside * M, Well.y + metres * M, Well.z };
  }

}


namespace {

  // A lock over the well, 12 cm long to its middle, pointing north when
  // shut and turned up about the east axis by 90 degrees to open
  const VrVector3 LockAxis = { 1.0f, 0.0f, 0.0f };
  const VrVector3 LockArm  = { 0.0f, 0.12f * M, 0.0f };

  VrLockInput lockInput(bool grip, float degrees, float metres) {
    float a = degrees * 3.14159265f / 180.0f;

    VrLockInput in;
    in.active = true;
    in.grip   = grip;
    in.pivot  = Well;
    in.axis   = LockAxis;
    in.arm    = LockArm;
    in.angle  = 3.14159265f / 2.0f;
    in.hand   = { Well.x, Well.y + std::cos(a) * metres * M, Well.z + std::sin(a) * metres * M };
    return in;
  }

}


TEST_CASE(lock_turns_open_and_shut_with_the_hand) {
  VrLockHand lock;

  CHECK(lock.update(lockInput(true, 0.0f, 0.2f)) == VrLockEvent::Grabbed);
  CHECK(lock.held());
  CHECK(lock.update(lockInput(true, 45.0f, 0.2f)) == VrLockEvent::None);
  CHECK(std::abs(lock.turn() - 0.5f) < 1e-3f);
  CHECK(!lock.open());

  CHECK(lock.update(lockInput(true, 80.0f, 0.2f)) == VrLockEvent::Opened);
  CHECK(lock.open());

  // Not past its angle, and let go of it stays open
  lock.update(lockInput(true, 150.0f, 0.2f));
  CHECK(lock.turn() == 1.0f);
  CHECK(lock.update(lockInput(false, 60.0f, 0.2f)) == VrLockEvent::None);
  CHECK(!lock.held());
  CHECK(lock.open());
  CHECK(lock.turn() == 1.0f);

  // Taken where it is, open, and turned back down
  CHECK(lock.update(lockInput(true, 90.0f, 0.15f)) == VrLockEvent::Grabbed);
  CHECK(lock.update(lockInput(true, 30.0f, 0.15f)) == VrLockEvent::None);
  CHECK(lock.update(lockInput(true, 10.0f, 0.15f)) == VrLockEvent::Shut);
  CHECK(!lock.open());

  lock.update(lockInput(false, 10.0f, 0.15f));
  CHECK(lock.turn() == 0.0f);
}


TEST_CASE(lock_needs_a_press_near_it) {
  VrLockHand lock;

  // Beyond its end, or held down before it came near
  CHECK(lock.update(lockInput(true, 0.0f, 0.4f)) == VrLockEvent::None);
  CHECK(lock.update(lockInput(true, 0.0f, 0.2f)) == VrLockEvent::None);
  CHECK(!lock.held());

  // Blocked: a hand holding a box
  VrLockInput blocked = lockInput(false, 0.0f, 0.2f);
  lock.update(blocked);
  blocked.grip    = true;
  blocked.blocked = true;
  CHECK(lock.update(blocked) == VrLockEvent::None);

  // Shut, it is not found where it would be open
  lock.update(lockInput(false, 90.0f, 0.2f));
  CHECK(lock.update(lockInput(true, 90.0f, 0.2f)) == VrLockEvent::None);

  lock.update(lockInput(false, 0.0f, 0.2f));
  lock.update(lockInput(true, 0.0f, 0.2f));
  lock.update(lockInput(true, 90.0f, 0.2f));
  CHECK(lock.open());

  lock.reset();
  CHECK(!lock.open());
  CHECK(!lock.held());
}


TEST_CASE(break_action_opens_and_closes_with_presses_near_the_barrel) {
  VrBreakHand action;

  CHECK(action.update(breakInput(true, alongBarrel(0.2f, 0.05f))) == VrBreakEvent::Opened);
  CHECK(action.open());

  // Holding the trigger does nothing more
  CHECK(action.update(breakInput(true, alongBarrel(0.1f, 0.0f))) == VrBreakEvent::None);
  CHECK(action.update(breakInput(false, alongBarrel(0.1f, 0.0f))) == VrBreakEvent::None);

  CHECK(action.update(breakInput(true, alongBarrel(0.0f, 0.08f))) == VrBreakEvent::Closed);
  CHECK(!action.open());
}


TEST_CASE(break_action_is_not_worked_away_from_the_barrel) {
  VrBreakHand action;

  // Beside the barrel, beyond its muzzle, behind the breech
  CHECK(action.update(breakInput(true, alongBarrel(0.2f, 0.15f))) == VrBreakEvent::None);
  CHECK(action.update(breakInput(false, alongBarrel(0.2f, 0.15f))) == VrBreakEvent::None);
  CHECK(action.update(breakInput(true, alongBarrel(0.5f, 0.0f))) == VrBreakEvent::None);
  CHECK(action.update(breakInput(false, alongBarrel(0.5f, 0.0f))) == VrBreakEvent::None);
  CHECK(action.update(breakInput(true, alongBarrel(-0.15f, 0.0f))) == VrBreakEvent::None);
  CHECK(!action.open());

  // A hand holding shells does not work it
  VrBreakInput blocked = breakInput(false, alongBarrel(0.1f, 0.0f));
  action.update(blocked);
  blocked.grip    = true;
  blocked.blocked = true;
  CHECK(action.update(blocked) == VrBreakEvent::None);

  action.update(breakInput(false, alongBarrel(0.1f, 0.0f)));
  CHECK(action.update(breakInput(true, alongBarrel(0.1f, 0.0f))) == VrBreakEvent::Opened);

  action.reset();
  CHECK(!action.open());
}


TEST_CASE(shells_do_not_enter_a_closed_weapon) {
  VrMagazineHand shells;
  emptyWeapon(shells);
  CHECK(shells.state() == VrMagazineState::Out);

  VrMagazineInput in = input(true, Pouch);
  in.closed = true;
  CHECK(shells.update(in) == VrMagazineEvent::Taken);

  in.held = in.hand = belowWell(0.06f);
  CHECK(shells.update(in) == VrMagazineEvent::None);
  CHECK(shells.state() == VrMagazineState::InHand);

  // Open, they enter
  in.closed = false;
  CHECK(shells.update(in) == VrMagazineEvent::Engaged);
}


TEST_CASE(opening_a_break_action_empties_it) {
  VrMagazineHand shells;
  shells.empty();
  CHECK(shells.state() == VrMagazineState::Out);

  // Nothing to take out: new shells are taken behind the back
  CHECK(shells.update(input(true, Pouch)) == VrMagazineEvent::Taken);
  CHECK(shells.update(input(false, Pouch)) == VrMagazineEvent::Dropped);
  CHECK(shells.state() == VrMagazineState::Out);
}


TEST_CASE(revolver_parts_are_found_by_keyword) {
  CHECK(vrClassifyWeaponPart("##SpeedLoader") == VrWeaponPartKind::Loader);
  CHECK(vrClassifyWeaponPart("##SpeedLoaderBullets:1") == VrWeaponPartKind::Loader);
  CHECK(vrClassifyWeaponPart("##LoadingGate") == VrWeaponPartKind::Gate);
  CHECK(vrClassifyWeaponPart("##swmCrane") == VrWeaponPartKind::None);
  CHECK(vrClassifyWeaponPart("##ShellFull") == VrWeaponPartKind::Round);

  CHECK(vrIsCartridgePart("##swmCasing"));
  CHECK(vrIsCartridgePart("##ShellEmpty"));
  CHECK(vrIsCartridgePart("##Bullet3"));
  CHECK(vrIsCartridgePart("##Shell2:0"));
  CHECK(!vrIsCartridgePart("##SpeedLoaderBullets"));
  CHECK(!vrIsCartridgePart("##swmCylinder"));
  CHECK(!vrIsCartridgePart("Bullet1"));
}


namespace {

  // The north-facing frame rolled about its forward axis
  VrGameCameraPose rolled(float angle) {
    VrGameCameraPose pose = northHead();
    pose.right = { std::cos(angle), 0.0f, -std::sin(angle) };
    pose.up    = { std::sin(angle), 0.0f,  std::cos(angle) };
    return pose;
  }

  VrFlickInput flick(float roll, float dt = 0.011f) {
    VrFlickInput in;
    in.active = true;
    in.roll   = roll;
    in.dt     = dt;
    return in;
  }

}


TEST_CASE(turn_rate_about_an_axis) {
  const VrVector3 forward = { 0.0f, 1.0f, 0.0f };
  const VrVector3 up      = { 0.0f, 0.0f, 1.0f };

  CHECK(std::fabs(vrTurnRate(rolled(0.2f), rolled(0.3f), forward, 0.01f) - 10.0f) < 0.01f);
  CHECK(std::fabs(vrTurnRate(rolled(0.3f), rolled(0.2f), forward, 0.01f) + 10.0f) < 0.01f);
  CHECK(std::fabs(vrTurnRate(rolled(0.2f), rolled(0.3f), up, 0.01f)) < 0.01f);
  CHECK(vrTurnRate(rolled(0.2f), rolled(0.2f), forward, 0.01f) == 0.0f);
  CHECK(vrTurnRate(rolled(0.2f), rolled(0.3f), forward, 0.0f) == 0.0f);
}


TEST_CASE(flick_opens_and_closes_the_cylinder) {
  VrFlickHand cylinder;

  CHECK(cylinder.update(flick(10.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(9.0f)) == VrBreakEvent::Opened);
  CHECK(cylinder.open());

  // The wrist turning back right after it does not close it
  CHECK(cylinder.update(flick(-10.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(-10.0f)) == VrBreakEvent::None);

  for (int i = 0; i < 6; i++)
    CHECK(cylinder.update(flick(0.0f, 0.1f)) == VrBreakEvent::None);

  // Turned the way it opens again, it stays open
  CHECK(cylinder.update(flick(10.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(10.0f)) == VrBreakEvent::None);

  CHECK(cylinder.update(flick(-10.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(-8.0f)) == VrBreakEvent::Closed);
  CHECK(!cylinder.open());
}


TEST_CASE(flick_needs_two_fast_frames) {
  VrFlickHand cylinder;

  // One fast frame, a slow one, and turns that change direction
  CHECK(cylinder.update(flick(20.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(3.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(20.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(-20.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(20.0f)) == VrBreakEvent::None);
  CHECK(!cylinder.open());

  // Not while the hand pushes rounds in, nor when it is not active
  VrFlickInput blocked = flick(20.0f);
  blocked.blocked = true;
  CHECK(cylinder.update(blocked) == VrBreakEvent::None);
  CHECK(cylinder.update(blocked) == VrBreakEvent::None);

  VrFlickInput inactive = flick(20.0f);
  inactive.active = false;
  CHECK(cylinder.update(inactive) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(20.0f)) == VrBreakEvent::None);
  CHECK(cylinder.update(flick(20.0f)) == VrBreakEvent::Opened);

  cylinder.reset();
  CHECK(!cylinder.open());
}

namespace {

  // A round enters the loading port in front of the body
  const VrVector3 Port = { 0.1f * M, 0.4f * M, 1.3f * M };

  // One round per loop: it comes to its place at 0.3 s, the loop starts
  // again at 0.5 s
  const VrRoundLoopTiming Loop = { 0.5f, 0.3f };

  VrRoundInput handfulInput(bool grip, const VrVector3& hand, float dt = 0.1f) {
    VrRoundInput in;
    in.active     = true;
    in.grip       = grip;
    in.hand       = hand;
    in.round      = hand;
    in.port       = Port;
    in.head       = northHead();
    in.roundsHeld = true;
    in.dt         = dt;
    return in;
  }

  VrVector3 nearPort(float metres) {
    return { Port.x + metres * M, Port.y, Port.z };
  }

  // Takes a handful behind the back and brings it to the port
  void startLoading(VrRoundLoader& loader) {
    loader.update(handfulInput(false, Pouch), Loop);
    CHECK(loader.update(handfulInput(true, Pouch), Loop) == VrRoundEvent::Taken);
    CHECK(loader.update(handfulInput(true, nearPort(0.03f)), Loop) == VrRoundEvent::Started);
    CHECK(loader.state() == VrRoundState::Loading);
  }

}

TEST_CASE(handful_is_taken_behind_the_back) {
  VrRoundLoader loader;

  CHECK(loader.update(handfulInput(true, nearPort(0.5f)), Loop) == VrRoundEvent::None);
  loader.update(handfulInput(false, Pouch), Loop);
  CHECK(loader.update(handfulInput(true, Pouch), Loop) == VrRoundEvent::Taken);
  CHECK(loader.state() == VrRoundState::Holding);

  // Holding the trigger while the hand moves takes nothing more
  CHECK(loader.update(handfulInput(true, nearPort(0.5f)), Loop) == VrRoundEvent::None);
}

TEST_CASE(no_handful_without_rounds) {
  VrRoundLoader loader;
  VrRoundInput in = handfulInput(true, Pouch);
  in.roundsHeld = false;

  CHECK(loader.update(in, Loop) == VrRoundEvent::NoRounds);
  CHECK(loader.state() == VrRoundState::Empty);
}

TEST_CASE(loading_places_one_round_per_loop) {
  VrRoundLoader loader;
  startLoading(loader);

  uint32_t placed = 0;

  // 2.05 s of animation at 0.1 s per frame: loops at 0, 0.5, 1, 1.5 and 2 s
  for (uint32_t i = 0; i < 20; i++) {
    if (loader.update(handfulInput(true, nearPort(0.03f)), Loop) == VrRoundEvent::Placed)
      placed += 1;
  }

  CHECK(placed == 4);
  CHECK(loader.state() == VrRoundState::Loading);
  CHECK(loader.time() < Loop.length);
}

TEST_CASE(loading_stops_when_the_hand_leaves_the_port) {
  VrRoundLoader loader;
  startLoading(loader);

  CHECK(loader.update(handfulInput(true, nearPort(0.10f)), Loop) == VrRoundEvent::None);
  CHECK(loader.update(handfulInput(true, nearPort(0.20f)), Loop) == VrRoundEvent::Stopped);
  CHECK(loader.state() == VrRoundState::Holding);

  // Back at the port, it loads again from the start of the loop
  CHECK(loader.update(handfulInput(true, nearPort(0.02f)), Loop) == VrRoundEvent::Started);
  CHECK(loader.time() == 0.0f);
}

TEST_CASE(loading_stops_when_the_port_closes) {
  VrRoundLoader loader;
  startLoading(loader);

  VrRoundInput in = handfulInput(true, nearPort(0.03f));
  in.closed = true;

  CHECK(loader.update(in, Loop) == VrRoundEvent::Stopped);
  CHECK(loader.update(in, Loop) == VrRoundEvent::None);
  CHECK(loader.state() == VrRoundState::Holding);
}

TEST_CASE(handful_let_go_of_is_gone) {
  VrRoundLoader loader;
  startLoading(loader);

  CHECK(loader.update(handfulInput(false, nearPort(0.03f)), Loop) == VrRoundEvent::Dropped);
  CHECK(loader.state() == VrRoundState::Empty);
}

TEST_CASE(full_weapon_keeps_the_handful_until_the_hand_moves_away) {
  VrRoundLoader loader;
  startLoading(loader);

  VrRoundEvent event = VrRoundEvent::None;

  for (uint32_t i = 0; i < 5 && event != VrRoundEvent::Placed; i++)
    event = loader.update(handfulInput(true, nearPort(0.03f)), Loop);

  CHECK(event == VrRoundEvent::Placed);
  loader.refuse();
  CHECK(loader.state() == VrRoundState::Holding);

  // It does not start again at once
  CHECK(loader.update(handfulInput(true, nearPort(0.03f)), Loop) == VrRoundEvent::None);
  CHECK(loader.update(handfulInput(true, nearPort(0.2f)), Loop) == VrRoundEvent::None);
  CHECK(loader.update(handfulInput(true, nearPort(0.03f)), Loop) == VrRoundEvent::Started);
}


TEST_CASE(loading_port_cap_is_a_gate) {
  CHECK(vrClassifyWeaponPart("##ReloadCap") == VrWeaponPartKind::Gate);
  CHECK(vrClassifyWeaponPart("##CRLever") == VrWeaponPartKind::Rack);
}


namespace {

  VrReloadSituation situation(VrReloadKind kind, int32_t clip) {
    VrReloadSituation s;
    s.kind = kind;
    s.clip = clip;
    return s;
  }

}


TEST_CASE(nothing_glows_on_a_loaded_magazine_weapon) {
  VrReloadSituation s = situation(VrReloadKind::Magazine, 12);
  s.hasRack = true;
  CHECK(!vrPartsToWork(s).any());
}


TEST_CASE(empty_magazine_glows) {
  VrReloadSituation s = situation(VrReloadKind::Magazine, 0);
  s.hasRack = true;

  VrReloadParts parts = vrPartsToWork(s);
  CHECK(parts.magazine);
  CHECK(!parts.rack);

  // Once out, nothing of it is left in the weapon to glow
  s.magazine = VrMagazineState::InHand;
  CHECK(!vrPartsToWork(s).any());
  s.magazine = VrMagazineState::Out;
  CHECK(!vrPartsToWork(s).any());
}


TEST_CASE(new_magazine_with_nothing_chambered_asks_for_the_rack) {
  VrReloadSituation s = situation(VrReloadKind::Magazine, 0);
  s.hasRack     = true;
  s.pendingFill = true;

  VrReloadParts parts = vrPartsToWork(s);
  CHECK(parts.rack);
  CHECK(!parts.magazine);
}


TEST_CASE(jam_glows_on_the_rack_or_the_cell) {
  VrReloadSituation s = situation(VrReloadKind::Magazine, 5);
  s.jammed  = true;
  s.hasRack = true;
  CHECK(vrPartsToWork(s).rack);
  CHECK(!vrPartsToWork(s).magazine);

  // An energy weapon with no rack part is cleared by taking its cell out
  s.hasRack = false;
  CHECK(vrPartsToWork(s).magazine);
  CHECK(!vrPartsToWork(s).rack);
}


TEST_CASE(port_glows_while_rounds_are_held_until_full) {
  VrReloadSituation s = situation(VrReloadKind::Rounds, 3);
  s.hasRack = true;
  CHECK(!vrPartsToWork(s).any());

  s.rounds = VrRoundState::Holding;
  CHECK(vrPartsToWork(s).port);

  s.rounds = VrRoundState::Loading;
  CHECK(vrPartsToWork(s).port);

  s.full = true;
  CHECK(!vrPartsToWork(s).port);
}


TEST_CASE(lever_glows_once_rounds_are_loaded_after_the_last_shot) {
  VrReloadSituation s = situation(VrReloadKind::Rounds, 0);
  s.hasRack     = true;
  s.unchambered = true;
  CHECK(!vrPartsToWork(s).rack);

  s.clip = 2;
  CHECK(vrPartsToWork(s).rack);

  // Not while the hand holds rounds: it cannot take the lever then
  s.rounds = VrRoundState::Holding;
  CHECK(!vrPartsToWork(s).rack);
  CHECK(vrPartsToWork(s).port);
}


TEST_CASE(break_action_glows_to_open_then_to_close) {
  VrReloadSituation s = situation(VrReloadKind::BreakAction, 0);
  CHECK(vrPartsToWork(s).hinge);

  // Open and empty: the shells are needed, nothing glows
  s.actionOpen = true;
  s.magazine   = VrMagazineState::Out;
  CHECK(!vrPartsToWork(s).any());

  s.magazine = VrMagazineState::Seating;
  CHECK(!vrPartsToWork(s).any());

  // Loaded and open: close it
  s.magazine = VrMagazineState::InWeapon;
  s.clip     = 2;
  CHECK(vrPartsToWork(s).hinge);

  // Closed and loaded
  s.actionOpen = false;
  CHECK(!vrPartsToWork(s).any());

  s.jammed = true;
  CHECK(vrPartsToWork(s).hinge);
}


TEST_CASE(lock_glows_before_and_after_the_box) {
  VrReloadSituation s = situation(VrReloadKind::Magazine, 0);
  s.hasLock = true;

  // Shut over an empty box: the lock first, then the box
  VrReloadParts parts = vrPartsToWork(s);
  CHECK(parts.lock);
  CHECK(!parts.magazine);

  s.lockOpen = true;
  parts = vrPartsToWork(s);
  CHECK(parts.magazine);
  CHECK(!parts.lock);

  // A loaded box under an open lock: the lock, to shut it
  s.clip = 240;
  parts = vrPartsToWork(s);
  CHECK(parts.lock);
  CHECK(!parts.magazine);

  s.lockOpen = false;
  CHECK(!vrPartsToWork(s).any());

  // While the box is out, nothing of the weapon glows
  s.lockOpen = true;
  s.magazine = VrMagazineState::InHand;
  CHECK(!vrPartsToWork(s).any());
}


TEST_CASE(cylinders_and_weapons_not_reloaded_by_hand_never_glow) {
  CHECK(!vrPartsToWork(situation(VrReloadKind::Cylinder, 0)).any());
  CHECK(!vrPartsToWork(situation(VrReloadKind::None, 0)).any());
}
