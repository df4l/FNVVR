#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "vr_grab.h"
#include "vr_grab_fingers.h"
#include "vr_hand_body.h"
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
   * level of detail). The actor reacts to being grabbed (VrNpcReaction). A hand that touches the object the other hand holds
   * holds it too, with a second spring on the same body.
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
    float     m_fullAcceleration = 0.0f;
    float     m_gravity          = 0.0f;
    VrVector3 m_playerPosition;
    VrVector3 m_lead;
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

    void update(uint8_t* player, float dt);

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

    bool keepReaction(const Held& held) const;

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

  };

}
