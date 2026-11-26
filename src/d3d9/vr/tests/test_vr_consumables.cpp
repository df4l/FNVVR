#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test.h"

#include "../vr_consumable_animation.h"
#include "../vr_consumable_gesture.h"

using namespace dxvk;

namespace {

  /**
   * \brief Writes a small animation file as the game's are written
   */
  class KfWriter {

  public:

    struct Track {
      std::string name;
      // Translation keys: time, x, y, z
      std::vector<std::array<float, 4>> translations;
      // XYZ angle keys on the X axis: time, angle
      std::vector<std::array<float, 2>> xAngles;
    };

    std::vector<Track>                              tracks;
    std::vector<std::pair<float, std::string>>      textKeys;
    float                                           stop = 1.0f;

    std::vector<uint8_t> write() const {
      std::vector<std::string> strings;
      auto string = [&strings] (const std::string& s) {
        for (size_t i = 0; i < strings.size(); i++) {
          if (strings[i] == s)
            return int32_t(i);
        }
        strings.push_back(s);
        return int32_t(strings.size() - 1);
      };

      // Blocks: 0 sequence, 1 text keys, then interpolator and data per track
      std::vector<std::vector<uint8_t>> blocks;
      std::vector<uint16_t> kinds;

      std::vector<uint8_t> sequence;
      put(sequence, string("SpecialIdle"));
      put(sequence, uint32_t(tracks.size()));
      put(sequence, uint32_t(1));

      for (size_t i = 0; i < tracks.size(); i++) {
        put(sequence, int32_t(2 + 2 * i));
        put(sequence, int32_t(-1));
        sequence.push_back(0);
        put(sequence, string(tracks[i].name));
        for (int j = 0; j < 4; j++)
          put(sequence, int32_t(-1));
      }

      put(sequence, 1.0f);
      put(sequence, int32_t(1));
      put(sequence, uint32_t(2));
      put(sequence, 1.0f);
      put(sequence, 0.0f);
      put(sequence, stop);
      put(sequence, int32_t(-1));
      put(sequence, int32_t(-1));
      blocks.push_back(sequence);
      kinds.push_back(0);

      std::vector<uint8_t> text;
      put(text, int32_t(-1));
      put(text, uint32_t(textKeys.size()));
      for (const auto& key : textKeys) {
        put(text, key.first);
        put(text, string(key.second));
      }
      blocks.push_back(text);
      kinds.push_back(1);

      for (size_t i = 0; i < tracks.size(); i++) {
        std::vector<uint8_t> interpolator;
        for (int j = 0; j < 3; j++)
          put(interpolator, 0.0f);
        put(interpolator, 1.0f);
        for (int j = 0; j < 3; j++)
          put(interpolator, 0.0f);
        put(interpolator, 1.0f);
        put(interpolator, int32_t(3 + 2 * i));
        blocks.push_back(interpolator);
        kinds.push_back(2);

        std::vector<uint8_t> data;
        if (tracks[i].xAngles.empty()) {
          put(data, uint32_t(0));
        } else {
          put(data, uint32_t(1));
          put(data, uint32_t(4));
          put(data, uint32_t(tracks[i].xAngles.size()));
          put(data, uint32_t(1));
          for (const auto& key : tracks[i].xAngles) {
            put(data, key[0]);
            put(data, key[1]);
          }
          put(data, uint32_t(0));
          put(data, uint32_t(0));
        }
        put(data, uint32_t(tracks[i].translations.size()));
        if (!tracks[i].translations.empty()) {
          put(data, uint32_t(1));
          for (const auto& key : tracks[i].translations)
            for (float v : key)
              put(data, v);
        }
        put(data, uint32_t(0));
        blocks.push_back(data);
        kinds.push_back(3);
      }

      const char* types[] = { "NiControllerSequence", "NiTextKeyExtraData", "NiTransformInterpolator", "NiTransformData" };

      std::vector<uint8_t> file;
      const char line[] = "Gamebryo File Format, Version 20.2.0.7\n";
      file.insert(file.end(), line, line + std::strlen(line));
      put(file, uint32_t(0x14020007));
      file.push_back(1);
      put(file, uint32_t(11));
      put(file, uint32_t(blocks.size()));
      put(file, uint32_t(34));
      for (int i = 0; i < 3; i++)
        file.push_back(0);
      put(file, uint16_t(4));
      for (const char* type : types) {
        put(file, uint32_t(std::strlen(type)));
        file.insert(file.end(), type, type + std::strlen(type));
      }
      for (uint16_t kind : kinds)
        put(file, kind);
      for (const auto& block : blocks)
        put(file, uint32_t(block.size()));
      put(file, uint32_t(strings.size()));
      put(file, uint32_t(64));
      for (const std::string& s : strings) {
        put(file, uint32_t(s.size()));
        file.insert(file.end(), s.begin(), s.end());
      }
      put(file, uint32_t(0));
      for (const auto& block : blocks)
        file.insert(file.end(), block.begin(), block.end());
      return file;
    }

  private:

    template<typename T>
    static void put(std::vector<uint8_t>& out, T value) {
      uint8_t bytes[sizeof(T)];
      std::memcpy(bytes, &value, sizeof(T));
      out.insert(out.end(), bytes, bytes + sizeof(T));
    }

  };

  // A drink: prop, cap off between 0.8 and 1.0 s, two sounds after
  KfWriter drink() {
    KfWriter kf;
    kf.stop = 3.0f;
    kf.tracks.push_back({ "B42AttachPointL", { { 0.0f, 5.0f, 2.0f, 0.0f }, { 3.0f, 5.0f, 2.0f, 0.0f } }, { } });
    kf.tracks.push_back({ "##Cap_B42", { { 0.0f, 0.0f, 10.0f, 0.0f }, { 0.8f, 0.0f, 10.0f, 0.0f },
      { 1.0f, 0.0f, 20.0f, 0.0f } }, { } });
    kf.tracks.push_back({ "##Liquid_B42", { { 0.0f, 0.0f, 0.0f, 0.0f } }, { } });
    kf.tracks.push_back({ "Bip01 L Finger1", { }, { { 0.0f, 0.0f }, { 3.0f, 1.0f } } });
    kf.textKeys = {
      { 0.0f,  "start" },
      { 0.03f, "ScriptLine: Goo1.AuxVarSetStr \"*_InjMP\" \"B42Inject\\Bottle.nif\"" },
      { 0.07f, "ScriptLine: Goo1.AuxVarSetStr \"*_InJSP\" \"fx\\Hit\\Draw.wav\"" },
      { 0.1f,  "ScriptLine: Goo1.AuxVarSetFlt \"*_InjM\" 1" },
      { 0.8f,  "ScriptLine: Goo1.AuxVarSetStr \"*_InJSP\" \"fx\\Hit\\CapOff.wav\"" },
      { 1.9f,  "ScriptLine: Goo1.AuxVarSetStr \"*_InJSP\" \"fx\\Hit\\Glug.wav\"" },
      { 2.2f,  "ScriptLine: Goo1.AuxVarSetFlt \"*_InJS\" 1" },
      { 2.9f,  "ScriptLine: Goo1.AuxVarSetFlt \"*_InjM\" 3" },
      { 3.0f,  "end" },
    };
    return kf;
  }

  VrGameCameraPose northHead() {
    VrGameCameraPose head;
    head.forward  = { 0.0f, 1.0f, 0.0f };
    head.up       = { 0.0f, 0.0f, 1.0f };
    head.right    = { 1.0f, 0.0f, 0.0f };
    head.position = { 0.0f, 0.0f, 1.7f * VrGameUnitsPerMetre };
    return head;
  }

  VrVector3 fromHead(float east, float north, float up) {
    VrVector3 head = northHead().position;
    return { head.x + east * VrGameUnitsPerMetre, head.y + north * VrGameUnitsPerMetre,
      head.z + up * VrGameUnitsPerMetre };
  }

}


TEST_CASE(consumables_known_items_are_found_by_file_and_form) {
  CHECK(std::string(vrConsumableEditorId("FalloutNV.esm", 0x00015169)) == "Stimpak");
  CHECK(std::string(vrConsumableEditorId("falloutnv.esm", 0x0501519E)) == "NukaCola");
  CHECK(std::string(vrConsumableEditorId("HonestHearts.esm", 0x0A011111)) == "NVDLC02WastelandTequila");
  CHECK(vrConsumableEditorId("FalloutNV.esm", 0x00011111) == nullptr);
  CHECK(vrConsumableEditorId("Other.esp", 0x00015169) == nullptr);
}


TEST_CASE(consumables_gestures_come_from_the_animation_name) {
  CHECK(vrConsumeGestureOf("Stimpak") == VrConsumeGesture::Body);
  CHECK(vrConsumeGestureOf("magazine") == VrConsumeGesture::Read);
  CHECK(vrConsumeGestureOf("DoctorBag") == VrConsumeGesture::Instant);
  CHECK(vrConsumeGestureOf("NukaCola") == VrConsumeGesture::Mouth);
  CHECK(vrConsumeGestureOf("SomeModDrink") == VrConsumeGesture::Mouth);
}


TEST_CASE(consumables_kf_tracks_and_keys_are_read) {
  std::vector<uint8_t> file = drink().write();
  VrKfAnimation animation;

  CHECK(animation.read(file.data(), file.size()));
  CHECK(animation.tracks().size() == 4);
  CHECK(animation.textKeys().size() == 9);
  CHECK_NEAR(animation.length(), 3.0f, 1e-5f);

  const VrKfTrack* cap = animation.track("##Cap_B42");
  CHECK(cap != nullptr);

  if (cap) {
    CHECK_NEAR(VrKfAnimation::sample(*cap, 0.9f).translate.y, 15.0f, 1e-4f);
    CHECK_NEAR(VrKfAnimation::sample(*cap, 5.0f).translate.y, 20.0f, 1e-4f);
  }

  // XYZ keys: the X angle halfway, as the game builds the rotation
  const VrKfTrack* finger = animation.track("Bip01 L Finger1");
  CHECK(finger != nullptr && finger->euler);

  if (finger) {
    VrGameRotation r = VrKfAnimation::sample(*finger, 1.5f).rotate;
    VrGameRotation expected = vrKfEulerRotation(0.5f, 0.0f, 0.0f);

    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        CHECK_NEAR(r.m[i][j], expected.m[i][j], 1e-5f);

    CHECK_NEAR(expected.m[1][2], std::sin(0.5f), 1e-6f);
  }
}


TEST_CASE(consumables_truncated_files_are_refused) {
  std::vector<uint8_t> file = drink().write();
  VrKfAnimation animation;

  for (size_t size : { size_t(10), file.size() / 2, file.size() - 3 })
    CHECK(!animation.read(file.data(), size) || animation.tracks().size() < 4);
}


TEST_CASE(consumables_animation_gives_prop_cap_and_sounds) {
  std::vector<uint8_t> file = drink().write();
  VrKfAnimation kf;
  CHECK(kf.read(file.data(), file.size()));

  VrConsumableAnimation animation;
  CHECK(vrReadConsumableAnimation(kf, "Bottle", animation));

  CHECK(animation.gesture == VrConsumeGesture::Mouth);
  CHECK(animation.prop == "B42Inject\\Bottle.nif");
  CHECK(animation.hasCap);
  CHECK(animation.cap.node == "##Cap");
  CHECK_NEAR(animation.capOn.translate.y, 10.0f, 1e-4f);
  CHECK(animation.parts.size() == 1 && animation.parts[0].node == "##Liquid");
  CHECK_NEAR(animation.inHand.translate.x, 5.0f, 1e-4f);
  CHECK(animation.fingers.size() == 1);

  // The cap is 2 units off at 0.9 s; use from 1.3 s to the removal
  CHECK(animation.useStart > 0.85f && animation.useStart < 1.4f);
  CHECK_NEAR(animation.useEnd, 2.9f, 1e-5f);
  CHECK_NEAR(animation.useRate, 1.0f, 1e-6f);

  CHECK(animation.drawSounds.size() == 1 && animation.drawSounds[0].path == "fx\\Hit\\Draw.wav");
  CHECK(animation.capSounds.size() == 1 && animation.capSounds[0].path == "fx\\Hit\\CapOff.wav");
  CHECK(animation.useSounds.size() == 2);
  CHECK(animation.useSounds[1].path.empty());
  CHECK_NEAR(animation.useSounds[0].time, 1.9f - animation.useStart, 1e-4f);
}


TEST_CASE(consumables_magazines_are_read_in_a_fixed_time) {
  KfWriter kf = drink();
  kf.tracks.erase(kf.tracks.begin() + 1);
  std::vector<uint8_t> file = kf.write();
  VrKfAnimation read;
  CHECK(read.read(file.data(), file.size()));

  VrConsumableAnimation animation;
  CHECK(vrReadConsumableAnimation(read, "Magazine", animation));
  CHECK(!animation.hasCap);
  CHECK(animation.gesture == VrConsumeGesture::Read);
  CHECK_NEAR(animation.useSeconds(), VrConsumeReadSeconds, 1e-4f);
}


TEST_CASE(consumables_an_animation_without_prop_is_not_b42) {
  KfWriter kf = drink();
  kf.tracks.erase(kf.tracks.begin());
  std::vector<uint8_t> file = kf.write();
  VrKfAnimation read;
  CHECK(read.read(file.data(), file.size()));

  VrConsumableAnimation animation;
  CHECK(!vrReadConsumableAnimation(read, "Bottle", animation));
}


TEST_CASE(consumables_zones_mouth_body_and_eyes) {
  VrConsumeZones zones;
  VrGameCameraPose head = northHead();
  VrVector3 farHand = fromHead(0.5f, 0.0f, -2.0f);
  float radius = 0.05f * VrGameUnitsPerMetre;

  CHECK(vrIsWhereConsumed(VrConsumeGesture::Mouth, head, fromHead(0.0f, 0.10f, -0.08f), radius, farHand, 0.0f, zones));
  CHECK(!vrIsWhereConsumed(VrConsumeGesture::Mouth, head, fromHead(0.0f, 0.40f, -0.30f), radius, farHand, 0.0f, zones));

  CHECK(vrIsWhereConsumed(VrConsumeGesture::Body, head, fromHead(0.10f, 0.15f, -0.70f), radius, farHand, 0.0f, zones));
  CHECK(vrIsWhereConsumed(VrConsumeGesture::Body, head, fromHead(0.40f, 0.40f, -0.40f), radius,
    fromHead(0.45f, 0.45f, -0.40f), 0.0f, zones));
  CHECK(!vrIsWhereConsumed(VrConsumeGesture::Body, head, fromHead(0.0f, 0.6f, 0.2f), radius, farHand, 0.0f, zones));

  CHECK(vrIsWhereConsumed(VrConsumeGesture::Read, head, fromHead(0.0f, 0.35f, -0.05f), radius, farHand, 0.0f, zones));
  CHECK(!vrIsWhereConsumed(VrConsumeGesture::Read, head, fromHead(0.35f, 0.05f, -0.05f), radius, farHand, 0.0f, zones));
}


TEST_CASE(consumables_gesture_takes_the_cap_then_counts_at_the_mouth) {
  VrConsumeZones zones;
  VrConsumeGestureState gesture;
  gesture.start(VrConsumeGesture::Mouth, true, 1.0f);

  VrConsumeInput input;
  input.head      = northHead();
  input.centre    = fromHead(0.0f, 0.10f, -0.08f);
  input.radius    = 0.05f * VrGameUnitsPerMetre;
  input.cap       = fromHead(0.2f, 0.3f, -0.3f);
  input.otherHand = fromHead(0.5f, 0.3f, -0.3f);
  input.dt        = 0.25f;

  // At the mouth with the cap on: nothing counts
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  CHECK(gesture.capOn());

  // A grip away from the cap does not take it
  input.otherGrip = true;
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  input.otherGrip = false;
  gesture.update(input, zones);

  input.otherHand = fromHead(0.22f, 0.3f, -0.3f);
  input.otherGrip = true;
  CHECK(gesture.update(input, zones) == VrConsumeEvent::CapTaken);
  CHECK(gesture.capInHand());

  input.otherGrip = false;
  CHECK(gesture.update(input, zones) == VrConsumeEvent::CapDropped);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Arrived);

  for (int i = 0; i < 3; i++)
    CHECK(gesture.update(input, zones) == VrConsumeEvent::None);

  CHECK(gesture.update(input, zones) == VrConsumeEvent::Consumed);
  CHECK(!gesture.active());
}


TEST_CASE(consumables_gesture_waits_when_the_item_leaves) {
  VrConsumeZones zones;
  VrConsumeGestureState gesture;
  gesture.start(VrConsumeGesture::Mouth, false, 0.5f);

  VrConsumeInput input;
  input.head      = northHead();
  input.centre    = fromHead(0.0f, 0.10f, -0.08f);
  input.otherHand = fromHead(0.5f, 0.3f, -0.3f);
  input.dt        = 0.2f;

  CHECK(gesture.update(input, zones) == VrConsumeEvent::Arrived);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  CHECK_NEAR(gesture.clock(), 0.2f, 1e-5f);

  input.centre = fromHead(0.0f, 0.5f, -0.6f);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Left);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  CHECK_NEAR(gesture.clock(), 0.2f, 1e-5f);

  input.centre = fromHead(0.0f, 0.10f, -0.08f);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Arrived);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Consumed);
}


TEST_CASE(consumables_gesture_keeps_the_item_without_a_button) {
  VrConsumeZones zones;
  VrConsumeGestureState gesture;
  VrConsumeInput input;
  input.head   = northHead();
  input.centre = fromHead(0.0f, 0.5f, -0.6f);
  input.dt     = 0.5f;

  // Away from the mouth with no button held, for longer than any wait
  gesture.start(VrConsumeGesture::Mouth, false, 1.0f);

  for (int i = 0; i < 40; i++)
    CHECK(gesture.update(input, zones) == VrConsumeEvent::None);

  CHECK(gesture.active());

  // Still consumed once brought to the mouth
  input.centre = fromHead(0.0f, 0.10f, -0.08f);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Arrived);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::None);
  CHECK(gesture.update(input, zones) == VrConsumeEvent::Consumed);
}
