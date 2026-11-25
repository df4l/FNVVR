#pragma once

#include "vr_consumable_animation.h"
#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief Where an item is consumed, in metres
   */
  struct VrConsumeZones {
    /// The mouth below and in front of the eyes
    float mouthBelow     = 0.08f;
    float mouthAhead     = 0.06f;
    /// How near the mouth the item's bound comes
    float mouthReach     = 0.06f;
    /// The body: below the eyes, near the head's vertical
    float bodyBelowMin   = 0.15f;
    float bodyBelowMax   = 0.95f;
    float bodyAround     = 0.35f;
    /// Or the other hand's forearm, near its controller
    float armReach       = 0.12f;
    /// A magazine in front of the eyes
    float readNear       = 0.15f;
    float readFar        = 0.60f;
    float readCone       = 0.5f;
    /// Further out than in before the item has left
    float leaveMargin    = 0.05f;
    /// The largest bound radius counted
    float largestRadius  = 0.15f;
    /// How near the cap the other hand takes it
    float capReach       = 0.08f;
  };

  /**
   * \brief Whether an item is where it is consumed
   *
   * \param [in] gesture How it is consumed
   * \param [in] head Head pose in the game world (Z up)
   * \param [in] centre Centre of the item's bound in the game world
   * \param [in] radius Radius of the bound, game units
   * \param [in] otherHand The right hand's position in the game world
   * \param [in] margin Metres added to the reaches
   * \param [in] zones The zones
   */
  bool vrIsWhereConsumed(
          VrConsumeGesture      gesture,
    const VrGameCameraPose&     head,
    const VrVector3&            centre,
          float                 radius,
    const VrVector3&            otherHand,
          float                 margin,
    const VrConsumeZones&       zones);

  /**
   * \brief What the controllers and the item are doing in a frame
   */
  struct VrConsumeInput {
    VrGameCameraPose head;
    /// The item's bound in the game world
    VrVector3        centre;
    float            radius = 0.0f;
    /// The cap's place in the game world, if it is still on
    VrVector3        cap;
    /// The right hand's position in the game world
    VrVector3        otherHand;
    /// The grips: the left hand's holds the item, the right's takes the cap
    bool             grip      = false;
    bool             otherGrip = false;
    float            dt        = 0.0f;
  };

  enum class VrConsumeEvent {
    None,
    /// The right hand took the cap off
    CapTaken,
    /// The right hand let the cap go
    CapDropped,
    /// The item came where it is consumed, the use clock starts or goes on
    Arrived,
    /// The item left before the end, the clock waits
    Left,
    /// The use clock reached its end: the item is consumed
    Consumed,
    /// The left hand let the item go before the end: it is not consumed
    Cancelled,
  };

  /**
   * \brief The gesture of consuming an item held in the left hand
   *
   * The item comes into the left hand from the quick wheel. With a cap,
   * the right hand takes it off first (grip near the cap). Then the item is
   * brought where it is consumed and held there for the use clock's time;
   * it may leave and come back, the clock waits. Letting the left grip go
   * once it was pressed, or never pressing it, cancels.
   */
  class VrConsumeGestureState {

  public:

    /// Seconds the left grip has to be pressed in, from the start
    static constexpr float GripWait = 4.0f;

    /**
     * \brief Starts with an item in the left hand
     *
     * \param [in] hasCap Whether the right hand takes a cap off first
     * \param [in] useSeconds Seconds the item stays where it is consumed
     */
    void start(VrConsumeGesture gesture, bool hasCap, float useSeconds);

    /**
     * \brief Ends the gesture without an event
     */
    void reset();

    VrConsumeEvent update(const VrConsumeInput& input, const VrConsumeZones& zones);

    bool active() const {
      return m_active;
    }

    bool capOn() const {
      return m_hasCap && !m_capTaken;
    }

    bool capInHand() const {
      return m_capTaken && m_capHeld;
    }

    bool atTarget() const {
      return m_atTarget;
    }

    /// Seconds on the use clock
    float clock() const {
      return m_clock;
    }

  private:

    VrConsumeGesture m_gesture    = VrConsumeGesture::Mouth;
    bool             m_active     = false;
    bool             m_hasCap     = false;
    bool             m_capTaken   = false;
    bool             m_capHeld    = false;
    bool             m_gripped    = false;
    bool             m_otherDown  = false;
    bool             m_atTarget   = false;
    float            m_useSeconds = 0.0f;
    float            m_clock      = 0.0f;
    float            m_waited     = 0.0f;

  };

}
