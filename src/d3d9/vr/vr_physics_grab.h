#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "vr_grab.h"
#include "vr_grab_fingers.h"
#include "vr_hand_body.h"
#include "vr_npc_drag.h"
#include "vr_npc_limb.h"
#include "vr_npc_manhandle.h"
#include "vr_npc_reaction.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief What the hands grabbed and let go of since the last call to
   *    VrPhysicsGrab::takeEvents, for the haptics
   */
  struct VrPhysicsGrabEvents {
    std::array<bool, VrHandCount> grabbed  = { };
    std::array<bool, VrHandCount> released = { };
    /// Let go of at the chest and put in the inventory
    std::array<bool, VrHandCount> stored   = { };
    /// Shoved an actor
    std::array<bool, VrHandCount> shoved   = { };
  };

  /**
   * \brief Holds world objects with the hands
   *
   * Each hand holds its own object on a Havok mouse spring of its own, made
   * as the game makes the one of its grab key (findings/grab.md §7, §8):
   * a press of a free hand's grip holds the object the hand touches (the
   * nearest hit of short rays around the hand, see vrGrabProbeRays) by the
   * point touched, and the spring pulls that point to where it belongs in
   * the hand. When the hand touches nothing, the press pulls the object
   * the hand points at, the game's activation target named by this hand
   * (VrHandActivatePick), by its centre of mass into the hand's centre.
   * Objects too heavy for the game's grab, and fixed and keyframed bodies
   * are not held. A corpse is held by the limb a hand touches, its
   * ragdoll's constraints bring the rest; it is not pulled from a
   * distance. A living actor is held by a limb of its hit reaction
   * ("ragdoll feedback", findings/grab.md §19): the reaction is started
   * when the hand closes on the actor and kept running while it holds,
   * so that the limb is a dynamic body the game drives towards the
   * animation, which the hand's spring pulls against; the limb is let go
   * of once the game ends the reaction (knocked down, out of the bone
   * level of detail). The game only draws the reaction (the drawn limbs
   * follow the ragdoll's) and ends it when its INI setting
   * bRagdollFeedback is on, which it is not by default: for the actors
   * the hands took, that part runs here then, until the reaction ends. The
   * actor reacts to being grabbed (VrNpcReaction). A grip stronger than the
   * actor (both hands add a point of Strength) keeps it from walking away:
   * its own move is replaced with the one the hands give it (VrNpcDrag), it
   * stands while they stand and walks, in the game's animation, where they
   * take it. Otherwise the actor walks on and the limb leaves the hand
   * sooner than a held object would (findings/grab.md §19.8, §19.9). A limb
   * outside the reaction follows its animation: the hand stays on it and the
   * actor still goes where the hand takes it. A hand that touches the object
   * the other hand holds holds it too, with a second spring on the same body.
   *
   * After the Skyrim VR mod PLANCK (findings/grab.md §19.10): a kept actor let
   * go of by a hand moving fast is knocked down with the game's own knock down,
   * thrown along the hand's move; the limbs of an actor knocked down are held as those of a corpse until it
   * stands up. Holding costs action points and the hands let go when there are
   * none left. The player cannot walk away faster than the actor comes along,
   * and passes through it. A fast open hand shoves an actor. Being handled
   * angers an actor, up to an assault on the player.
   *
   * A body held by one hand also turns with the hand: a callback of its
   * spring sets the body's angular velocity towards the rotation it had in
   * the hand when grabbed, about the point held (vrGrabTurnStep). A body
   * held by both hands is turned by the two springs, and takes its
   * rotation in the hand again when one lets go. An actor's limb is left
   * to its ragdoll.
   *
   * A hand pushes with a fixed force (vrGrabMaxAcceleration), shared by
   * both hands on one body, so heavy objects follow and come more slowly
   * than light ones. The springs keep less of the game's damping, which
   * takes a quarter of the held body's speed at every step and kept it
   * from following a walking player, and are aimed ahead of the hands by
   * the player's move (vrGrabLead).
   *
   * Havok steps on another thread while the frame is drawn, so the springs
   * are worked in the game's update of the player, after the game's own
   * grab, with the hands the VR frame gave last. While a hand holds an
   * object, the object's collision group is the player's: it neither
   * pushes nor hurts the player and keeps colliding with everything else.
   * Not an actor's limb, which collides with the other limbs by its
   * ragdoll's own group.
   * The object's own group comes back once it was let go and is clear of
   * the player.
   *
   * A free hand pushes objects with a keyframed sphere (VrHandBodies).
   *
   * As the player moves to a cell with another world, held objects are
   * let go of and the spheres leave the old world, before the game may
   * unload it.
   *
   * An item let go of at the chest goes into the inventory, through the
   * game's own activation by the player, as if it had been picked up
   * from the world.
   *
   * As with the game's grab, the object's OnGrab and OnRelease script
   * blocks run, guards see it taken and its new position is saved. The
   * game's IsPlayerGrabbedRef does not know about it.
   *
   * Only used on the game's main thread.
   */
  class VrPhysicsGrab {

  public:

    ~VrPhysicsGrab();

    /**
     * \brief Finds the game's functions and hooks the player's update
     *
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrPhysicsGrab> install();

    /**
     * \brief Gives a hand for the next update of the player
     *
     * \param [in] hand The hand
     * \param [in] pose The hand in the game world, or \c nullptr when it is
     *    not tracked or the player is not in game; copied
     * \param [in] grip The hand's grip is held
     * \param [in] free The hand may hold an object
     * \param [in] aimed The hand named the game's activation target
     * \param [in] chest The hand is at the chest (vrGrabAtChest)
     */
    void setHand(
            VrHand                hand,
      const VrGameCameraPose*     pose,
            bool                  grip,
            bool                  free,
            bool                  aimed,
            bool                  chest);

    /**
     * \brief Whether a hand holds an object
     */
    bool holds(VrHand hand) const {
      return m_hands[uint32_t(hand)].grab.holding();
    }

    /**
     * \brief What the hands grabbed and let go of since the previous call
     */
    VrPhysicsGrabEvents takeEvents();

    /**
     * \brief Gives work to do in each update of the player
     *
     * Run after the hands' springs, while Havok is idle, on the game's
     * main thread.
     *
     * \param [in] task Called with the PlayerCharacter and the frame time
     */
    void setPlayerUpdateTask(std::function<void (uint8_t*, float)> task) {
      m_playerUpdateTask = std::move(task);
    }

    /**
     * \brief Gives what makes a grabbed living actor react
     *
     * \param [in] reaction Not owned, or \c nullptr for no reaction
     */
    void setNpcReaction(VrNpcReaction* reaction) {
      m_npcReaction = reaction;
    }

    /**
     * \brief Turns the pushing spheres of the free hands on or off
     *
     * \param [in] enabled Off, the hands no longer push objects
     */
    void setHandPush(bool enabled) {
      m_handPush = enabled;
    }

    /**
     * \brief How a hand is drawn to stay on what it holds
     *
     * From the player's last update (vrGrabHandCorrection): the held point,
     * and the body's rotation while the hand turns it, keep their place in
     * the drawn hand when the object is blocked or lags behind.
     *
     * \returns \c nullptr while the hand holds nothing or pulls an object
     */
    const VrGrabCorrection* handCorrection(VrHand hand) const {
      const Correction& correction = m_corrections[uint32_t(hand)];
      return correction.valid ? &correction.value : nullptr;
    }

    /**
     * \brief Gives a hand's finger bones, to fit the fingers on what it
     *    grabs
     *
     * \param [in] hand The hand
     * \param [in] rig Its finger bones, or \c nullptr if not known; copied
     */
    void setFingerRig(VrHand hand, const VrFingerRig* rig);

    /**
     * \brief How far each finger of a hand closes on what it holds
     *
     * Fitted once the hand holds the object where it keeps it
     * (vrFitFingers), with the hand drawn as handCorrection places it.
     *
     * \returns \c nullptr while the hand holds nothing, pulls an object
     *    or its fingers are not known
     */
    const VrFingerCurls* heldFingers(VrHand hand) const {
      const Held& held = m_hands[uint32_t(hand)];
      return m_corrections[uint32_t(hand)].valid && held.fitted ? &held.fingers : nullptr;
    }

  private:

    struct HandInput {
      VrGameCameraPose pose;
      bool             tracked = false;
      bool             grip    = false;
      bool             free    = false;
      bool             aimed   = false;
      bool             chest   = false;
    };

    /**
     * \brief An object held by a hand
     */
    struct Held {
      VrGrabHand grab;
      float      mass   = 0.0f;
      /// NiPointer<bhkMouseSpringAction>, holds a reference
      void*      spring = nullptr;
      /// hkpRigidBody, kept alive by the spring
      uint8_t*   body   = nullptr;
      /// TESObjectREFR, compared with the form found by its id before use
      uint8_t*   ref    = nullptr;
      uint32_t   refId  = 0;
      /// Point held, in the body's frame, Havok units
      VrVector3  local;
      /// The body's own collision filter, the one it has while held, and
      /// whether it was changed
      uint32_t   filter  = 0;
      uint32_t   groupedFilter = 0;
      bool       grouped = false;
      /// A limb of an actor's ragdoll, and whether the actor lives
      bool       limb    = false;
      bool       living  = false;
      /// The grip keeps the living actor from walking away
      bool       inPlace = false;
      /// The living actor is knocked down, its whole ragdoll simulated
      bool       knocked = false;
      /// The limb follows its animation: the spring, which keeps the body
      /// alive, is not in the world
      bool       anchored = false;
      /// The spring's limit on a living limb, for all it drags along, once known
      float      limbAcceleration = 0.0f;
      /// The second hand's failed grip on this actor was logged
      bool       loggedMiss = false;
      /// Where the point held belongs, in the actor's frame, game units
      VrNpcGroundVector anchor;
      /// Where the hand wants the point held, game units
      VrVector3  target;
      /// Seconds in the hand, and how far the point held strayed from the
      /// hand's target, game units
      float      heldTime = 0.0f;
      float      furthest = 0.0f;
      /// The body's rotation in the hand's frame, while the hand turns it
      VrGrabFrame frame;
      bool       turning = false;
      /// The spring's callback array before the turn callback was added
      std::array<uint32_t, 3> callbacks = { };
      /// Turn of the drawn palm towards the point touched, in the hand's
      /// frame (VrGrabCorrection::palm)
      VrVector3     palm;
      /// How far each finger closes on the object, once fitted
      VrFingerCurls fingers = { };
      bool          fitted  = false;
    };

    /**
     * \brief A body let go of whose collision filter is still the player's
     */
    struct Released {
      void*    spring = nullptr;
      uint8_t* body   = nullptr;
      uint32_t filter = 0;
      uint32_t groupedFilter = 0;
    };

    /**
     * \brief Something a hand may hold, found by the probe
     */
    struct Candidate {
      uint8_t*  body = nullptr;
      uint8_t*  ref  = nullptr;
      VrVector3 point;
      float     distance = 0.0f;
      float     mass     = 0.0f;
      /// Pulled from where the hand points rather than touched
      bool      pulled   = false;
      /// A limb of an actor's ragdoll, and whether the actor lives
      bool      limb     = false;
      bool      living   = false;
    };

    struct Correction {
      VrGrabCorrection value;
      bool             valid = false;
    };

    VrPhysicsGrab() = default;

    std::array<HandInput, VrHandCount> m_input;
    std::array<Held, VrHandCount>      m_hands;
    std::array<Correction, VrHandCount> m_corrections;
    std::array<VrFingerRig, VrHandCount> m_rigs;
    std::array<bool, VrHandCount>        m_hasRig = { };
    std::vector<Released>              m_released;
    VrPhysicsGrabEvents                m_events;

    VrGrabSpringConstants m_spring;
    VrGrabSpringConstants m_limbSpring;
    float     m_fullAcceleration = 0.0f;
    float     m_gravity          = 0.0f;
    VrVector3 m_playerPosition;
    VrVector3 m_lead;
    VrVector3 m_limbLead;
    VrVector3 m_playerStep;
    bool      m_hasPlayerPosition = false;
    float     m_leadLogTime       = 0.0f;

    uint32_t m_loggedGrabs   = 0;
    uint32_t m_loggedMisses  = 0;
    uint32_t m_loggedLosses  = 0;
    uint32_t m_loggedLeads   = 0;
    bool     m_loggedSpring  = false;
    bool     m_loggedNoGroup = false;
    uint32_t m_loggedFits    = 0;
    uint32_t m_loggedStores  = 0;
    bool     m_canStore      = false;
    bool     m_handPush      = true;
    bool     m_loggedNoPush  = false;

    std::unique_ptr<VrHandBodies> m_handBodies;

    VrNpcReaction* m_npcReaction = nullptr;
    std::function<void (uint8_t*, float)> m_playerUpdateTask;
    bool           m_npcLimbs    = false;
    uint32_t       m_loggedLimbs = 0;
    uint32_t       m_loggedLivingReleases = 0;

    /**
     * \brief A living actor taken by a hand whose hit reaction runs here
     *
     * Kept after the hand lets go, until the reaction ends.
     */
    struct Driven {
      uint8_t* actor = nullptr;
      uint32_t refId = 0;
    };

    // Both hands' actors and two let go of
    constexpr static uint32_t MaxDriven = 4;

    bool                           m_drivesFeedback = false;
    std::array<Driven, MaxDriven>  m_driven;
    // Their ragdoll controllers, read by feedbackPoseHook on the thread
    // that animates the actors
    std::array<std::atomic<uint8_t*>, MaxDriven> m_drivenRagdolls = { };

    /**
     * \brief A living actor whose limbs follow the hands
     *
     * Its drive weights and blend are worked in the player's update and given
     * to the game by ragdollDriveHook on the thread that animates the actors.
     * Kept after the hands let go while the limbs go back to their animation.
     */
    struct Loose {
      std::atomic<uint8_t*> ragdoll = { nullptr };
      std::atomic<float>    interp  = { 0.0f };
      uint8_t* actor  = nullptr;
      uint32_t refId  = 0;
      /// The race's blend between the ragdoll's pose and the animation's
      float    blend  = 1.0f;
      /// Share of the way back to the animation
      float    back   = 0.0f;
      bool     held   = false;
      bool     logged = false;
      /// The weights in the hands, kept while the limbs go back
      std::array<float, VrRagdollMaxBodies> inHand = { };
    };

    bool                         m_canLoosen = false;
    std::array<Loose, MaxDriven> m_loose;
    uint32_t                     m_loggedLoose = 0;
    uint32_t                     m_loggedSecondHand = 0;

    /**
     * \brief An actor whose move the hands replace
     *
     * Worked in the player's update, read by actorMoveHook and moveModeHook
     * on the threads that update the actors. Both hands on an actor share
     * the first hand's.
     */
    struct Drag {
      std::atomic<uint8_t*> actor     = { nullptr };
      /// The actor can walk: not seated, not an immobile creature
      std::atomic<bool>     walks     = { false };
      std::atomic<bool>     setsHeading = { false };
      std::atomic<float>    velocityX = { 0.0f };
      std::atomic<float>    velocityY = { 0.0f };
      std::atomic<float>    heading   = { 0.0f };
      std::atomic<uint32_t> moveMode  = { 0u };

      VrNpcDrag logic;
      uint32_t  refId  = 0;
      bool      logged = false;
      /// The actor's cached walk and run speeds are the drag's
      bool      setsSpeeds = false;
    };

    std::array<Drag, VrHandCount> m_drags;
    bool m_canHoldInPlace = false;
    bool m_canDrag        = false;
    uint32_t m_loggedDrags = 0;

    /**
     * \brief A hand that let go of an actor, for a yank
     */
    struct LetGo {
      uint8_t*  actor  = nullptr;
      uint32_t  refId  = 0;
      VrVector3 velocity;
      float     age    = 0.0f;
      bool      fresh  = false;
      bool      kept   = false;
    };

    /**
     * \brief How angry an actor is who was handled lately
     */
    struct Handled {
      uint8_t*   actor = nullptr;
      uint32_t   refId = 0;
      bool       forgives = false;
      VrNpcAnger anger;
    };

    /**
     * \brief An actor the player's capsule passes through
     *
     * From when a hand takes it until it is clear of the player once let go.
     */
    struct PassedThrough {
      uint8_t* actor = nullptr;
      uint32_t refId = 0;
    };

    constexpr static uint32_t MaxHandled = 4;
    constexpr static uint32_t MaxPassedThrough = 4;

    std::array<VrHandMotion, VrHandCount> m_handMotion;
    std::array<VrVector3, VrHandCount>    m_handPositions;
    std::array<bool, VrHandCount>         m_hasHandPosition = { };
    std::array<LetGo, VrHandCount>        m_letGo;
    std::array<Handled, MaxHandled>       m_handled;
    std::array<PassedThrough, MaxPassedThrough> m_passedThrough;
    bool     m_canKnockDown     = false;
    bool     m_canPassThrough   = false;
    bool     m_canSetSpeeds     = false;
    bool     m_canShove         = false;
    bool     m_canSpendPoints   = false;
    float    m_shoveCooldown    = 0.0f;
    uint32_t m_loggedManhandling = 0;
    VrNpcPlayerBrake m_playerBrake;

    // Read by playerMoveHook and collisionFilterHook on the threads that
    // move the player and step Havok
    std::atomic<float>    m_playerSpeedLimit  = { 0.0f };
    std::atomic<float>    m_playerBrakeStrength = { 0.0f };
    std::atomic<uint32_t> m_playerGroup       = { 0u };
    std::array<std::atomic<uint32_t>, MaxPassedThrough> m_passedGroups = { };

    void update(uint8_t* player, float dt);

    void updateHandMotion(float dt);

    void noteLetGo(VrHand hand, const Held& held);

    void updateYanks(uint8_t* player, float dt);

    bool knockedDown(uint8_t* actor) const;

    bool knockDown(uint8_t* actor, const VrVector3& velocity);

    bool spendActionPoints(uint8_t* player, float cost) const;

    void updateEffort(uint8_t* player, float dt);

    void updateShoves(uint8_t* player, float dt);

    void shove(uint8_t* player, VrHand hand, const Candidate& touched, const VrVector3& velocity);

    Handled* handledEntry(uint8_t* actor);

    void updateAnger(float dt);

    void updatePassingThrough(uint8_t* player);

    void updateActorPhantom(uint8_t* actor) const;

    bool passesThrough(uint32_t filterA, uint32_t filterB) const;

    void setDragSpeeds(Drag& drag, uint8_t* actor, float speed);

    void clearDragSpeeds(Drag& drag, uint8_t* actor);

    void updateLead(uint8_t* player, float dt);

    void updateHand(uint8_t* player, VrHand hand, float dt);

    void updateCorrection(uint8_t* player, VrHand hand, Held& held, const VrGameCameraPose& pose);

    void fitFingers(uint8_t* player, VrHand hand, Held& held, const VrGameCameraPose& drawn);

    bool storable(const Held& held, VrHand hand) const;

    void store(uint8_t* player, VrHand hand, Held& held);

    bool grab(uint8_t* player, VrHand hand, const VrGameCameraPose& pose);

    bool probe(uint8_t* player, VrHand hand, const VrVector3& centre, Candidate& best);

    bool aimTarget(VrHand hand, const VrVector3& centre, Candidate& candidate);

    bool holdable(uint8_t* body, float maxMass, Candidate& candidate) const;

    bool attach(uint8_t* player, VrHand hand, const Candidate& candidate);

    void readSpring(uint8_t* hkWorld);

    void showGroupedBodies(bool own);

    uint32_t handsOn(const uint8_t* body) const;

    bool stillHeld(const Held& held) const;

    bool moveTarget(Held& held, const VrGameCameraPose& pose);

    bool startReaction(uint8_t* actor) const;

    bool keepReaction(Held& held);

    bool canHoldLiving() const;

    void driveReaction(uint8_t* actor);

    void updateDriven();

    bool drives(const uint8_t* ragdoll) const;

    void updateLoose(float dt);

    Loose* looseEntry(uint8_t* actor, uint8_t* ragdoll);

    void applyLoose(uint8_t* ragdoll) const;

    void updateDrags(uint8_t* player, float dt);

    void updateDrag(Drag& drag, uint8_t* player, const Held& held, const Held* other, float margin, float limit, float dt);

    void stopDrag(Drag& drag);

    const Drag* dragOf(const uint8_t* actor) const;

    void addTurn(Held& held);

    void updateTurn(VrHand hand, Held& held, const VrGameCameraPose& pose);

    void stopTurn(VrHand hand, Held& held);

    void release(Held& held);

    void restoreFilters(uint8_t* player);

    void updateHandBodies(uint8_t* player, float dt);

    void leaveCell(uint8_t* cell);

    static void __fastcall physicsGrabHook(
            uint8_t*              player,
            void*                 unused,
            float                 dt);

    static void __fastcall notInControlHook(
            uint8_t*              player,
            void*                 unused);

    static void __fastcall setParentCellHook(
            uint8_t*              player,
            void*                 unused,
            uint8_t*              cell);

    static void __fastcall feedbackPoseHook(
            uint8_t*              ragdoll,
            void*                 unused,
            uint32_t              stage);

    static void __fastcall ragdollDriveHook(
            uint8_t*              ragdoll,
            void*                 unused,
            uint32_t              stage);

    static void* __fastcall actorMoveHook(
            uint8_t*              actor,
            void*                 unused,
            float                 dt,
            VrVector3*            move,
            uint8_t               flags);

    static uint32_t __fastcall moveModeHook(
            uint8_t*              mover,
            void*                 unused);

    static void* __fastcall playerMoveHook(
            uint8_t*              player,
            void*                 unused,
            float                 dt,
            VrVector3*            move,
            uint8_t               flags);

    static bool* __fastcall collisionFilterHook(
            void*                 filter,
            void*                 unused,
            bool*                 result,
      const uint8_t*              a,
      const uint8_t*              b);

  };

}
