#pragma once

#include <array>
#include <cstdint>

#include "vr_menu_pointer.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The values of Caravan's six tracks, where they lie on the mat
   */
  struct VrCaravanBoard {
    constexpr static uint32_t Tracks      = 6;
    constexpr static uint32_t ImageSize   = 512;
    /// A track's value over this has bust
    constexpr static int32_t  MostValue   = 26;

    /// The square of the mat the values are drawn on, facing up
    VrPanelPlacement placement;
    /// Value of each track, -1 for a track without a card
    std::array<int32_t, Tracks>   values = { -1, -1, -1, -1, -1, -1 };
    /// Centre of each value in the image, (0, 0) top left, (1, 1) bottom right
    std::array<VrVector2, Tracks> spots  = { };
  };

  /**
   * \brief A stick step Caravan's menu is asked to take
   */
  enum class VrCaravanKey : uint32_t {
    None,
    Left,
    Right,
    Up,
  };

  /**
   * \brief What to write in Caravan's menu so that one step of its own
   *    lands on a target
   *
   * The menu takes the step itself (sound, raised card, cursor, the check
   * of the placement): findings/caravan.md §3.3.
   */
  struct VrCaravanStep {
    /// Selection to write before the step: a hand card or a row
    int32_t      selection = 0;
    VrCaravanKey key       = VrCaravanKey::None;
  };

  /**
   * \brief Finds the point nearest to where the laser meets the table
   *
   * \param [in] points Points on the table, in its plane
   * \param [in] count Number of points
   * \param [in] at Where the laser meets the table
   * \param [in] reach Farthest a point may be from \c at
   * \param [in] current The point chosen so far, -1 if none: it keeps the
   *    laser until another is nearer by \c stickiness
   * \param [in] stickiness Distance the chosen point is favoured by
   * \returns Index of the point, -1 if none is within reach
   */
  int32_t vrNearestPoint(
    const VrVector2*  points,
          uint32_t    count,
    const VrVector2&  at,
          float       reach,
          int32_t     current,
          float       stickiness);

  /**
   * \brief Where a card shape lies, from its world transform
   *
   * The game's layout: a point of the card is placed at
   * rotate * (scale * point) + translate, rotate row-major. The card is the
   * rectangle of its own plane z = 0 within the half sizes.
   */
  struct VrCaravanCard {
    float     rotate[3][3] = { };
    VrVector3 translate    = { };
    float     scale        = 1.0f;
    float     halfX        = 0.0f;
    float     halfY        = 0.0f;
  };

  /**
   * \brief A transform of the game's scene graph, laid out as the game's
   *
   * A point p goes to rotate * (scale * p) + translate, rotate row-major.
   */
  struct VrCaravanTransform {
    float     rotate[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    VrVector3 translate    = { };
    float     scale        = 1.0f;
  };

  /**
   * \brief Carries the player's hand of cards from the table to a hand
   *
   * The game lays the hand on the table, faces up (+Z), tips towards the
   * opponent (+Y), the first card on the left (-X). The result moves the
   * point \c centre of that fan to \c hold, its tips along \c up and its
   * faces as close to \c face as the tips let them, at \c scale times its
   * size. All in the scene.
   *
   * \param [in] centre Point of the fan on the table the hand holds
   * \param [in] hold Where the hand holds it
   * \param [in] up Direction of the tips, not 0
   * \param [in] face Direction the faces turn to, not along \c up
   * \param [in] scale Size in the hand over size on the table
   */
  VrCaravanTransform vrCaravanFanInHand(
    const VrVector3&      centre,
    const VrVector3&      hold,
    const VrVector3&      up,
    const VrVector3&      face,
          float           scale);

  /**
   * \brief Opens the game's fan of cards wider
   *
   * The result turns a card of the fan, on the table, about +Z around
   * \c pivot by \c factor - 1 times the angle of its long axis (+X of the
   * card) from \c middle, so that the cards fan out \c factor times as
   * wide. Angles in radians, from +Y, positive towards -X.
   *
   * \param [in] card World transform of the card's node
   * \param [in] pivot Point the fan's cards turn about
   * \param [in] middle Angle of the fan's middle
   * \param [in] factor Width of the opened fan over the game's
   */
  VrCaravanTransform vrCaravanSpreadCard(
    const VrCaravanTransform& card,
    const VrVector3&      pivot,
          float           middle,
          float           factor);

  /**
   * \brief Applies a transform to a point
   */
  VrVector3 vrCaravanTransformPoint(const VrCaravanTransform& transform, const VrVector3& point);

  /**
   * \brief Applies \c outer after \c inner, as a parent's world transform
   *    to a child's
   */
  VrCaravanTransform vrCaravanCompose(const VrCaravanTransform& outer, const VrCaravanTransform& inner);

  /**
   * \brief Where a ray meets a card
   *
   * \param [in] origin The ray's origin
   * \param [in] direction The ray's direction, not 0
   * \param [in] card The card
   * \returns Distance along the ray in lengths of \c direction, negative
   *    when the ray misses the card or meets it behind its origin
   */
  float vrRayMeetsCard(
    const VrVector3&      origin,
    const VrVector3&      direction,
    const VrCaravanCard&  card);

  /**
   * \brief The card a ray meets first
   *
   * The cards of a hand overlap: the first one met is the one seen there.
   *
   * \returns Index of the card, -1 if the ray meets none
   */
  int32_t vrFirstCardOnRay(
    const VrCaravanCard*  cards,
          uint32_t        count,
    const VrVector3&      origin,
    const VrVector3&      direction);

  /**
   * \brief The step from the card before the target to the target
   *
   * The menu steps right through its hand and wraps at the end.
   *
   * \param [in] target Hand card to select
   * \param [in] current Hand card selected now
   * \param [in] size Cards in the hand
   * \returns No key when nothing is to change
   */
  VrCaravanStep vrCaravanHandStep(int32_t target, int32_t current, int32_t size);

  /**
   * \brief The step from the row before the target to the target
   *
   * Up counts the player's rows down (towards the opponent) and the
   * opponent's rows up, and the track is kept.
   *
   * \param [in] track Track of the target, 0-2 the player's, 3-5 the opponent's
   * \param [in] row Row of the target
   */
  VrCaravanStep vrCaravanSlotStep(uint32_t track, int32_t row);

  /**
   * \brief The step towards a track while a whole track is discarded
   *
   * The menu moves one track per step, right towards the higher one.
   *
   * \param [in] target Track to select
   * \param [in] current Track selected now
   */
  VrCaravanKey vrCaravanTrackKey(int32_t target, int32_t current);

  /**
   * \brief A card the game is placing on a row, all -1 for none
   */
  struct VrCaravanPlacing {
    int32_t track  = -1;
    int32_t row    = -1;
    int32_t column = -1;
  };

  /**
   * \brief Checks whether a card of a row model is in the game
   *
   * A row model holds a card per column; those past the cards on the row
   * wait at their bind pose, except the one being placed.
   * \param [in] track Track of the row model
   * \param [in] row Row of the row model
   * \param [in] column Column of the card in the model
   * \param [in] cardsOnRow Cards the game has on that row
   * \param [in] placing The card the game is placing
   */
  bool vrCaravanCardInGame(
          uint32_t          track,
          uint32_t          row,
          uint32_t          column,
          uint32_t          cardsOnRow,
    const VrCaravanPlacing& placing);

  /**
   * \brief What the controller buttons ask of Caravan's game
   */
  enum class VrCaravanCommand : uint32_t {
    None,
    /// Discard the selected hand card (the gamepad's LT)
    DiscardCard,
    /// Start discarding a whole caravan (the gamepad's RT)
    DiscardTrack,
    /// Put back the taken card, leave a caravan's discard (the gamepad's B)
    Cancel,
  };

  /**
   * \brief Caravan's controller buttons during the game
   *
   * B (back) discards the selected hand card on a tap and starts a whole
   * caravan's discard once held for \ref HoldTime; while a slot is being
   * chosen it cancels at once, as Y (option) always does. A press that gave
   * a command gives no other until released.
   */
  class VrCaravanButtons {

  public:

    constexpr static float HoldTime = 0.6f;

    /**
     * \param [in] back B is down
     * \param [in] option Y is down
     * \param [in] choosesSlot A hand card is taken or a caravan is being
     *    discarded: the menu's \c SELECT_TRACK flag
     * \param [in] dt Seconds since the last update
     */
    VrCaravanCommand update(bool back, bool option, bool choosesSlot, float dt);

    /// Forgets the buttons' state, e.g. while the game does not take them
    void reset() { *this = VrCaravanButtons(); }

  private:

    bool  m_back       = false;
    bool  m_backUsed   = false;
    float m_backHeld   = 0.0f;
    bool  m_option     = false;

  };

  /**
   * \brief The two rows of cards of Caravan's deck building
   */
  enum class VrCaravanDeckRow : uint32_t {
    /// The cards in the player's deck: a click takes one out
    Deck,
    /// The cards out of it: a click puts one in
    Available,
  };

  /**
   * \brief What the deck building is asked this frame
   */
  struct VrCaravanDeckAction {
    /// Left stick X to hold: -1, 0 or 1, one card per step of the menu
    int32_t stick  = 0;
    /// Press the gamepad's A (add the current card to the deck)
    bool    add    = false;
    /// Press the gamepad's X (take the current card out of the deck)
    bool    remove = false;
  };

  /**
   * \brief Brings a clicked card of Caravan's deck building to the middle,
   *    then puts it in or takes it out of the deck
   *
   * Both rows show the collection from the card at the deck meter's value
   * minus \ref CurrentSlot; only the card in the middle (the meter's value)
   * is added or removed. The menu scrolls one card per stick step and
   * animation, with its sound: the stick is held towards the clicked card
   * until the meter reaches it (findings/caravan.md §5 step 4).
   */
  class VrCaravanDeckPicker {

  public:

    constexpr static int32_t Slots       = 24;
    constexpr static int32_t CurrentSlot = 11;
    /// Seconds without the meter moving before the scroll is given up
    constexpr static float   StallTime   = 2.0f;

    /**
     * \brief A click on a card of a row
     *
     * \param [in] row The row clicked
     * \param [in] slot Slot of the card in the row, 0 to \ref Slots - 1
     * \param [in] current The deck meter's value: the card in the middle
     * \param [in] total Cards in the collection
     * \returns Whether the slot holds a card of the collection
     */
    bool click(VrCaravanDeckRow row, int32_t slot, int32_t current, int32_t total);

    /**
     * \brief What to do this frame
     *
     * \param [in] current The deck meter's value
     * \param [in] inDeck Whether the clicked card is in the deck now
     * \param [in] idle The menu takes input (its state 0)
     * \param [in] dt Seconds since the last update
     */
    VrCaravanDeckAction update(int32_t current, bool inDeck, bool idle, float dt);

    /// The clicked card's index in the collection, -1 for none
    int32_t target() const { return m_target; }

    void reset() { *this = VrCaravanDeckPicker(); }

  private:

    int32_t          m_target  = -1;
    VrCaravanDeckRow m_row     = VrCaravanDeckRow::Deck;
    int32_t          m_last    = -1;
    float            m_stalled = 0.0f;

  };

}
