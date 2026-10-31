#include <algorithm>
#include <cfloat>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_nodes.h"
#include "vr_game_sequence.h"
#include "vr_game_sound.h"
#include "vr_magazine.h"
#include "vr_reload_animation.h"
#include "vr_sound_keys.h"

namespace dxvk {

  namespace {

    // ItemChange::HasModEffectActive, __thiscall with the effect, returns a
    // bool in AL
    using HasModEffectFn = uint32_t (__fastcall*)(uint8_t* entry, void* unused, uint32_t effect);

    // TESObjectWEAP::GetReloadAnim, __thiscall with a bool
    using GetReloadAnimFn = uint32_t (__fastcall*)(const uint8_t* weapon, void* unused, uint32_t extended);

    // How many times per second of the animation the magazine is sampled
    constexpr float SamplesPerSecond = 60.0f;

    // How far from the magazine's times its sounds may be, in seconds of
    // the animation
    constexpr float SoundWindow = 0.4f;

    // How near the left hand bone the rack part's centre must be for the
    // animation's hand to hold it, in metres
    constexpr float RackHeldReach = 0.15f;

    // How far a break action's hinge part must turn for the animation to
    // open it, in radians (10 degrees)
    constexpr float MinHingeAngle = 0.1745f;

    // Part of its turn by which the hinge part has started opening, or is
    // closed again
    constexpr float HingeMoved = 0.1f;

    // How long a loading loop with no "Hit" key goes on after its round
    // is in its place, in seconds
    constexpr float LoopAfterPlace = 0.2f;

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    VrVector3 readVector(const uint8_t* object, uintptr_t offset) {
      VrVector3 v;
      std::memcpy(&v, object + offset, sizeof(v));
      return v;
    }

    VrGameRotation readRotation(const uint8_t* object, uintptr_t offset) {
      VrGameRotation r;
      std::memcpy(r.m, object + offset, sizeof(r.m));
      return r;
    }

    const char* objectName(const uint8_t* object) {
      return readField<const char*>(object, VrGame::ObjectName);
    }

    const char* partName(const uint8_t* part) {
      const char* name = objectName(part);
      return name ? name : "";
    }

    /**
     * \brief The first-person reload sequence the game would play
     */
    const uint8_t* findReloadSequence(uint8_t* player) {
      uint8_t* entry = vrActorWeaponEntry(player);
      auto weapon    = entry ? readField<const uint8_t*>(entry, VrGame::InventoryEntryForm) : nullptr;

      if (!weapon)
        return nullptr;

      uint32_t extended = reinterpret_cast<HasModEffectFn>(VrGame::ItemHasModEffect)(
        entry, nullptr, VrGame::ModEffectClipSize) & 0xFF;
      uint32_t reload = reinterpret_cast<GetReloadAnimFn>(VrGame::WeaponGetReloadAnim)(
        weapon, nullptr, extended) & 0xFF;
      return vrFindPlayerSequence(player, reload);
    }


    /**
     * \brief The centre of a part's meshes in its own frame
     */
    VrVector3 meshCentre(const uint8_t* part) {
      float scale = readField<float>(part, VrGame::ObjectWorldScale);
      auto bound  = readField<const uint8_t*>(part, VrGame::ObjectWorldBound);

      // A part the game hides, such as a round, may have no bound
      if (!bound || scale <= 0.0f || readField<float>(bound, sizeof(VrVector3)) <= 0.0f)
        return VrVector3();

      return vrTranspose(readRotation(part, VrGame::ObjectWorldRotation))
        * (readVector(bound, 0) - readVector(part, VrGame::ObjectWorldTranslation)) * (1.0f / scale);
    }


    /**
     * \brief Reads how the sequence pulls the rack part back
     *
     * \param [out] from When it starts moving
     * \param [out] to When it is home again
     */
    bool readRack(
      const VrSequenceTracks&       tracks,
            uint8_t*              root,
            uint8_t*              weapon,
            uint8_t*              rack,
            uint8_t*              leftHand,
            float                 begin,
            float                 end,
            VrRackAnimation&      animation,
            float&                from,
            float&                to) {
      std::vector<VrMagazineSample> samples;
      std::vector<VrSequencePose> poses;

      for (uint32_t i = 0; begin + float(i) / SamplesPerSecond <= end; i++) {
        float t = begin + float(i) / SamplesPerSecond;
        VrSequencePose pose;

        if (!tracks.toAncestor(rack, weapon, t, pose))
          return false;

        VrVector3 offset = poses.empty() ? VrVector3() : (pose.translate - poses.front().translate);
        samples.push_back({ t, offset * (1.0f / VrGameUnitsPerMetre) });
        poses.push_back(pose);
      }

      VrRackMotion motion;

      if (!vrFindRackMotion(samples.data(), samples.size(), motion))
        return false;

      animation.axis   = motion.axis;
      animation.travel = motion.travel;
      animation.poses.clear();

      float along = 0.0f;

      for (size_t i = motion.start; i <= motion.back; i++) {
        along = std::max(along, vrDot(samples[i].offset, motion.axis));

        VrRackPose pose;
        pose.along          = along;
        pose.pose.rotate    = poses[i].rotate;
        pose.pose.translate = poses[i].translate;
        animation.poses.push_back(pose);
      }

      // The left hand holds the part all the way back if it is at its
      // centre then; the right hand may rack some weapons
      float time = samples[motion.back].time;
      from = samples[motion.start].time;
      to   = samples[motion.home].time;

      VrSequencePose hand, part;
      animation.held = false;

      if (tracks.toAncestor(leftHand, root, time, hand) && tracks.toAncestor(rack, root, time, part)
       && hand.scale > 0.0f) {
        VrVector3 middle = part.translate + part.rotate * meshCentre(rack) * part.scale;
        float distance   = vrLength(middle - hand.translate) / VrGameUnitsPerMetre;

        VrGameRotation toHand = vrTranspose(hand.rotate);
        animation.held                 = distance <= RackHeldReach;
        animation.partInHand.rotate    = toHand * part.rotate;
        animation.partInHand.translate = (toHand * (part.translate - hand.translate)) * (1.0f / hand.scale);
        animation.fingers              = animation.held ? vrReadSequenceFingers(tracks, leftHand, time) : std::vector<VrBonePose>();

        Logger::info(str::format("VR: Reload animation racks '", partName(rack), "' ", motion.travel * 100.0f,
          " cm back along ", motion.axis.x, ", ", motion.axis.y, ", ", motion.axis.z, " in the weapon, from ",
          from, " to ", time, " s, home at ", to, " s; its centre is then ", distance * 100.0f,
          " cm from the left hand bone", animation.held ? ", which holds it" : ", which does not hold it"));
      }

      return true;
    }


    /**
     * \brief A node's transform in one of its ancestors' frame, as the
     *    model has it now
     */
    bool restToAncestor(uint8_t* node, const uint8_t* ancestor, VrSequencePose& pose) {
      pose = VrSequencePose();

      for (; node && node != ancestor; node = readField<uint8_t*>(node, VrGame::ObjectParent)) {
        VrSequencePose local;
        local.rotate    = readRotation(node, VrGame::ObjectLocalRotation);
        local.translate = readVector(node, VrGame::NodeLocalTranslate);
        local.scale     = readField<float>(node, VrGame::ObjectLocalScale);
        pose = vrComposeSequencePose(local, pose);
      }

      return node == ancestor;
    }


    /**
     * \brief Reads how the sequence opens the action about a part
     *
     * The part turns away from where it is closed: where the model has it
     * now, or where the sequence has it at its start. The grenade
     * machinegun's model has its lock open, the reload animation starts
     * with it shut.
     *
     * \param [in] closedAtStart The part is closed at the sequence's start
     * \param [in] placed When the shells are in their place
     * \param [out] widest How far it turns at most
     * \param [out] from When the part starts turning
     * \param [out] to When it is closed again, or a negative time if it
     *    stays open
     */
    bool readHinge(
      const VrSequenceTracks&       tracks,
            uint8_t*              weapon,
            uint8_t*              hinge,
            float                 begin,
            float                 end,
            bool                  closedAtStart,
            float                 placed,
            VrHingeAnimation&     animation,
            float&                widest,
            float&                from,
            float&                to) {
      VrSequencePose closed;

      if (closedAtStart ? !tracks.toAncestor(hinge, weapon, begin, closed) : !restToAncestor(hinge, weapon, closed))
        return false;

      std::vector<float> times;
      std::vector<VrSequencePose> poses;

      for (uint32_t i = 0; begin + float(i) / SamplesPerSecond <= end; i++) {
        float t = begin + float(i) / SamplesPerSecond;
        VrSequencePose pose;

        if (!tracks.toAncestor(hinge, weapon, t, pose))
          return false;

        times.push_back(t);
        poses.push_back(pose);
      }

      std::vector<float> angles;
      widest = 0.0f;

      for (const VrSequencePose& pose : poses) {
        angles.push_back(vrRotationAngle(closed.rotate, pose.rotate));
        widest = std::max(widest, angles.back());
      }

      if (widest < MinHingeAngle)
        return false;

      // Open as it is when the shells are in their place, or as wide as it
      // goes if it is not open then
      size_t widestAt = 0;
      size_t placedAt = 0;
      from = -1.0f;
      to   = -1.0f;

      for (size_t i = 0; i < poses.size(); i++) {
        if (from < 0.0f && angles[i] > HingeMoved * widest)
          from = times[i];

        if (angles[i] > angles[widestAt])
          widestAt = i;

        if (times[i] <= placed)
          placedAt = i;

        if (to < 0.0f && times[i] > placed && from >= 0.0f && angles[i] <= HingeMoved * widest)
          to = times[i];
      }

      size_t open = angles[placedAt] >= 0.5f * widest ? placedAt : widestAt;

      animation.part             = partName(hinge);
      animation.closed.rotate    = closed.rotate;
      animation.closed.translate = closed.translate;
      animation.open.rotate      = poses[open].rotate;
      animation.open.translate   = poses[open].translate;
      animation.angle            = angles[open];

      // The axis of the turn from closed to open, the way it opens
      VrQuaternion turn = vrQuaternionFromGameRotation(poses[open].rotate * vrTranspose(closed.rotate));
      VrVector3 axis = { turn.x, turn.y, turn.z };
      float length = vrLength(axis);
      animation.axis = length > 1e-6f ? axis * ((turn.w < 0.0f ? -1.0f : 1.0f) / length) : VrVector3();
      return true;
    }


    /**
     * \brief Reads how the sequence swings a revolver's cylinder out
     *
     * Of the round's ancestors below the weapon node, the one the sequence
     * turns furthest.
     */
    bool readSwing(
      const VrSequenceTracks&       tracks,
            uint8_t*              weapon,
            uint8_t*              round,
            float                 begin,
            float                 end,
            float                 placed,
            VrHingeAnimation&     animation,
            float&                from,
            float&                to) {
      float widest = 0.0f;
      bool found = false;

      for (uint8_t* part = readField<uint8_t*>(round, VrGame::ObjectParent); part && part != weapon;
          part = readField<uint8_t*>(part, VrGame::ObjectParent)) {
        VrHingeAnimation candidate;
        float candidateWidest = 0.0f;
        float candidateFrom   = 0.0f;
        float candidateTo     = -1.0f;

        if (!readHinge(tracks, weapon, part, begin, end, false, placed, candidate, candidateWidest, candidateFrom, candidateTo)
         || candidateWidest <= widest)
          continue;

        animation = candidate;
        widest    = candidateWidest;
        from      = candidateFrom;
        to        = candidateTo;
        found     = true;
      }

      return found;
    }


    /**
     * \brief When the sequence's "Hit" key is
     * \returns The time, or a negative one if it has none
     */
    float findHitKey(const uint8_t* sequence) {
      auto textKeys  = readField<const uint8_t*>(sequence, VrGame::SequenceTextKeys);
      auto keys      = textKeys ? readField<const uint8_t*>(textKeys, VrGame::TextKeyArray) : nullptr;
      uint32_t count = textKeys ? readField<uint32_t>(textKeys, VrGame::TextKeyCount) : 0;

      for (uint32_t i = 0; keys && i < count; i++) {
        const uint8_t* key = keys + i * VrGame::TextKeySize;

        if (vrIsHitKey(readField<const char*>(key, VrGame::TextKeyText)))
          return readField<float>(key, 0);
      }

      return -1.0f;
    }


    /**
     * \brief Reads the loop of the sequence that loads one round
     *
     * From the start of the sequence to its "Hit" key.
     *
     * \param [in] placed When the round is in its place
     */
    void readLoop(
      const uint8_t*              sequence,
            float                 begin,
            float                 end,
            float                 placed,
            VrRoundLoop&          loop) {
      float hit = findHitKey(sequence);

      loop.timing.placed = placed - begin;
      loop.timing.length = hit > placed
        ? hit - begin
        : std::min(end - begin, loop.timing.placed + LoopAfterPlace);

      Logger::info(str::format("VR: Reload animation loads one round per loop of ", loop.timing.length, " s",
        hit > placed ? " (to its Hit key)" : " (it has no Hit key after the round's place)",
        ", the round in its place at ", loop.timing.placed, " s"));
    }


    void readSounds(const uint8_t* sequence, const VrMagazineTiming& timing, float rackFrom, float rackTo,
        float hingeFrom, float hingeTo, float lockFrom, float lockTo, float begin, VrReloadAnimation& animation) {
      auto group = readField<const uint8_t*>(sequence, VrGame::AnimSequenceGroup);

      if (!group)
        return;

      uint32_t count = readField<uint32_t>(group, VrGame::AnimGroupSoundCount);
      auto sounds    = readField<const uint8_t*>(group, VrGame::AnimGroupSounds);

      std::vector<float> times;
      std::vector<const uint8_t*> entries;

      for (uint32_t i = 0; sounds && i < count; i++) {
        const uint8_t* entry = sounds + i * VrGame::AnimGroupSoundSize;

        // Entries without a sound are footsteps and other events
        if (readField<const uint8_t*>(entry, VrGame::AnimGroupSoundForm)) {
          times.push_back(readField<float>(entry, 0));
          entries.push_back(entry);
        }
      }

      int32_t out = animation.round ? -1
        : vrFindNearestSound(times.data(), times.size(), timing.out, SoundWindow, -1);
      int32_t in  = vrFindNearestSound(times.data(), times.size(), timing.in,  SoundWindow, out);

      auto toSound = [&entries] (int32_t index) {
        VrAnimationSound sound;

        if (index >= 0) {
          sound.sound  = readField<const uint8_t*>(entries[index], VrGame::AnimGroupSoundForm);
          sound.volume = float(readField<uint8_t>(entries[index], VrGame::AnimGroupSoundVolume)) / 255.0f;
          sound.pitch  = readField<float>(entries[index], VrGame::AnimGroupSoundPitch);
        }

        return sound;
      };

      // The rack's sound is neither of the magazine's; it is played from
      // the time the part leaves its place to the time it is home again
      std::vector<float> others = times;

      for (int32_t index : { out, in }) {
        if (index >= 0)
          others[index] = FLT_MAX;
      }

      int32_t rack = animation.hasRack
        ? vrFindSoundDuring(others.data(), others.size(), rackFrom, rackTo, SoundWindow, -1)
        : -1;

      // The action opening and closing are neither, nor each other
      int32_t opening = animation.hasHinge
        ? vrFindNearestSound(others.data(), others.size(), hingeFrom, SoundWindow, -1)
        : -1;
      int32_t closing = animation.hasHinge && hingeTo >= 0.0f
        ? vrFindNearestSound(others.data(), others.size(), hingeTo, SoundWindow, opening)
        : -1;

      // The lock over the magazine opening and shutting
      int32_t unlocking = animation.hasLock
        ? vrFindNearestSound(others.data(), others.size(), lockFrom, SoundWindow, -1)
        : -1;
      int32_t locking = animation.hasLock && lockTo >= 0.0f
        ? vrFindNearestSound(others.data(), others.size(), lockTo, SoundWindow, unlocking)
        : -1;

      animation.out = toSound(out);
      animation.in  = toSound(in);

      if (animation.hasRack)
        animation.rack.sound = toSound(rack);

      if (animation.hasHinge) {
        animation.hinge.openSound  = toSound(opening);
        animation.hinge.closeSound = toSound(closing);
      }

      if (animation.hasLock) {
        animation.lock.openSound  = toSound(unlocking);
        animation.lock.closeSound = toSound(locking);
      }

      // Every sound of the loop plays in each loop, as the game plays them
      if (animation.hasLoop) {
        animation.loop.sounds.clear();

        for (size_t i = 0; i < entries.size(); i++) {
          if (times[i] - begin < animation.loop.timing.length)
            animation.loop.sounds.push_back({ times[i] - begin, toSound(int32_t(i)) });
        }

        std::sort(animation.loop.sounds.begin(), animation.loop.sounds.end(),
          [] (const VrRoundLoopSound& a, const VrRoundLoopSound& b) { return a.time < b.time; });
      }

      for (size_t i = 0; i < entries.size(); i++) {
        Logger::info(str::format("VR: Reload animation sound at ", times[i], " s: '",
          vrGameSoundFile(readField<const uint8_t*>(entries[i], VrGame::AnimGroupSoundForm)), "'",
          int32_t(i) == out  ? ", the magazine going out" : "",
          int32_t(i) == in   ? (animation.round ? ", the round going in" : ", the magazine going in") : "",
          int32_t(i) == rack ? ", the weapon chambering" : "",
          int32_t(i) == opening ? ", the action opening" : "",
          int32_t(i) == closing ? ", the action closing" : "",
          int32_t(i) == unlocking ? ", the lock opening" : "",
          int32_t(i) == locking ? ", the lock shutting" : "",
          animation.hasLoop && times[i] - begin < animation.loop.timing.length ? ", in each loop" : ""));
      }
    }

  }


  bool vrReadReloadAnimation(
          uint8_t*              player,
          uint8_t*              root,
          uint8_t*              weapon,
          uint8_t*              magazine,
          bool                  round,
          bool                  looping,
          uint8_t*              rack,
          uint8_t*              hinge,
          bool                  swing,
          uint8_t*              lock,
          uint8_t*              leftHand,
          VrReloadAnimation&    animation) {
    const uint8_t* sequence = findReloadSequence(player);

    if (!sequence) {
      Logger::info("VR: The weapon has no first-person reload animation");
      return false;
    }

    auto sequenceName = readField<const char*>(sequence, VrGame::ObjectName);
    float begin = readField<float>(sequence, VrGame::SequenceBeginKeyTime);
    float end   = readField<float>(sequence, VrGame::SequenceEndKeyTime);

    VrSequenceTracks tracks(sequence);

    // The magazine in the weapon's frame through the animation, from where
    // it is at the start
    std::vector<VrMagazineSample> samples;
    std::vector<VrSequencePose> poses;

    for (uint32_t i = 0; begin + float(i) / SamplesPerSecond <= end; i++) {
      float time = begin + float(i) / SamplesPerSecond;
      VrSequencePose pose;

      if (!tracks.toAncestor(magazine, weapon, time, pose))
        return false;

      VrVector3 offset = poses.empty() ? VrVector3() : (pose.translate - poses.front().translate);
      samples.push_back({ time, offset * (1.0f / VrGameUnitsPerMetre) });
      poses.push_back(pose);
    }

    VrMagazineTiming timing;
    size_t place = 0;

    animation.round = round;

    if (round ? !vrFindRoundTiming(samples.data(), samples.size(), timing, place)
              : !vrFindMagazineTiming(samples.data(), samples.size(), timing)) {
      Logger::info(str::format("VR: The reload animation '", sequenceName ? sequenceName : "",
        round ? "' does not carry the round in" : "' does not take the magazine out and put it back"));
      return false;
    }

    // A magazine is left in its place at the end
    if (!round)
      place = poses.size() - 1;

    animation.place.rotate    = poses[place].rotate;
    animation.place.translate = poses[place].translate;

    // The centre of the magazine's meshes in its own frame
    VrVector3 centre = meshCentre(magazine);

    // The magazine in the left hand's frame while it is out, to find when
    // the hand carries it
    std::vector<VrGameTransform> inHand;
    std::vector<float> inHandTimes;

    for (const VrMagazineSample& sample : samples) {
      VrSequencePose hand, held;

      if (sample.time < timing.out || sample.time >= timing.in
       || !tracks.toAncestor(leftHand, root, sample.time, hand)
       || !tracks.toAncestor(magazine, root, sample.time, held))
        continue;

      VrGameRotation toHand = vrTranspose(hand.rotate);
      VrVector3 middle = held.translate + held.rotate * centre * held.scale;

      VrGameTransform transform;
      transform.rotate    = toHand * held.rotate;
      transform.translate = toHand * (middle - hand.translate) * (1.0f / VrGameUnitsPerMetre);

      // What the hand does through the animation, for findings/reload.md
      if (!inHand.empty() && !(inHand.size() % 3)) {
        const VrGameTransform& last = inHand.back();
        Logger::info(str::format("VR: Reload animation at ", sample.time, " s: the ", round ? "round " : "magazine ",
          vrLength(sample.offset) * 100.0f, " cm out, its centre ", vrLength(transform.translate) * 100.0f,
          " cm from the left hand bone, moving ", vrLength(transform.translate - last.translate) * 100.0f,
          " cm and ", vrRotationAngle(last.rotate, transform.rotate) * 57.2957795f, " degrees in the hand"));
      }

      inHand.push_back(transform);
      inHandTimes.push_back(sample.time);
    }

    size_t carried = vrFindMagazineHold(inHand.data(), inHand.size());

    if (carried < inHand.size())
      timing.grip = inHandTimes[carried];

    animation.carried = carried < inHand.size();

    // The magazine in the left hand's frame while the hand holds it
    VrSequencePose hand, held;

    if (!tracks.toAncestor(leftHand, root, timing.grip, hand)
     || !tracks.toAncestor(magazine, root, timing.grip, held)
     || hand.scale <= 0.0f)
      return false;

    VrGameRotation toHand = vrTranspose(hand.rotate);
    animation.magazineInHand.rotate    = toHand * held.rotate;
    animation.magazineInHand.translate = (toHand * (held.translate - hand.translate)) * (1.0f / hand.scale);
    animation.handDistance = vrLength(held.translate + held.rotate * centre * held.scale - hand.translate)
      * (1.0f / VrGameUnitsPerMetre);
    animation.axis = timing.axis;

    // The fingers the animation moves
    animation.fingers = vrReadSequenceFingers(tracks, leftHand, timing.grip);

    float rackFrom = 0.0f;
    float rackTo   = 0.0f;
    animation.hasRack = rack && readRack(tracks, root, weapon, rack, leftHand, begin, end, animation.rack, rackFrom, rackTo);

    if (rack && !animation.hasRack)
      Logger::info(str::format("VR: The reload animation does not rack '", partName(rack), "'"));

    float hingeFrom   = 0.0f;
    float hingeTo     = -1.0f;
    float hingeWidest = 0.0f;

    if (hinge)
      animation.hasHinge = readHinge(tracks, weapon, hinge, begin, end, false, timing.in, animation.hinge, hingeWidest, hingeFrom, hingeTo);
    else if (swing)
      animation.hasHinge = readSwing(tracks, weapon, magazine, begin, end, timing.in, animation.hinge, hingeFrom, hingeTo);
    else
      animation.hasHinge = false;

    const VrHingeAnimation& opened = animation.hinge;

    if (animation.hasHinge) {
      Logger::info(str::format(swing ? "VR: Reload animation swings '" : "VR: Reload animation breaks the action open about '",
        opened.part, swing ? "' open by " : "' by ", opened.angle * 57.2957795f, " degrees about ",
        opened.axis.x, ", ", opened.axis.y, ", ", opened.axis.z, " in the weapon, from ", hingeFrom, " s",
        hingeTo >= 0.0f ? str::format(", closed again at ", hingeTo, " s") : std::string(", open to the end")));
    } else if (hinge || swing) {
      Logger::info(str::format(swing ? "VR: The reload animation does not swing open '" : "VR: The reload animation does not break the action open about '",
        hinge ? partName(hinge) : "any part holding the round", "'"));
    }

    float lockFrom   = 0.0f;
    float lockTo     = -1.0f;
    float lockWidest = 0.0f;
    animation.hasLock = lock && readHinge(tracks, weapon, lock, begin, end, true, timing.in, animation.lock, lockWidest, lockFrom, lockTo);

    if (animation.hasLock) {
      Logger::info(str::format("VR: Reload animation turns the lock '", animation.lock.part, "' open by ",
        animation.lock.angle * 57.2957795f, " degrees about ", animation.lock.axis.x, ", ", animation.lock.axis.y, ", ",
        animation.lock.axis.z, " in the weapon, from ", lockFrom, " s",
        lockTo >= 0.0f ? str::format(", shut again at ", lockTo, " s") : std::string(", open to the end")));
    } else if (lock) {
      Logger::info(str::format("VR: The reload animation does not turn the lock '", partName(lock), "'"));
    }

    animation.hasLoop = round && looping;

    if (animation.hasLoop)
      readLoop(sequence, begin, end, timing.in, animation.loop);

    readSounds(sequence, timing, rackFrom, rackTo, hingeFrom, hingeTo, lockFrom, lockTo, begin, animation);

    Logger::info(str::format("VR: Reload animation '", sequenceName ? sequenceName : "", "' from ", begin, " to ", end,
      round ? " s: the round is carried in from " : " s: the magazine goes out at ", timing.out, " s and in at ", timing.in, " s, along ",
      timing.axis.x, ", ", timing.axis.y, ", ", timing.axis.z, " in the weapon; the left hand holds it at ", timing.grip,
      carried < inHand.size() ? " s, where it carries it" : " s, before it goes in (the hand never carries it)",
      ", ", animation.magazineInHand.translate.x, ", ", animation.magazineInHand.translate.y, ", ",
      animation.magazineInHand.translate.z, " units from the hand bone (its centre ", animation.handDistance * 100.0f,
      " cm away), with ", animation.fingers.size(), " finger bone(s)"));
    return true;
  }


  void vrPlayReloadSound(
    const VrAnimationSound&     sound,
    const VrVector3&            position) {
    auto player = *reinterpret_cast<uint8_t* const*>(VrGame::Player);

    if (!player)
      return;

    // As fast as the reload animation would play
    auto animation = readField<const uint8_t*>(player, VrGame::PlayerFirstPersonAnimData);
    float speed = animation ? readField<float>(animation, VrGame::AnimDataReloadSpeed) : 1.0f;

    vrPlayFirstPersonSound(sound, position, speed);
  }

}
