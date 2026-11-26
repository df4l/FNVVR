#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "vr_consumable_animation.h"
#include "vr_hand_fingers.h"

namespace dxvk {

  namespace {

    struct ConsumableItem {
      const char* editorId;
      const char* file;
      uint32_t    formId;
    };

    constexpr const char* FalloutNv    = "FalloutNV.esm";
    constexpr const char* HonestHearts = "HonestHearts.esm";

    // B42 Inject's and its anim pack's folders (findings/consumables.md §6),
    // their forms read from the game's plugins
    constexpr ConsumableItem Items[] = {
      { "BlamcoMacandCheese", FalloutNv, 0x08C556 },
      { "Buffout", FalloutNv, 0x015163 },
      { "Cram", FalloutNv, 0x08C552 },
      { "DandyBoyApples", FalloutNv, 0x08C554 },
      { "FancyLadsSnackCakes", FalloutNv, 0x08C55A },
      { "InstaMash", FalloutNv, 0x08C558 },
      { "IrradiatedBlamcoMacandCheese", FalloutNv, 0x143FE6 },
      { "IrradiatedCram", FalloutNv, 0x143FE8 },
      { "IrradiatedDandyBoyApples", FalloutNv, 0x143FE9 },
      { "IrradiatedFancyLadsSnackCakes", FalloutNv, 0x143FEA },
      { "IrradiatedInstaMash", FalloutNv, 0x143FE7 },
      { "IrradiatedPotatoCrisps", FalloutNv, 0x143FED },
      { "IrradiatedSugarBombs01", FalloutNv, 0x143FF0 },
      { "IrradiatedWhiskey", FalloutNv, 0x143FE1 },
      { "IrradiatedYumYumDeviledEggs", FalloutNv, 0x143FDF },
      { "Jet", FalloutNv, 0x015164 },
      { "JetDixon", FalloutNv, 0x122B49 },
      { "JunkFood", FalloutNv, 0x03359C },
      { "MagazineNVBarter", FalloutNv, 0x0EC4A4 },
      { "MagazineNVCritical", FalloutNv, 0x135689 },
      { "MagazineNVEnergyWeapons", FalloutNv, 0x0EC4A6 },
      { "MagazineNVExplosives", FalloutNv, 0x0EC4A7 },
      { "MagazineNVGuns", FalloutNv, 0x0EC4AC },
      { "MagazineNVLockpick", FalloutNv, 0x0EC4A8 },
      { "MagazineNVMedicine", FalloutNv, 0x0EC4A9 },
      { "MagazineNVMeleeWeapons", FalloutNv, 0x0EC4AA },
      { "MagazineNVRepair", FalloutNv, 0x0EC4AB },
      { "MagazineNVScience", FalloutNv, 0x0EC4B1 },
      { "MagazineNVSneak", FalloutNv, 0x0EC4AD },
      { "MagazineNVSpeech", FalloutNv, 0x0EC4AE },
      { "MagazineNVSurvival", FalloutNv, 0x0EC4AF },
      { "MagazineNVUnarmed", FalloutNv, 0x0EC4B0 },
      { "Mentats", FalloutNv, 0x015165 },
      { "Morphine", FalloutNv, 0x050F8F },
      { "MS05IceNukaCola", FalloutNv, 0x040DCB },
      { "MS05NukaColaQtm", FalloutNv, 0x0284F9 },
      { "MS09Ultrajet", FalloutNv, 0x0C9CF4 },
      { "MS15Mentat01", FalloutNv, 0x0BF7C5 },
      { "MS15Mentat02", FalloutNv, 0x0BF7C6 },
      { "MS15Mentat03", FalloutNv, 0x0BF7C7 },
      { "NukaCola", FalloutNv, 0x01519E },
      { "NVAntivenom", FalloutNv, 0x0E2C6F },
      { "NVBitterDrink", FalloutNv, 0x1613BD },
      { "NVCateye", FalloutNv, 0x07EA1B },
      { "NVDLC02WastelandTequila", HonestHearts, 0x011111 },
      { "NVDoctorBag", FalloutNv, 0x0CB05C },
      { "NVFixer", FalloutNv, 0x0CB5A9 },
      { "NVHydra", FalloutNv, 0x13B2C3 },
      { "NVNukaColaQuartz", FalloutNv, 0x0E2F6D },
      { "NVNukaColaVictory", FalloutNv, 0x0E2F6C },
      { "NVReboundChem", FalloutNv, 0x0CEA5F },
      { "NVSteadyChem", FalloutNv, 0x0CED8C },
      { "NVSuperStimpak", FalloutNv, 0x0CCEF2 },
      { "NVTurbo", FalloutNv, 0x120D81 },
      { "PartyTimeMentats", FalloutNv, 0x146CCB },
      { "PotatoCrisps", FalloutNv, 0x08C562 },
      { "Psycho", FalloutNv, 0x015166 },
      { "RadAway", FalloutNv, 0x015167 },
      { "RadX", FalloutNv, 0x015168 },
      { "Rocket", FalloutNv, 0x146CCD },
      { "SalisburySteak", FalloutNv, 0x0293A9 },
      { "Slasher", FalloutNv, 0x146CCA },
      { "Stimpak", FalloutNv, 0x015169 },
      { "SugarBombs01", FalloutNv, 0x08C551 },
      { "Whiskey", FalloutNv, 0x032C75 },
      { "WhiskeyDixon", FalloutNv, 0x122B4A },
      { "YumYumDeviledEggs", FalloutNv, 0x08C55C },
    };

    struct GestureEntry {
      const char*      name;
      VrConsumeGesture gesture;
    };

    // The animations that are not taken to the mouth
    constexpr GestureEntry Gestures[] = {
      { "Stimpak",      VrConsumeGesture::Body },
      { "SuperStimpak", VrConsumeGesture::Body },
      { "MedX",         VrConsumeGesture::Body },
      { "Psycho",       VrConsumeGesture::Body },
      { "Fixer",        VrConsumeGesture::Body },
      { "RadAway",      VrConsumeGesture::Body },
      { "Magazine",     VrConsumeGesture::Read },
      { "DoctorBag",    VrConsumeGesture::Instant },
    };

    // The prop's node B42 Inject inserts below the left hand
    constexpr const char* AttachNode = "B42AttachPointL";

    // The suffix JIP's AttachModel gives the prop's nodes
    constexpr const char* PartSuffix = "_B42";

    // A sound played at the item's appearance comes with its attachment
    constexpr float DrawSlack = 0.05f;

    // The cap is off once it is this far from its place, in game units
    constexpr float CapOffDistance = 2.0f;

    // Step of the search for the cap's removal
    constexpr float CapSearchStep = 1.0f / 30.0f;

    // The shortest time the item stays where it is consumed
    constexpr float ShortestUse = 0.3f;

    bool equalNoCase(const char* a, const char* b) {
      for (; *a && *b; a++, b++) {
        if (std::tolower(uint8_t(*a)) != std::tolower(uint8_t(*b)))
          return false;
      }

      return *a == *b;
    }

    std::string lower(std::string text) {
      for (char& c : text)
        c = char(std::tolower(uint8_t(c)));
      return text;
    }

    // The value a ScriptLine key gives an aux variable: B42 Inject's keys
    // are `... "*_<variable>" <value>`, the value quoted for a string
    bool keyValue(const std::string& text, const char* variable, std::string& value) {
      std::string pattern = lower(std::string("_") + variable + "\"");
      size_t at = lower(text).find(pattern);

      if (at == std::string::npos)
        return false;

      size_t rest = at + pattern.size();

      while (rest < text.size() && text[rest] == ' ')
        rest++;

      if (rest < text.size() && text[rest] == '"') {
        size_t end = text.find('"', rest + 1);
        value = text.substr(rest + 1, end == std::string::npos ? std::string::npos : end - rest - 1);
      } else {
        size_t end = text.find_first_of(" \t", rest);
        value = text.substr(rest, end == std::string::npos ? std::string::npos : end - rest);
      }

      return true;
    }

    float distance(const VrVector3& a, const VrVector3& b) {
      float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
      return std::sqrt(x * x + y * y + z * z);
    }

    bool isPart(const std::string& name) {
      size_t suffix = std::strlen(PartSuffix);
      return name.size() > 2 + suffix && name.compare(0, 2, "##") == 0
        && name.compare(name.size() - suffix, suffix, PartSuffix) == 0;
    }

    bool isCap(const std::string& name) {
      return lower(name).find("cap") != std::string::npos;
    }

    bool isSpit(const std::string& path) {
      return lower(path).find("spit") != std::string::npos;
    }

  }


  const char* vrConsumableEditorId(const char* file, uint32_t formId) {
    if (!file)
      return nullptr;

    for (const ConsumableItem& item : Items) {
      if (item.formId == (formId & 0xFFFFFF) && equalNoCase(item.file, file))
        return item.editorId;
    }

    return nullptr;
  }


  VrConsumeGesture vrConsumeGestureOf(const std::string& name) {
    for (const GestureEntry& entry : Gestures) {
      if (equalNoCase(entry.name, name.c_str()))
        return entry.gesture;
    }

    return VrConsumeGesture::Mouth;
  }


  bool vrReadConsumableAnimation(
    const VrKfAnimation&          animation,
    const std::string&            name,
          VrConsumableAnimation&  result) {
    result = VrConsumableAnimation();
    result.gesture = vrConsumeGestureOf(name);

    const VrKfTrack* attach = animation.track(AttachNode);

    if (!attach)
      return false;

    // The keys: the prop, when it is in the hand and gone, the sounds
    struct SoundKey {
      float       time;
      std::string path;
    };

    std::vector<SoundKey> sounds;
    float attachTime = -1.0f;
    float detachTime = animation.length();

    for (const VrKfTextKey& key : animation.textKeys()) {
      std::string value;

      if (keyValue(key.text, "InjMP", value)) {
        result.prop = value;
      } else if (keyValue(key.text, "InjM", value)) {
        int mode = std::atoi(value.c_str());

        if (mode == 1 && attachTime < 0.0f)
          attachTime = key.time;
        else if (mode == 3)
          detachTime = std::min(detachTime, key.time);
      } else if (keyValue(key.text, "InJSP", value)) {
        sounds.push_back({ key.time, value });
      } else if (keyValue(key.text, "InJS", value)) {
        if (std::atoi(value.c_str()) == 1)
          sounds.push_back({ key.time, std::string() });
      }
    }

    if (attachTime < 0.0f || detachTime <= attachTime)
      return false;

    // The parts, and the cap among them
    for (const VrKfTrack& track : animation.tracks()) {
      if (!isPart(track.name))
        continue;

      VrConsumablePart part;
      part.node  = track.name.substr(0, track.name.size() - std::strlen(PartSuffix));
      part.track = track;

      if (!result.hasCap && isCap(part.node)) {
        result.hasCap = true;
        result.cap    = std::move(part);
      } else {
        result.parts.push_back(std::move(part));
      }
    }

    // The cap is off when it has left its place; a cap that stays on is
    // only a part
    float capOff = attachTime;

    if (result.hasCap) {
      result.capOn = VrKfAnimation::sample(result.cap.track, attachTime);
      bool off = false;

      for (float t = attachTime; t <= detachTime && !off; t += CapSearchStep) {
        VrSequencePose pose = VrKfAnimation::sample(result.cap.track, t);

        if (distance(pose.translate, result.capOn.translate) > CapOffDistance) {
          capOff = t;
          off    = true;
        }
      }

      if (!off) {
        result.hasCap = false;
        result.parts.push_back(std::move(result.cap));
        result.cap = VrConsumablePart();
        capOff = attachTime;
      }
    }

    // The use: from the item's arrival where it is consumed to its
    // removal; a magazine is read in a fixed time
    float ready = result.hasCap ? capOff : attachTime;
    result.useEnd   = detachTime;
    result.useStart = std::max(ready, std::min(ready + VrConsumeBringUpSeconds, detachTime - ShortestUse));
    result.useRate  = result.gesture == VrConsumeGesture::Read
      ? (result.useEnd - result.useStart) / VrConsumeReadSeconds
      : 1.0f;

    if (result.useRate <= 0.0f)
      result.useRate = 1.0f;

    // The hand holds the prop as it does while it is used
    float hold = 0.5f * (result.useStart + result.useEnd);
    result.inHand = VrKfAnimation::sample(*attach, hold);

    for (const VrKfTrack& track : animation.tracks()) {
      VrHand hand;
      uint32_t index;

      if (vrFindFingerBone(track.name.c_str(), hand, index) && hand == VrHand::Left && track.hasRotation)
        result.fingers.push_back({ track.name, VrKfAnimation::sample(track, hold).rotate });
    }

    // The sounds by group
    float firstDraw = -1.0f;
    float firstCap  = -1.0f;

    for (const SoundKey& sound : sounds) {
      // A cork pulled with the teeth is spat out; the right hand takes it
      // off instead
      if (result.hasCap && isSpit(sound.path))
        continue;

      if (sound.time <= attachTime + DrawSlack) {
        if (firstDraw < 0.0f)
          firstDraw = sound.time;
        result.drawSounds.push_back({ sound.time - firstDraw, sound.path });
      } else if (result.hasCap && sound.time < capOff + DrawSlack) {
        if (firstCap < 0.0f)
          firstCap = sound.time;
        result.capSounds.push_back({ sound.time - firstCap, sound.path });
      } else {
        float time = std::max(0.0f, (sound.time - result.useStart) / result.useRate);
        result.useSounds.push_back({ time, sound.path });
      }
    }

    return true;
  }

}
