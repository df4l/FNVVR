#pragma once

#include <cstddef>
#include <cstdint>

#include "vr_game_space.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  /**
   * \brief What a named part of a weapon's model is for the manual reload
   *
   * The reload animations move parts of the weapon's model whose names
   * start with "##", and the names differ per weapon, see
   * findings/reload.md §6 and §7 (decision 10).
   */
  enum class VrWeaponPartKind {
    /// Not used by the manual reload
    None,
    /// A detachable magazine or energy cell
    Magazine,
    /// What is pulled back to chamber a round: a slide, charging handle,
    /// bolt or cocking lever
    Rack,
    /// A round loaded by itself: a cartridge or shell that the reload
    /// animation of a weapon loaded round by round carries in, or the
    /// shells of a break action
    Round,
    /// What a break action opens about: the part named as its stock,
    /// which carries the barrel and the shells and which the reload
    /// animation turns down about the breech
    Hinge,
    /// A revolver's speed loader, which its reload animation carries into
    /// the swung out cylinder
    Loader,
    /// The loading gate of a revolver loaded round by round, or the cap
    /// over the loading port of a rifle loaded round by round
    Gate,
    /// A launcher's missile or mini-nuke (##NifRound), loaded alone into
    /// the tube, which leaves the weapon when it is fired
    Projectile,
    /// A lock over a magazine, turned open before the magazine comes out
    /// (the minigun's and the grenade machineguns' ##MinigunAmmoLock)
    Lock,
  };

  /**
   * \brief Tells what a part of a weapon's model is from its name
   *
   * A magazine is a part whose name contains "clip", "magazine", "drum",
   * "cell", "battery", "ammo" or "canister", in any case, unless it also
   * names something next to the magazine (its release, latch, lock or bay)
   * or a magazine that is not detachable (a revolver's moon clip); one that
   * names a lock is the lock over the magazine. A rack part is one whose name
   * contains "slide", "bolt", "charginghandle", "lever" or "bar", unless it
   * also names a barrel, a lock, a pin, a release or a trigger. A round is
   * one whose name contains "shell", "bullet" or "round", unless it is a
   * spent case ("empty", "casing"). A hinge is one whose name contains
   * "stock". A speed loader is one whose name contains "speedloader",
   * whatever else it names, and a loading gate one whose name contains
   * "gate". A launcher's round, ##NifRound, is a projectile. A table of
   * exceptions comes first. The ":n" suffix of a
   * part's geometry is ignored.
   *
   * \param [in] name Name of the object, with its "##" prefix
   */
  VrWeaponPartKind vrClassifyWeaponPart(const char* name);

  /**
   * \brief Whether a part of a weapon's model is a cartridge, live or spent
   *
   * A round (VrWeaponPartKind::Round), or a part whose name contains
   * "shell", "bullet" or "casing" and that is not a speed loader: what a
   * revolver's cylinder holds.
   *
   * \param [in] name Name of the object, with its "##" prefix
   */
  bool vrIsCartridgePart(const char* name);

  /**
   * \brief Distance from a point to a segment
   *
   * \param [in] point The point
   * \param [in] start Where the segment starts
   * \param [in] axis Direction it goes along from there, unit
   * \param [in] length Its length, in the units of the points
   */
  float vrDistanceToSegment(
    const VrVector3&            point,
    const VrVector3&            start,
    const VrVector3&            axis,
          float                 length);

  /**
   * \brief Where new magazines are taken: behind the back, over the left shoulder
   *
   * Distances in metres in the head's frame, the head's heading only:
   * behind the head, not far to its right, not much below it and within
   * reach of it.
   */
  struct VrMagazinePouch {
    /// How far behind the head the hand must be
    float behind   = 0.05f;
    /// How far right of the head's centre the hand may be
    float right    = 0.05f;
    /// How far below the head the hand may be
    float below    = 0.35f;
    /// How far from the head the hand may be
    float reach    = 0.6f;
  };

  /**
   * \brief Whether a hand is behind the back, over the left shoulder
   *
   * The heading is taken from the head's forward and up axes, so that it
   * stays defined when the player looks straight up or down.
   *
   * \param [in] head Head pose in the game world (Z up)
   * \param [in] hand Hand position in the game world
   * \param [in] pouch Extent of the place, in metres
   */
  bool vrIsAtMagazinePouch(
    const VrGameCameraPose&     head,
    const VrVector3&            hand,
    const VrMagazinePouch&      pouch);

  /**
   * \brief Where the weapon's magazine is
   */
  enum class VrMagazineState {
    /// In the weapon, as the game draws it
    InWeapon,
    /// Held by the left hand in the weapon's well, sliding along it
    Seating,
    /// Held by the left hand, out of the weapon
    InHand,
    /// Neither in the weapon nor in the hand
    Out,
  };

  /**
   * \brief What the left hand did with the magazine in a frame
   */
  enum class VrMagazineEvent {
    None,
    /// The magazine was taken out of the weapon: it slides out of its well
    Ejected,
    /// The magazine came out of the well, it is free in the hand
    Cleared,
    /// The magazine in the hand entered the well
    Engaged,
    /// A new magazine was taken behind the back
    Taken,
    /// The hand reached behind the back, but no rounds are left
    NoRounds,
    /// The magazine in the hand was let go of
    Dropped,
    /// The magazine was pushed home into the weapon
    Inserted,
  };

  /**
   * \brief What the magazine logic sees in a frame
   *
   * Positions in the game world, in game units.
   */
  struct VrMagazineInput {
    /// The weapon in the right hand has a magazine, and the left hand is
    /// tracked and free (not on the weapon)
    bool             active = false;
    /// The magazine action (the left trigger) is held
    bool             grip = false;
    /// A press of the trigger does not take the magazine out: it is nearer
    /// the weapon's rack, see VrRackHand
    bool             blocked = false;
    /// Left hand: the middle of the grip
    VrVector3        hand;
    /// Where the centre of a magazine held by the left hand is
    VrVector3        held;
    /// Where the centre of the magazine sits in the weapon
    VrVector3        well;
    /// Direction the magazine comes out of the weapon along, unit
    VrVector3        axis;
    /// Head pose
    VrGameCameraPose head;
    /// The inventory holds rounds for the weapon
    bool             roundsHeld = false;
    /// The weapon is closed (a break action): what the hand holds cannot
    /// enter it
    bool             closed = false;
  };

  /**
   * \brief Distances of the magazine gestures, in metres
   */
  struct VrMagazineReach {
    /// How close the hand must be to the magazine, or to the way it comes
    /// out of the weapon, to take it out
    float eject   = 0.12f;
    /// How far that way goes from the magazine's centre
    float below   = 0.15f;
    /// How far the magazine slides along its well, from its place to the
    /// mouth of the well
    float depth   = 0.03f;
    /// How far past the mouth of the well the magazine is pulled before it
    /// comes out
    float slack   = 0.02f;
    /// How close to the way into the well a magazine in the hand must come
    /// to enter it
    float capture = 0.06f;
    /// How far that way goes beyond the mouth of the well
    float approach = 0.10f;
    /// How far the hand may move across the well before the magazine comes
    /// out of it sideways
    float leave   = 0.06f;
    /// How far a magazine just taken out must slide before pushing it back
    /// puts it in again
    float armed   = 0.01f;
    /// How far past that capture a magazine that came out of the well must
    /// move before it can enter again
    float rearm   = 0.02f;
    /// Where new magazines are taken
    VrMagazinePouch pouch;
  };

  /**
   * \brief Follows the magazine of the weapon in the right hand
   *
   * The left trigger pressed near the magazine, or below it along the way it
   * comes out, takes it out: it slides along
   * its well with the hand, and comes free past the mouth of the well or
   * when the hand moves across it. It stays in the hand while the trigger is
   * held. Let go of, it disappears. With no magazine in the weapon, the
   * left trigger pressed behind the back takes a new one if rounds are left.
   * A magazine in the hand brought near the way into the well enters it
   * at its mouth,
   * and goes in once it is pushed home. Only a press of the trigger takes a
   * magazine: holding it while the hand moves takes nothing.
   *
   * The game decides the clip: it is emptied on the way out and filled on
   * the way in by the caller.
   *
   * Break actions are loaded the same way with their shells, and
   * revolvers with their speed loader; weapons loaded round by round are
   * loaded by VrRoundLoader.
   */
  class VrMagazineHand {

  public:

    explicit VrMagazineHand(const VrMagazineReach& reach = VrMagazineReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrMagazineEvent update(const VrMagazineInput& input);

    /**
     * \brief Puts the magazine back in the weapon
     *
     * When the weapon changes, or when the game fills the clip itself
     * (equipping, V.A.T.S., an ammunition swap...).
     */
    void reset();

    /**
     * \brief Empties the weapon
     *
     * When a break action is opened: its shells are thrown out, nothing is
     * left in it.
     */
    void empty();

    /**
     * \brief How far the hand is from where a press of the trigger takes the
     *    magazine out, in metres
     */
    float ejectDistance(const VrMagazineInput& input) const;

    VrMagazineState state() const {
      return m_state;
    }

    /**
     * \brief How far the magazine is out of its place along its well
     *
     * In metres, while it is VrMagazineState::Seating.
     */
    float seat() const {
      return m_seat;
    }

  private:

    VrMagazineReach m_reach;
    VrMagazineState m_state     = VrMagazineState::InWeapon;
    bool            m_gripDown  = false;
    bool            m_canInsert = false;
    bool            m_canEnter  = false;

    // While seating: how far out the magazine is, and where the held
    // magazine was along and across the well when it entered it, so that
    // the hand moves it from there
    float           m_seat       = 0.0f;
    float           m_alongStart = 0.0f;
    VrVector3       m_acrossStart;

    void seat(const VrMagazineInput& input, float start);

    VrMagazineEvent take(const VrMagazineInput& input, bool pressed, float toWay);

    VrMagazineEvent drop();

  };

  /**
   * \brief What the left hand did with a handful of rounds in a frame
   */
  enum class VrRoundEvent {
    None,
    /// A handful of rounds was taken behind the back
    Taken,
    /// The hand reached behind the back, but no rounds are left
    NoRounds,
    /// The held round reached the loading port: loading starts
    Started,
    /// A round came to its place in this frame
    Placed,
    /// The held round left the loading port, or it closed: the handful
    /// stays in the hand
    Stopped,
    /// The handful was let go of
    Dropped,
    /// The weapon took no more rounds: loading stops, set by the caller
    /// after refuse()
    Full,
  };

  /**
   * \brief Where the left hand's handful of rounds is
   */
  enum class VrRoundState {
    /// The hand holds no rounds
    Empty,
    /// The hand holds a handful of rounds, at its controller
    Holding,
    /// They go into the loading port, one per loop of the game's loading
    /// animation
    Loading,
  };

  /**
   * \brief What the round loading logic sees in a frame
   *
   * Positions in the game world, in game units.
   */
  struct VrRoundInput {
    /// The weapon in the right hand is loaded round by round, and the left
    /// hand is tracked and free (not on the weapon)
    bool             active = false;
    /// The magazine action (the left trigger) is held
    bool             grip = false;
    /// The loading port is closed (a revolver's gate): no round goes in
    bool             closed = false;
    /// The inventory holds rounds beyond those in the clip
    bool             roundsHeld = false;
    /// Left hand: the middle of the grip
    VrVector3        hand;
    /// Left hand: where the round it holds between its fingers is
    VrVector3        round;
    /// Where a round enters the loading port
    VrVector3        port;
    /// Head pose
    VrGameCameraPose head;
    /// Time the animation plays for in this frame, in seconds of it
    float            dt = 0.0f;
  };

  /**
   * \brief One loop of the game's animation that loads one round
   *
   * In seconds of the animation, from its start.
   */
  struct VrRoundLoopTiming {
    /// The loop starts again: the animation's "Hit" key
    float length = 0.0f;
    /// The round comes to its place
    float placed = 0.0f;
  };

  /**
   * \brief Distances of the round loading gesture, in metres
   */
  struct VrRoundReach {
    /// How close the held round must come to the loading port to start
    /// loading
    float capture = 0.08f;
    /// How far it may move from it while loading
    float leave   = 0.15f;
    /// How far past the capture a hand that stopped loading must move
    /// before it can start again
    float rearm   = 0.03f;
    /// Where the rounds are taken
    VrMagazinePouch pouch;
  };

  /**
   * \brief Follows the left hand loading a weapon round by round
   *
   * The left trigger pressed behind the back takes a handful of rounds if
   * the inventory holds any beyond the clip. Its round brought to the
   * entrance of the loading port and held there, the rounds go in at the
   * rhythm of the game's own loading animation, one coming to its place
   * in each of its loops, until the weapon is full (refuse), the hand
   * moves away from the port, the port closes or the trigger is let go
   * of. Moved away or with the port closed, the hand keeps the handful;
   * let go of, the handful is gone: no round was taken from the
   * inventory. See findings/reload.md §8.11.
   */
  class VrRoundLoader {

  public:

    explicit VrRoundLoader(const VrRoundReach& reach = VrRoundReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrRoundEvent update(const VrRoundInput& input, const VrRoundLoopTiming& loop);

    /**
     * \brief The weapon took no more rounds
     *
     * After VrRoundEvent::Placed, when the game refuses the round (the
     * weapon is full or no rounds are left): the animation stops and the
     * hand keeps the handful, away from the port before it can start again.
     */
    void refuse();

    /**
     * \brief Empties the hand, when the weapon changes
     */
    void reset();

    VrRoundState state() const {
      return m_state;
    }

    /**
     * \brief Time in the loop of the loading animation, in seconds
     *
     * Valid while VrRoundState::Loading.
     */
    float time() const {
      return m_time;
    }

    /**
     * \brief Time in the loop at the last frame
     *
     * Before time() when the loop went on, after it when it started again;
     * equal to it in the frame the animation starts.
     */
    float lastTime() const {
      return m_lastTime;
    }

  private:

    VrRoundReach m_reach;
    VrRoundState m_state    = VrRoundState::Empty;
    bool         m_gripDown = false;
    bool         m_canStart = false;
    bool         m_placed   = false;
    float        m_time     = 0.0f;
    float        m_lastTime = 0.0f;

    void stop(const VrRoundInput& input);

  };

  /**
   * \brief What the left hand did with the weapon's rack part in a frame
   */
  enum class VrRackEvent {
    None,
    /// The hand took the rack part
    Grabbed,
    /// The hand let go of it before it was pulled back all the way
    Released,
    /// The hand let go of it after pulling it back all the way: it goes
    /// home and chambers a round
    Racked,
  };

  /**
   * \brief What the rack logic sees in a frame
   *
   * Positions in the game world, in game units.
   */
  struct VrRackInput {
    /// The weapon in the right hand has a rack part, and the left hand is
    /// tracked, free (not on the weapon) and holds no magazine
    bool      active = false;
    /// The magazine action (the left trigger) is held
    bool      grip = false;
    /// A press of the trigger does not take the rack part: it is nearer the
    /// magazine, see VrMagazineHand
    bool      blocked = false;
    /// How far back the rack part rests when no hand holds it, in metres:
    /// its travel when it is locked back (nothing is chambered), part of
    /// it when the weapon is jammed, 0 when it is home
    float     rest = 0.0f;
    /// Left hand: the middle of the grip
    VrVector3 hand;
    /// Where the centre of the rack part is when it is home
    VrVector3 rack;
    /// Direction it is pulled back along, unit
    VrVector3 axis;
    /// How far it is pulled back, in metres
    float     travel = 0.0f;
  };

  /**
   * \brief Distances of the rack gesture, in metres
   */
  struct VrRackReach {
    /// How close the hand must be to the rack part, or to the way it is
    /// pulled back along, to take it
    float grab  = 0.10f;
    /// How far the hand may move across that way before it lets go
    float leave = 0.10f;
    /// Part of the travel it must be pulled back by to chamber a round
    float full  = 0.85f;
  };

  /**
   * \brief Follows the rack part of the weapon in the right hand
   *
   * The left trigger pressed near the rack part, or along the way it is
   * pulled back, takes it: it follows the hand back along its way, up to
   * its travel. Let go of, or left sideways, it goes home by itself, and
   * chambers a round if it was pulled back all the way. It is taken
   * where it rests: a part locked back is already all the way back, so
   * taking it and letting it go is enough.
   */
  class VrRackHand {

  public:

    explicit VrRackHand(const VrRackReach& reach = VrRackReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrRackEvent update(const VrRackInput& input);

    /**
     * \brief Lets go of the rack part, when the weapon changes
     */
    void reset();

    /**
     * \brief How far the hand is from where a press of the trigger takes the
     *    rack part, in metres
     */
    float grabDistance(const VrRackInput& input) const;

    bool held() const {
      return m_held;
    }

    /**
     * \brief How far the hand holds the rack part back, in metres
     */
    float pull() const {
      return m_pull;
    }

  private:

    VrRackReach m_reach;
    bool        m_gripDown = false;
    bool        m_held     = false;
    bool        m_full     = false;
    float       m_pull     = 0.0f;

    // Where the hand was along and across the way back when it took the
    // part, and how far back the part was then
    float       m_alongStart = 0.0f;
    float       m_pullStart  = 0.0f;
    VrVector3   m_acrossStart;

  };

  /**
   * \brief What the left hand did with the action of a break action in a frame
   */
  enum class VrBreakEvent {
    None,
    /// The action was broken open
    Opened,
    /// The action was closed
    Closed,
  };

  /**
   * \brief What the break action logic sees in a frame
   *
   * Positions in the game world, in game units.
   */
  struct VrBreakInput {
    /// The weapon in the right hand is a break action, and the left hand is
    /// tracked and free (not on the weapon)
    bool      active = false;
    /// The magazine action (the left trigger) is held
    bool      grip = false;
    /// A press of the trigger does not work the action: the hand holds
    /// shells
    bool      blocked = false;
    /// Left hand: the middle of the grip
    VrVector3 hand;
    /// Where the shells sit in the breech
    VrVector3 breech;
    /// Direction the barrel points along from the breech, unit
    VrVector3 barrel;
  };

  /**
   * \brief Distances of the break action gesture, in metres
   */
  struct VrBreakReach {
    /// How close the hand must be to the barrel to work the action
    float reach  = 0.10f;
    /// How far the barrel goes from the breech
    float length = 0.35f;
  };

  /**
   * \brief Follows the action of a break action in the right hand
   *
   * The left trigger pressed near the barrel, from the breech forward,
   * breaks the action open, and pressed there again closes it. Only a
   * press works it: holding the trigger while the hand moves does nothing.
   */
  class VrBreakHand {

  public:

    explicit VrBreakHand(const VrBreakReach& reach = VrBreakReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrBreakEvent update(const VrBreakInput& input);

    /**
     * \brief Closes the action, when the weapon changes or the game
     *    reloads it
     */
    void reset();

    /**
     * \brief How far the hand is from where a press of the trigger works
     *    the action, in metres
     */
    float workDistance(const VrBreakInput& input) const;

    bool open() const {
      return m_open;
    }

  private:

    VrBreakReach m_reach;
    bool         m_gripDown = false;
    bool         m_open     = false;

  };

  /**
   * \brief What the left hand did with the lock over the magazine in a frame
   */
  enum class VrLockEvent {
    None,
    /// The hand took the lock
    Grabbed,
    /// The hand turned it open
    Opened,
    /// The hand turned it shut
    Shut,
  };

  /**
   * \brief What the lock logic sees in a frame
   *
   * Positions in the game world, in game units.
   */
  struct VrLockInput {
    /// The weapon in the right hand has a lock over its magazine, and the
    /// left hand is tracked and free (not on the weapon)
    bool      active = false;
    /// The magazine action (the left trigger) is held
    bool      grip = false;
    /// A press of the trigger does not take the lock: the hand holds a
    /// magazine, the weapon has none, or the magazine is nearer
    bool      blocked = false;
    /// Left hand: the middle of the grip
    VrVector3 hand;
    /// Where the lock turns about
    VrVector3 pivot;
    /// Axis it turns about to open, unit
    VrVector3 axis;
    /// From the pivot to the middle of the lock, shut
    VrVector3 arm;
    /// How far it turns open, in radians
    float     angle = 0.0f;
  };

  /**
   * \brief Distances and shares of the lock gesture
   */
  struct VrLockReach {
    /// How close the hand must be to the lock to take it, in metres
    float grab  = 0.08f;
    /// Share of its turn past which it is open
    float open  = 0.8f;
    /// Share of its turn below which an open lock is shut
    float shut  = 0.2f;
  };

  /**
   * \brief Follows the lock over the magazine of the weapon in the right
   *    hand
   *
   * The left trigger pressed near the lock, from its pivot to twice the
   * distance to its middle, takes it: it turns with the hand about its axis,
   * up to the angle of the game's reload animation. Turned most of the way it
   * is open, and back most of the way it is shut again. Let go of, it
   * stays open or shut, see findings/reload.md §8.12.
   */
  class VrLockHand {

  public:

    explicit VrLockHand(const VrLockReach& reach = VrLockReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrLockEvent update(const VrLockInput& input);

    /**
     * \brief Shuts the lock, when the weapon changes or the game reloads it
     */
    void reset();

    /**
     * \brief How far the hand is from where a press of the trigger takes the
     *    lock, in metres
     */
    float grabDistance(const VrLockInput& input) const;

    bool held() const {
      return m_held;
    }

    bool open() const {
      return m_open;
    }

    /**
     * \brief How far the lock is turned, from 0 (shut) to 1 (open)
     */
    float turn() const {
      return m_turn;
    }

  private:

    VrLockReach m_reach;
    bool        m_gripDown = false;
    bool        m_held     = false;
    bool        m_open     = false;
    float       m_turn     = 0.0f;

  };

  /**
   * \brief How fast a hand turns about an axis
   *
   * From the hand's frame at two times: each of its axes turns from the
   * first time to the second, and their turns add up to the hand's.
   *
   * \param [in] from The hand at the first time
   * \param [in] to The hand at the second time
   * \param [in] axis Axis to turn about, unit, in the frame of the poses
   * \param [in] dt Time between the two, in seconds
   * \returns The rate in radians per second, positive for a turn about the
   *    axis by the right-hand rule, 0 if no time passed
   */
  float vrTurnRate(
    const VrGameCameraPose&     from,
    const VrGameCameraPose&     to,
    const VrVector3&            axis,
          float                 dt);

  /**
   * \brief What the flick logic sees in a frame
   */
  struct VrFlickInput {
    /// The weapon in the right hand is a revolver that opens with a flick,
    /// and the left hand is tracked and free (not on the weapon)
    bool  active = false;
    /// A flick does not open or close it: the left hand pushes rounds into
    /// the cylinder
    bool  blocked = false;
    /// How fast the right hand turns about the barrel, in radians per
    /// second, positive in the direction the cylinder swings out
    float roll = 0.0f;
    /// Time since the last frame, in seconds
    float dt = 0.0f;
  };

  /**
   * \brief How hard and how often the revolver is flicked
   */
  struct VrFlickReach {
    /// How fast the hand must turn about the barrel, in two frames in a
    /// row, in radians per second (about 400 degrees per second)
    float rate = 7.0f;
    /// How long after a flick the next one is ignored, in seconds: the
    /// wrist turns back after a flick
    float rest = 0.5f;
  };

  /**
   * \brief Follows the cylinder of a revolver in the right hand
   *
   * A flick of the right wrist that turns the revolver about its barrel the
   * way the cylinder swings out opens it, and one the other way closes it,
   * see findings/reload.md §8.9. A flick is a turn faster than the reach's
   * rate in two frames in a row; the wrist turning back right after it is
   * not another one.
   */
  class VrFlickHand {

  public:

    explicit VrFlickHand(const VrFlickReach& reach = VrFlickReach())
    : m_reach(reach) { }

    /**
     * \brief Takes the frame's input
     * \returns What happened in this frame
     */
    VrBreakEvent update(const VrFlickInput& input);

    /**
     * \brief Closes the cylinder, when the weapon changes or the game
     *    reloads it
     */
    void reset();

    bool open() const {
      return m_open;
    }

  private:

    VrFlickReach m_reach;
    bool         m_open     = false;
    float        m_lastRoll = 0.0f;
    float        m_wait     = 0.0f;

  };


  /**
   * \brief Where the magazine is at a time of the game's reload animation
   */
  struct VrMagazineSample {
    float     time = 0.0f;
    /// The magazine's centre relative to its place in the weapon, in
    /// metres, in the weapon's frame
    VrVector3 offset;
  };

  /**
   * \brief When the game's reload animation moves the magazine
   */
  struct VrMagazineTiming {
    /// The magazine leaves its place
    float     out  = 0.0f;
    /// The magazine is back in its place
    float     in   = 0.0f;
    /// The left hand holds the new magazine, before pushing it in
    float     grip = 0.0f;
    /// Direction the new magazine comes from, towards its place: the
    /// direction it goes out along, unit, in the weapon's frame
    VrVector3 axis;
  };

  /**
   * \brief Finds when the game's reload animation moves the magazine
   *
   * The magazine leaves its place when it first moves by more than a
   * centimetre, and is back when it last comes within a centimetre of it.
   * The hand holds the new magazine at the last time before that it was
   * about four centimetres out, or less if it never went that far.
   *
   * \param [in] samples The magazine through the animation, in time order
   * \param [in] count Number of samples
   * \param [out] timing When the magazine moves
   * \returns \c false if the magazine does not leave its place and come
   *    back to it
   */
  bool vrFindMagazineTiming(
    const VrMagazineSample*     samples,
          size_t                count,
          VrMagazineTiming&     timing);

  /**
   * \brief Finds where the game's reload animation puts a round
   *
   * The round, carried in by the left hand, stops in its place: the first
   * time it stays within a few millimetres of where it is for a tenth of
   * a second after having been three centimetres away from there. It
   * comes from the direction it was in the last time before that it was
   * about four centimetres out (less if it never went that far), which
   * is also the time the hand holds it if it never carries it still.
   * \c out is the first sample's time.
   *
   * \param [in] samples The round through the animation, in time order,
   *    each relative to the same point of the weapon
   * \param [in] count Number of samples
   * \param [out] timing When the round goes in, and from where
   * \param [out] place The sample where it is in its place
   * \returns \c false if the round never comes to rest after moving in
   */
  bool vrFindRoundTiming(
    const VrMagazineSample*     samples,
          size_t                count,
          VrMagazineTiming&     timing,
          size_t&               place);

  /**
   * \brief Finds when the game's reload animation holds the magazine best
   *
   * The left hand holds the magazine while it carries it: from one sample
   * to the next, the magazine moves by less than a few millimetres and
   * turns by less than a few degrees in the hand's frame. Of those times,
   * the hand holds it best where its centre is nearest the hand bone, in
   * the palm rather than at the fingertips or pushed in by the palm.
   *
   * \param [in] inHand The magazine's centre (in metres) and rotation in
   *    the left hand bone's frame, sampled 60 times per second
   * \param [in] count Number of samples
   * \returns The sample, or \c count if the hand never carries the
   *    magazine
   */
  size_t vrFindMagazineHold(
    const VrGameTransform*      inHand,
          size_t                count);

  /**
   * \brief Turns a magazine the hand would hold upside down upright
   *
   * Some reload animations never carry the new magazine in the left hand
   * (vrFindMagazineHold): it is then held as the hand pushes it into the
   * weapon, which can leave it upside down in the player's hand (the
   * varmint rifle's 2hrreloadm). If the end of the magazine that goes
   * into the weapon points down in the controller's frame, the magazine
   * is turned about its centre by the least rotation that points that
   * end along the controller's up.
   *
   * \param [in] handInController The hand bone's rotation in the
   *    controller's frame
   * \param [in] up The controller's up, in its frame
   * \param [in] insertion Direction the magazine goes into the weapon
   *    along, in the magazine's frame
   * \param [in] centre The magazine's centre in its frame, in units of
   *    the hand's scale
   * \param [in,out] inHand The magazine in the hand bone's frame, turned
   *    if it points down
   * \param [out] angle The angle, in radians, between the end that goes
   *    in and the controller's up before any turn
   * \returns \c true if the magazine was turned
   */
  bool vrUprightMagazineHold(
    const VrGameRotation&       handInController,
    const VrVector3&            up,
    const VrVector3&            insertion,
    const VrVector3&            centre,
          VrGameTransform&      inHand,
          float&                angle);

  /**
   * \brief How the game's reload animation pulls the rack part back
   */
  struct VrRackMotion {
    /// The sample it starts moving at, the one it is furthest back at
    /// and the one it is home again at (the last if it stays back)
    size_t    start = 0;
    size_t    back  = 0;
    size_t    home  = 0;
    /// Direction it is pulled back along, unit, in the weapon's frame
    VrVector3 axis;
    /// How far, in metres
    float     travel = 0.0f;
  };

  /**
   * \brief Finds how the game's reload animation racks the weapon
   *
   * The rack part is furthest back where it is furthest from its place,
   * which must be more than a centimetre; it starts moving the last time
   * before that it was within a few millimetres of its place, and is home
   * the first time after that it is that near again.
   *
   * \param [in] samples The rack part through the animation, in time order
   * \param [in] count Number of samples
   * \param [out] motion How it is pulled back
   * \returns \c false if the animation does not move it that far
   */
  bool vrFindRackMotion(
    const VrMagazineSample*     samples,
          size_t                count,
          VrRackMotion&         motion);

  /**
   * \brief Finds the sound played nearest a time
   *
   * \param [in] times Times of the sounds
   * \param [in] count Number of sounds
   * \param [in] time Time to look near
   * \param [in] window How far from the time a sound may be
   * \param [in] exclude A sound not to pick, or -1
   * \returns The sound's index, or -1 if none is that near
   */
  int32_t vrFindNearestSound(
    const float*                times,
          size_t                count,
          float                 time,
          float                 window,
          int32_t               exclude);

  /**
   * \brief Finds the sound played nearest a stretch of time
   *
   * \param [in] times Times of the sounds
   * \param [in] count Number of sounds
   * \param [in] from Start of the stretch
   * \param [in] to End of the stretch
   * \param [in] window How far outside the stretch a sound may be
   * \param [in] exclude A sound not to pick, or -1
   * \returns The sound's index, or -1 if none is that near
   */
  int32_t vrFindSoundDuring(
    const float*                times,
          size_t                count,
          float                 from,
          float                 to,
          float                 window,
          int32_t               exclude);

  /**
   * \brief How the weapon in the hand is reloaded by hand
   */
  enum class VrReloadKind {
    /// It is not reloaded by hand
    None,
    /// With a detachable magazine or cell
    Magazine,
    /// Round by round, through a loading port or a revolver's loading gate
    Rounds,
    /// A break action, opened with the left hand
    BreakAction,
    /// A revolver whose cylinder swings out
    Cylinder,
  };

  /**
   * \brief Where the reload of the weapon in the hand stands
   */
  struct VrReloadSituation {
    VrReloadKind     kind     = VrReloadKind::None;
    /// The weapon has a rack part (slide, bolt, lever, pump)
    bool             hasRack  = false;
    /// Where its magazine, shells or speed loader are
    VrMagazineState  magazine = VrMagazineState::InWeapon;
    /// Where the handful of rounds is
    VrRoundState     rounds   = VrRoundState::Empty;
    /// Rounds in the clip, or -1 without a clip
    int32_t          clip     = 0;
    /// The weapon refused a round at this clip count
    bool             full     = false;
    /// A magazine went in with nothing chambered: racking fills the clip
    bool             pendingFill = false;
    /// The weapon is jammed
    bool             jammed   = false;
    /// The weapon fired its last round and its action was not worked since
    bool             unchambered = false;
    /// The action (a break action) is open
    bool             actionOpen = false;
    /// The weapon has a lock over its magazine
    bool             hasLock = false;
    /// It is open
    bool             lockOpen = false;
  };

  /**
   * \brief Parts of the weapon the next reload gesture works
   *
   * They glow as V.A.T.S. highlights a limb, see findings/reload.md §9.
   */
  struct VrReloadParts {
    /// The magazine or cell, to take out
    bool magazine = false;
    /// The rack part, to work
    bool rack     = false;
    /// The barrel of a break action, to open or close
    bool hinge    = false;
    /// The loading port or gate, where the rounds in the hand go
    bool port     = false;
    /// The lock over the magazine, to open or shut
    bool lock     = false;

    bool any() const {
      return magazine || rack || hinge || port || lock;
    }
  };

  /**
   * \brief Tells which parts of the weapon the reload needs worked
   *
   * Whenever the weapon needs it, wherever the left hand is (user,
   * 2026-10-18): an empty or jammed magazine is taken out, a weapon with
   * nothing chambered or jammed is racked, an empty break action is opened
   * and a loaded one closed, and the port takes the rounds in the hand
   * until the weapon is full. A hand that holds a magazine, shells or
   * rounds is not asked to rack, and a cylinder that swings out is worked
   * with the right wrist, so nothing of it glows. A lock over the magazine
   * is opened before the magazine is taken out, and shut once a loaded one
   * is in.
   */
  VrReloadParts vrPartsToWork(const VrReloadSituation& situation);

}
