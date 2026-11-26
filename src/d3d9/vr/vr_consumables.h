#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vr_consumable_gesture.h"
#include "vr_game_hotkeys.h"
#include "vr_scene_model.h"

namespace dxvk {

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
    /// The right hand's position in the game world
    VrVector3        otherHand;
    /// The right hand's grip, which takes the cap
    bool             otherGrip = false;
    float            dt        = 0.0f;
  };

  enum class VrConsumablesEvent {
    None,
    /// The item came into the left hand
    Started,
    /// The right hand took the cap off
    CapTaken,
    /// The item was consumed
    Consumed,
    /// The item went back to the inventory: a menu opened, or the
    /// weapon was not holstered
    Cancelled,
  };

  /**
   * \brief Items of the quick wheel consumed by hand, with B42 Inject's
   *    animations
   *
   * See findings/consumables.md. Only for players who installed B42 Inject's
   * animations (and Hit's anim pack): an ingestible with an animation
   * folder comes into the left hand instead of being used at once. The
   * weapon is holstered meanwhile and drawn again after. The prop of the
   * animation (or the item's own model) is attached to the left hand bone,
   * the hand closed on it as the animation holds it; the right hand takes
   * its cap off; the item is consumed once it stayed at the mouth, the body
   * or in front of the eyes for the animation's time: the quick wheel's own
   * use runs then, with the item's consume sound muted (the animation's
   * sounds play instead), and the HUD says what was consumed. The left
   * hand keeps the item without a button held; opening a menu (the
   * Pip-Boy) puts it back. Without the files, or for a poison or the
   * doctor's bag, the item is used at once as before.
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
     * \param [in] hand Poses the left hand around the item, or \c nullptr
     */
    VrConsumablesEvent update(const VrConsumablesInput& input, VrWeaponDrawn* drawn, VrWeaponHand* hand);

    /**
     * \brief Whether an item is on its way or in the hand: the hands do
     *    nothing else meanwhile
     */
    bool busy() const {
      return m_state != State::Idle;
    }

  private:

    enum class State {
      Idle,
      Holstering,
      Holding,
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

    explicit VrConsumables(const VrTexts& texts);

    const VrTexts&                    m_texts;

    State                             m_state       = State::Idle;
    uint32_t                          m_slot        = 0;
    VrHotKeyItem                      m_item;
    const uint8_t*                    m_itemSound   = nullptr;
    const VrConsumableAnimation*      m_animation   = nullptr;
    VrGameTransform                   m_inHand;
    VrConsumeGestureState             m_gesture;
    VrConsumeZones                    m_zones;
    bool                              m_holstered   = false;
    float                             m_waited      = 0.0f;
    float                             m_clockPlayed = 0.0f;
    size_t                            m_nextUseSound = 0;
    uint32_t                          m_picks       = 0;

    VrSceneModel*                     m_prop        = nullptr;
    void*                             m_cap         = nullptr;
    void*                             m_capParent   = nullptr;
    void*                             m_capHand     = nullptr;
    void*                             m_capHold     = nullptr;
    bool                              m_capHeld     = false;
    bool                              m_otherDown   = false;
    std::vector<std::pair<void*, const VrConsumablePart*>> m_parts;

    std::vector<PendingSound>         m_pending;

    std::map<std::string, LoadedAnimation>               m_animations;
    std::map<std::string, std::unique_ptr<VrSceneModel>> m_props;

    const VrConsumableAnimation* findAnimation(const uint8_t* form);

    bool attachProp(VrWeaponHand* hand);

    void detachProp(VrWeaponHand* hand);

    bool takeCap();

    void dropCap();

    void poseParts(float time);

    void consume();

    void finish(VrWeaponDrawn* drawn, VrWeaponHand* hand);

    void queueSounds(const std::vector<VrConsumableSound>& sounds, float offset);

    void playPending(float dt);

    void playSound(const VrConsumableSound& sound);

  };

}
