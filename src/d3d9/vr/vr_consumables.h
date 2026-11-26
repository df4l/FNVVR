#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vr_consumable_gesture.h"
#include "vr_physics_grab.h"
#include "vr_game_hotkeys.h"
#include "vr_scene_model.h"

namespace dxvk {

  class VrPhysicsGrab;
  class VrTexts;
  class VrWeaponDrawn;
  class VrWeaponHand;

  /**
   * \brief What the player does in a frame, for an item consumed by hand
   */
  struct VrConsumablesInput {
    /// In the game, not in a menu: the gesture is cancelled otherwise
    bool             inGame    = false;
    /// The head in the game world (Z up)
    VrGameCameraPose head;
    /// The hands' positions in the game world
    std::array<VrVector3, VrHandCount> hands;
    /// The hands' grips: the hand without the item takes the cap
    std::array<bool, VrHandCount>      grips = { };
    /// A gun or a melee weapon is drawn in the right hand: it stays there
    /// for an item the left hand consumes alone
    bool             armed     = false;
    float            dt        = 0.0f;
  };

  enum class VrConsumablesEvent {
    None,
    /// The item came into the hand
    Started,
    /// The other hand took the cap off
    CapTaken,
    /// The teeth took the cap off
    CapBitten,
    /// The item was consumed
    Consumed,
    /// The item went back to the inventory: a menu opened, or the
    /// weapon was not holstered; or the game refused to pick it up
    Cancelled,
  };

  /**
   * \brief Items consumed by hand, with B42 Inject's animations
   *
   * See findings/consumables.md. Only for players who installed B42 Inject's
   * animations (and Hit's anim pack). Without the files, or for a poison or
   * the doctor's bag, items are used at once as before.
   *
   * From the quick wheel, an ingestible with an animation folder comes into
   * the left hand instead of being used at once. The weapon is holstered
   * meanwhile and drawn again after, unless the left hand consumes the item
   * alone (no cap, or a cork the teeth pull) beside a gun or a melee
   * weapon. The prop of the animation (or the item's own model) is attached
   * to the hand bone, the hand closed on it as the animation holds it; the
   * other hand takes its cap off, or the teeth for a cork the animation
   * spits out (then it flies off the mouth); the item is consumed once it
   * stayed at the mouth, the body or in front of the eyes for the
   * animation's time: the game's own use runs then, with the item's consume
   * sound muted (the animation's sounds play instead), and the HUD says
   * what was consumed. The hand keeps the item without a button held;
   * opening a menu (the Pip-Boy) puts it back in the inventory.
   *
   * An item of the world held by one hand (VrPhysicsGrab) is consumed the
   * same way, without leaving the hand: without a cap, the held object
   * itself is brought where it is consumed and kept there for the
   * animation's time, then the game picks it up and uses it; with one, the
   * other hand's grip on it, or a cork brought to the mouth, has the game
   * pick it up and the prop takes its place in the hand (it slides from
   * the object's place to the animation's), its cap already in that hand
   * or between the teeth. Until then it stays an object like any other.
   * In the right hand, the animation's left hand hold is mirrored. A needle
   * held from the world goes into the other arm or below the chest, where
   * letting go of it does not put it in the inventory.
   */
  class VrConsumables {

  public:

    /**
     * \brief Checks the game's functions
     *
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrConsumables> install(const VrTexts& texts);

    ~VrConsumables();

    VrConsumables(const VrConsumables&) = delete;
    VrConsumables& operator = (const VrConsumables&) = delete;

    /**
     * \brief Starts consuming an item chosen on the quick wheel by hand
     *
     * \param [in] slot The item's hotkey slot
     * \param [in] item The item
     * \returns \c false if the item is used at once: not an ingestible, a
     *    poison, without an animation, or a gesture is already going on
     */
    bool begin(uint32_t slot, const VrHotKeyItem& item);

    /**
     * \brief Runs the gesture for a frame
     *
     * \param [in] input What the player does
     * \param [in] drawn Holsters and draws the weapon, or \c nullptr
     * \param [in] hand Poses the hands around the item, or \c nullptr
     * \param [in] grab The hands' hold on world objects, or \c nullptr
     */
    VrConsumablesEvent update(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand,
      VrPhysicsGrab* grab);

    /**
     * \brief Whether an item is on its way or in a hand: the hands do
     *    nothing else meanwhile, unless the weapon stayed drawn
     */
    bool busy() const {
      return m_state != State::Idle;
    }

    /**
     * \brief Whether a hand holds an item to consume, or is about to
     */
    bool holds(VrHand hand) const {
      return busy() && m_hand == hand;
    }

    /**
     * \brief The hand an event is felt in: the item's, or the other one
     *    for its cap
     */
    VrHand eventHand(VrConsumablesEvent event) const {
      return event == VrConsumablesEvent::CapTaken ? VrHand(uint32_t(m_hand) ^ 1u) : m_hand;
    }

  private:

    enum class State {
      Idle,
      Holstering,
      PickingUp,
      Holding,
    };

    /// What follows the game's pick up of a held item
    enum class PickUp {
      /// Used at once, the gesture was made with the object
      Consume,
      /// Taken in the hand, the cap in the other one
      CapInHand,
      /// Taken in the hand, the cork at the mouth
      CapAtMouth,
    };

    struct PendingSound {
      float          delay = 0.0f;
      std::string    path;
      const uint8_t* sound = nullptr;
    };

    struct LoadedAnimation {
      bool                  valid = false;
      VrConsumableAnimation animation;
    };

    /// An item of the world a hand holds
    struct Watch {
      uint8_t*                     ref       = nullptr;
      const uint8_t*               base      = nullptr;
      const VrConsumableAnimation* animation = nullptr;
      /// Without a cap, the gesture with the held object
      VrConsumeGestureState        gesture;
      size_t                       nextUseSound = 0;
      bool                         otherDown = false;
      /// Refused once (full health): not again until held anew
      bool                         refused   = false;
    };

    explicit VrConsumables(const VrTexts& texts);

    const VrTexts&                    m_texts;

    /// Items held from the world can be used without a hotkey
    bool                              m_worldItems  = false;
    /// A weapon stayed drawn in the right hand in the last frame
    bool                              m_armed       = false;
    /// The left hand held an object of the world in the last frame
    bool                              m_leftHolds   = false;

    State                             m_state       = State::Idle;
    VrHand                            m_hand        = VrHand::Left;
    /// From the quick wheel's slot, or an item of the world
    bool                              m_fromWheel   = true;
    uint32_t                          m_slot        = 0;
    VrHotKeyItem                      m_item;
    const uint8_t*                    m_itemSound   = nullptr;
    const VrConsumableAnimation*      m_animation   = nullptr;
    VrGameTransform                   m_inHand;
    VrSequencePose                    m_propPose;
    const std::vector<VrBonePose>*    m_fingers     = nullptr;
    VrConsumeGestureState             m_gesture;
    VrConsumeZones                    m_zones;
    bool                              m_holstered   = false;
    bool                              m_keepWeapon  = false;
    float                             m_waited      = 0.0f;
    size_t                            m_nextUseSound = 0;
    uint32_t                          m_picks       = 0;

    /// A held item being picked up, and where the prop starts from
    PickUp                            m_pickUp      = PickUp::Consume;
    bool                              m_hasSlide    = false;
    VrSequencePose                    m_slideFrom;
    float                             m_slideTime   = 0.0f;
    bool                              m_capNow      = false;

    std::array<Watch, VrHandCount>    m_watches;

    VrSceneModel*                     m_prop        = nullptr;
    void*                             m_cap         = nullptr;
    void*                             m_capParent   = nullptr;
    void*                             m_capHand     = nullptr;
    void*                             m_capHold     = nullptr;
    bool                              m_capHeld     = false;
    bool                              m_otherDown   = false;

    /// A bitten cap: hidden in the mouth, then spat out
    bool                              m_spitting    = false;
    bool                              m_spitFlying  = false;
    float                             m_spitTime    = 0.0f;
    float                             m_spitDelay   = 0.0f;
    VrVector3                         m_spitFrom;
    VrVector3                         m_spitVelocity;
    VrVector3                         m_capMesh;
    /// The item's bound before the bite, in its root's frame: the flying
    /// cork would stretch it
    VrVector3                         m_spitBound;
    float                             m_spitRadius  = 0.0f;
    std::vector<std::pair<void*, const VrConsumablePart*>> m_parts;

    std::vector<PendingSound>         m_pending;

    std::map<std::string, LoadedAnimation>               m_animations;
    std::map<std::string, std::unique_ptr<VrSceneModel>> m_props;

    /// The animations' holds mirrored for the right hand
    std::map<const VrConsumableAnimation*, VrConsumableHold> m_mirrored;

    const VrConsumableAnimation* findAnimation(const uint8_t* form);

    const VrConsumableAnimation* consumableAnimation(const uint8_t* form);

    VrConsumablesEvent watchWorld(const VrConsumablesInput& input, VrWeaponHand* hand, VrPhysicsGrab* grab);

    VrConsumablesEvent watchHand(const VrConsumablesInput& input, VrWeaponHand* hand, VrPhysicsGrab* grab,
      VrHand side, const VrHeldItem& held);

    void startPickUp(VrWeaponHand* hand, VrPhysicsGrab* grab, VrHand side, uint8_t* object, PickUp kind);

    VrConsumablesEvent endPickUp(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand,
      VrPhysicsGrab* grab);

    bool startHolding(VrWeaponHand* hand);

    VrConsumablesEvent hold(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand);

    bool refusesNow(const uint8_t* form) const;

    bool attachProp(VrWeaponHand* hand);

    void detachProp(VrWeaponHand* hand);

    bool takeCap(VrWeaponHand* hand, const VrGameTransform& toWorld);

    void dropCap();

    void biteCap();

    void flySpitCap(const VrGameCameraPose& head, float dt, const VrGameTransform& toWorld);

    void poseParts(float time);

    void slideProp(float dt);

    void consume();

    void finish(VrWeaponDrawn* drawn, VrWeaponHand* hand);

    void queueSounds(const std::vector<VrConsumableSound>& sounds, float offset);

    void playPending(float dt);

    void playSound(const VrConsumableSound& sound);

    void playUseSounds(const VrConsumableAnimation& animation, float clock, size_t& next);

  };

}
