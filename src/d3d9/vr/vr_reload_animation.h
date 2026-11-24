#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_game_sequence.h"
#include "vr_game_sound.h"
#include "vr_magazine.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  /**
   * \brief The rack part at a point of its way back
   */
  struct VrRackPose {
    /// How far back along the way it is, in metres
    float           along = 0.0f;
    /// The part in the weapon node's frame
    VrGameTransform pose;
  };

  /**
   * \brief What the game's reload animation does with a weapon's rack part
   */
  struct VrRackAnimation {
    /// The part from home to all the way back, \c along never going down
    std::vector<VrRackPose> poses;
    /// Direction it is pulled back along, unit, in the weapon node's frame
    VrVector3               axis;
    /// How far, in metres
    float                   travel = 0.0f;
    /// The left hand holds it all the way back
    bool                    held = false;
    /// The rack part in the left hand bone's frame then, in units of the
    /// hand's scale
    VrGameTransform         partInHand;
    /// Rotations of the bones below the left hand in their parents then
    std::vector<VrBonePose> fingers;
    /// The weapon chambering a round
    VrAnimationSound        sound;
  };

  /**
   * \brief How the game's reload animation opens the action
   *
   * A break action turns its stock (the hinge) away from the barrel; a
   * revolver swings its cylinder out, or opens its loading gate.
   */
  struct VrHingeAnimation {
    /// Name of the part that turns
    std::string     part;
    /// The part in the weapon node's frame while the action is closed
    VrGameTransform closed;
    /// The part in the weapon node's frame while the action is open
    VrGameTransform open;
    /// How far it turns, in radians
    float           angle = 0.0f;
    /// Axis it turns about to open, unit, in the weapon node's frame
    VrVector3       axis;
    /// The action opening and closing
    VrAnimationSound openSound;
    VrAnimationSound closeSound;
  };

  /**
   * \brief A sound of a loading loop
   */
  struct VrRoundLoopSound {
    /// When it plays, in seconds from the start of the loop
    float         time = 0.0f;
    VrAnimationSound sound;
  };

  /**
   * \brief The loop of the game's animation that loads one round
   *
   * The animation of a weapon loaded round by round is played once per
   * round from its start up to its "Hit" key, where the game starts it
   * again while rounds are left to load; the rest of it (working the
   * lever, shutting the gate) only follows the last round. The game's
   * left hand barely moves in it (the right hand turns the weapon to the
   * round), so only its rhythm and its sounds are kept: the rounds go in
   * from the left hand held at the port, see findings/reload.md §8.11.
   */
  struct VrRoundLoop {
    VrRoundLoopTiming             timing;
    /// The loop's sounds, in time order
    std::vector<VrRoundLoopSound> sounds;
  };

  /**
   * \brief What the game's reload animation does with a weapon's magazine
   *
   * Read from the first-person reload animation the game would play for
   * the weapon, without playing it: its tracks are evaluated at chosen
   * times, see findings/reload.md §8.2.
   */
  struct VrReloadAnimation {
    /// The part is a round loaded by itself (VrWeaponPartKind::Round),
    /// read from the animation that loads one round
    bool                    round = false;
    /// Where the part is in its place, in the weapon node's frame: where a
    /// round comes to rest, or where the animation leaves the magazine at
    /// its end (the part's own place in the model may be elsewhere: a
    /// launcher's shot carries its round away)
    VrGameTransform         place;
    /// The magazine part in the left hand bone's frame while the hand
    /// holds the new magazine, in units of the hand's scale
    VrGameTransform         magazineInHand;
    /// How far the magazine's centre is from the left hand bone then, in
    /// metres: the hand of some animations never carries it
    float                   handDistance = 0.0f;
    /// The hand carries the magazine (vrFindMagazineHold); otherwise it is
    /// held as the hand pushes it into the weapon
    bool                    carried = false;
    /// Rotations of the bones below the left hand in their parents while
    /// it holds the new magazine
    std::vector<VrBonePose> fingers;
    /// Direction the magazine comes out of the weapon along, unit, in the
    /// weapon node's frame
    VrVector3               axis;
    /// The magazine going out and going in (a round has no sound going
    /// out)
    VrAnimationSound        out;
    VrAnimationSound        in;
    /// How the animation racks the weapon, valid if \c hasRack
    VrRackAnimation         rack;
    bool                    hasRack = false;
    /// How the animation opens the action, valid if \c hasHinge
    VrHingeAnimation        hinge;
    bool                    hasHinge = false;
    /// The loop that loads one round, valid if \c hasLoop
    VrRoundLoop             loop;
    bool                    hasLoop = false;
    /// How the animation turns the lock over the magazine open before
    /// taking it out and shut once it is back, valid if \c hasLock
    VrHingeAnimation        lock;
    bool                    hasLock = false;
  };

  /**
   * \brief Reads the reload animation of the weapon in the player's hand
   *
   * The animation the game would play for the weapon (its reload group,
   * or its modded one with the extended magazine) is looked up in the
   * player's first-person animations. The magazine's track tells when it
   * goes out and in and along which direction, the left hand's tracks how
   * the hand holds it, and the animation's sounds nearest those times are
   * the magazine's. The rack part's track, if there is one, tells how the
   * weapon is racked, and the sound nearest the time it is furthest back
   * is the chambering.
   *
   * For a weapon loaded round by round, the animation is the one that
   * loads one round, and the part is the round it carries in: where it
   * comes to rest is its place, the direction it comes from the way in,
   * and it has no sound going out. A break action's shells are read the
   * same way, and its hinge part's track tells how far the action opens
   * while they go in, and the sounds nearest the times it starts opening
   * and is closed again are the action's. A revolver's cylinder is read
   * the same way: its speed loader is the round, and the part that swings
   * it out is the one of its ancestors the animation turns furthest; or
   * its loading gate is the hinge. A weapon loaded round by round with a
   * looping reload also gets the loop that loads one round. The lock over a
   * magazine is read as a hinge that opens before the magazine goes out
   * and shuts after it is back in.
   *
   * Only called on the game's main thread, while the animation is not
   * played. Evaluating a track keeps nothing but its last time.
   *
   * \param [in] player The PlayerCharacter
   * \param [in] root Root of the first-person model
   * \param [in] weapon The weapon node in it
   * \param [in] magazine The magazine part below the weapon node, or the
   *    round part
   * \param [in] round The part is a round
   * \param [in] looping The weapon's reload loops, one round at a time
   * \param [in] rack The rack part below the weapon node, or \c nullptr
   * \param [in] hinge The hinge part of a break action, or the loading gate
   *    of a revolver, below the weapon node, or \c nullptr
   * \param [in] swing The action is a revolver's: the hinge, or without
   *    one the ancestor of the round the animation turns furthest, swings
   *    open in the weapon
   * \param [in] lock The lock over the magazine below the weapon node, or
   *    \c nullptr
   * \param [in] leftHand The left hand bone
   * \param [out] animation What the animation does
   * \returns \c false if the weapon has no first-person reload animation
   *    that takes the magazine out and puts it back, or that carries the
   *    round in
   */
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
          VrReloadAnimation&    animation);

  /**
   * \brief Plays a sound of the reload animation for the player
   *
   * As the game plays the sounds of the player's first-person animations,
   * at the reload speed, but from a point of the world.
   *
   * \param [in] sound The sound
   * \param [in] position Where it is played from, in the game world
   */
  void vrPlayReloadSound(
    const VrAnimationSound&     sound,
    const VrVector3&            position);

}
