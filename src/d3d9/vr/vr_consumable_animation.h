#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_kf_reader.h"

namespace dxvk {

  /**
   * \brief Where an item is taken to be consumed, see findings/consumables.md §6
   */
  enum class VrConsumeGesture {
    /// Eaten, drunk or inhaled: to the mouth
    Mouth,
    /// A needle: into the body, the other arm or the thigh
    Body,
    /// A magazine: held in front of the eyes
    Read,
    /// Not a gesture: used at once (the doctor's bag)
    Instant,
  };

  /**
   * \brief The editor ID of one of the items B42 Inject animates
   *
   * The game keeps no editor ID for an ingestible: B42 Inject's folders are
   * named after them, so the items are known by their file and form ID.
   *
   * \param [in] file The plugin file that defines the item, such as
   *    "FalloutNV.esm", in any case
   * \param [in] formId The item's form ID without its load order index
   * \returns The editor ID, or \c nullptr for another item
   */
  const char* vrConsumableEditorId(const char* file, uint32_t formId);

  /**
   * \brief The gesture of an animation, by its file name
   *
   * \param [in] name The animation file's name, without its folder and
   *    extension, such as "NukaCola"
   * \returns VrConsumeGesture::Mouth for an animation not in the table
   */
  VrConsumeGesture vrConsumeGestureOf(const std::string& name);

  /**
   * \brief A sound of a consumption's animation
   */
  struct VrConsumableSound {
    /// Seconds: after the first sound of its group, or on the use clock
    float       time = 0.0f;
    /// The sound file, from the sound folder; empty for the item's own
    /// consume sound
    std::string path;
  };

  /**
   * \brief A part of the prop the animation moves (cap, pill, liquid)
   */
  struct VrConsumablePart {
    /// The node's name in the prop's model
    std::string node;
    VrKfTrack   track;
  };

  /**
   * \brief What a B42 Inject animation gives the gesture
   *
   * Times are the animation's, in seconds. The use clock runs while the
   * item is where it is consumed; it covers the animation from useStart to
   * useEnd at useRate times the animation's speed.
   */
  struct VrConsumableAnimation {
    VrConsumeGesture               gesture = VrConsumeGesture::Mouth;
    /// The prop's model, from the meshes folder; empty for the item's own
    std::string                    prop;
    /// The prop in the left hand bone's frame
    VrSequencePose                 inHand;
    /// The rotations of the bones below the left hand holding it
    std::vector<VrBonePose>        fingers;
    /// The cap, lid or cork the other hand takes off, if it has one, at
    /// its place on the prop
    bool                           hasCap = false;
    VrConsumablePart               cap;
    VrSequencePose                 capOn;
    /// The other parts, moved on the use clock
    std::vector<VrConsumablePart>  parts;
    /// Played when the item comes into the hand, when the cap is taken,
    /// and on the use clock
    std::vector<VrConsumableSound> drawSounds;
    std::vector<VrConsumableSound> capSounds;
    std::vector<VrConsumableSound> useSounds;
    float                          useStart = 0.0f;
    float                          useEnd   = 0.0f;
    float                          useRate  = 1.0f;

    /// Seconds the item has to stay where it is consumed
    float useSeconds() const {
      return (useEnd - useStart) / useRate;
    }

    /// The animation's time after some seconds on the use clock
    float useTime(float seconds) const {
      return useStart + seconds * useRate;
    }
  };

  /**
   * \brief Time the player takes to bring the item to the mouth or the
   *    body, cut from the animation's start of use
   */
  constexpr float VrConsumeBringUpSeconds = 0.4f;

  /**
   * \brief Time a magazine is held in front of the eyes to be read
   *    (user, 2026-11-25)
   */
  constexpr float VrConsumeReadSeconds = 1.5f;

  /**
   * \brief Reads what a B42 Inject animation does
   *
   * \param [in] animation The animation file
   * \param [in] name Its file name without folder and extension
   * \param [out] result What the gesture uses
   * \returns \c false if it is not a B42 Inject animation (no prop
   *    attached to the left hand)
   */
  bool vrReadConsumableAnimation(
    const VrKfAnimation&          animation,
    const std::string&            name,
          VrConsumableAnimation&  result);

}
