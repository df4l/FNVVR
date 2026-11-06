#pragma once

#include <array>
#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Rectangle in the game's interface
   *
   * In interface units or pixels, the vertical axis grows downwards.
   */
  struct VrInterfaceRect {
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;

    float width() const {
      return right - left;
    }

    float height() const {
      return bottom - top;
    }

    bool isEmpty() const {
      return right <= left || bottom <= top;
    }
  };

  /**
   * \brief Size of the game's interface in its own units
   *
   * The interface is laid out in units where the shorter side of the
   * screen is 960 (landscape) or 1280 (portrait) units long, whatever the
   * resolution, see findings/hud.md.
   *
   * \param [in] width Width of the render target, in pixels
   * \param [in] height Height of the render target, in pixels
   */
  VrVector2 vrComputeInterfaceSize(uint32_t width, uint32_t height);

  /**
   * \brief How a text tile is placed on its position
   */
  enum class VrTextAlign : uint32_t {
    Left,
    Centre,
    Right,
  };

  /**
   * \brief Rectangle covered by a text tile
   *
   * The game lays a text out from the tile's position and stores the size
   * it takes in the tile's width and height. A centred text is centred on
   * that position and a right-aligned one ends there. See
   * findings/hud.md §15.
   *
   * \param [in] position Position of the tile, in interface units
   * \param [in] size Width and height of the text, in interface units
   * \param [in] align How the text is placed on its position
   */
  VrInterfaceRect vrComputeTextRect(
    const VrVector2&            position,
    const VrVector2&            size,
          VrTextAlign           align);

  /**
   * \brief Smallest rectangle holding both rectangles
   *
   * An empty rectangle holds nothing, so the other one is returned.
   */
  VrInterfaceRect vrUniteRects(
    const VrInterfaceRect&      a,
    const VrInterfaceRect&      b);

  /**
   * \brief What the hand HUD groups actually draw, when it was measured
   *
   * Rectangles in interface units, \c nullptr when unknown.
   */
  struct VrHandHudContent {
    const VrInterfaceRect* hitPoints    = nullptr;
    const VrInterfaceRect* actionPoints = nullptr;
    const VrInterfaceRect* info         = nullptr;
  };

  /**
   * \brief Parts of the interface image cut out for the hand HUD panels
   */
  struct VrHandHudCrops {
    /// Health bar and compass, for the back of the right hand
    VrInterfaceRect wrist;
    /// Action points, ammunition and weapon condition, shown below them
    VrInterfaceRect actionPoints;
    /// Interaction prompt, for the fingertip: the rectangle holding its parts
    VrInterfaceRect prompt;
    /// Parts of the prompt, each copied to the same place relative to \c prompt
    std::array<VrInterfaceRect, 2> promptParts;
    uint32_t                       promptPartCount = 0;
  };

  /**
   * \brief Computes where the hand HUD elements are in the interface image
   *
   * The elements are drawn in the same interface pass, so each crop only
   * has to hold its own element and must not reach into the others. The
   * game puts the health bar and compass in the bottom left corner, the
   * action points and ammunition in the bottom right corner and the prompt
   * at the bottom centre. The crops of the corners are their group's
   * rectangle with a margin, a wider one on the left of the action points,
   * where the weapon condition's label moves when an ammunition type is
   * shown.
   *
   * The prompt's crop is what its texts and images cover, with a small
   * margin, when that was measured: a prompt the game's data replaces
   * (an activator's own action, a long name) can be wider or taller than
   * its group. Its rows level with the corners' content are narrowed so
   * that they reach neither corner; the rows above keep their width, so the
   * prompt is cut in two parts then. Without a measure, the crop is
   * centred on the group, from above it to the bottom of the screen, and
   * narrowed the same way, in one part.
   *
   * \param [in] hitPoints Rectangle of the HitPoints group, in interface units
   * \param [in] actionPoints Rectangle of the ActionPoints group, in interface units
   * \param [in] info Rectangle of the Info group, in interface units
   * \param [in] size Size of the interface, in interface units
   * \param [in] content What the groups draw, where it was measured
   */
  VrHandHudCrops vrComputeHandHudCrops(
    const VrInterfaceRect&      hitPoints,
    const VrInterfaceRect&      actionPoints,
    const VrInterfaceRect&      info,
    const VrVector2&            size,
    const VrHandHudContent&     content = VrHandHudContent());

  /**
   * \brief Layout of the interaction prompt's image
   *
   * Rectangles and size are in pixels.
   */
  struct VrPromptImage {
    VrVector2                      size = { 0.0f, 0.0f };
    std::array<VrInterfaceRect, 2> targets;
  };

  /**
   * \brief Places the prompt's parts in its image
   *
   * The parts keep their places relative to each other, and the whole is
   * centred in the image, so that the prompt stays centred on the point
   * the panel is placed at whatever its size. The image is at least
   * \c minimum large, so that it can keep the size it had while the
   * prompt changes, and is made only larger when a prompt needs it.
   *
   * \param [in] parts The prompt's parts in the HUD image, in pixels
   * \param [in] count Number of parts, two at most
   * \param [in] minimum Smallest size of the image, in pixels
   */
  VrPromptImage vrComputePromptImage(
    const VrInterfaceRect*      parts,
          uint32_t              count,
    const VrVector2&            minimum);

  /**
   * \brief Layout of the wrist display's image
   *
   * The health bar and compass are on top, towards the fingers, and the
   * action points and ammunition below them, towards the forearm. Both are
   * centred horizontally. Rectangles and size are in interface units.
   */
  struct VrWristStack {
    VrVector2       size = { 0.0f, 0.0f };
    VrInterfaceRect health;
    VrInterfaceRect actionPoints;

    /**
     * \brief How far the image's centre lies below the health bar's, in interface units
     */
    float healthDrop() const {
      return 0.5f * size.y - 0.5f * (health.top + health.bottom);
    }
  };

  /**
   * \brief Stacks the crops of the wrist display into one image
   *
   * \param [in] health Crop of the health bar and compass
   * \param [in] actionPoints Crop of the action points and ammunition
   */
  VrWristStack vrComputeWristStack(
    const VrInterfaceRect&      health,
    const VrInterfaceRect&      actionPoints);

  /**
   * \brief Where the hand HUD panels are placed and how large they are
   *
   * Offsets are in metres along the hand's forward, up and right axes, in
   * the frame the hands are drawn with (vrComputeControllerPoint).
   */
  struct VrHandHudLayout {
    /// The health bar and compass are on the back of the right hand, which
    /// faces right when the controller is held upright
    constexpr static VrVector3 WristOffset = { -0.02f, 0.02f, 0.05f };
    /// The display is turned from the hand's right axis towards its up
    /// axis by this angle, in radians (30 degrees), so that a slight turn of
    /// the wrist shows it
    constexpr static float WristFaceUpAngle = 0.5235988f;
    /// Panel size per interface unit, in metres: the health bar's group is
    /// about 13 cm wide
    constexpr static float WristMetresPerUnit = 0.00035f;

    /// The prompt is above the index fingertip of the hand nearest the target
    constexpr static VrVector3 FingertipOffset = { 0.08f, 0.0f, 0.0f };
    /// Height of the prompt's centre above the fingertip, in metres
    constexpr static float PromptLift = 0.07f;
    /// Panel size per interface unit, in metres: about the angular size of
    /// the HUD panel in front of the head, for a hand half a metre away
    constexpr static float PromptMetresPerUnit = 0.0006f;
  };

  /**
   * \brief Pose of the health bar and compass panel on the back of a hand
   *
   * The panel's up axis runs along the hand towards the fingers, like the
   * dial of a watch, and the panel faces outwards from the back of the hand.
   * The health bar stays at WristOffset, and the panel's centre is moved
   * towards the forearm so that what is drawn below it hangs from there.
   *
   * \param [in] hand Right hand controller
   * \param [in] drop Distance from the health bar's centre down to the
   *    panel's centre, in metres
   */
  VrPose vrComputeWristPanelPose(const VrControllerState& hand, float drop);

  /**
   * \brief Pose of the interaction prompt panel at a hand's fingertip
   *
   * The panel is above the fingertip, upright and facing the head.
   */
  VrPose vrComputePromptPanelPose(const VrControllerState& hand, const VrPose& head);

  /**
   * \brief Shows the wrist display while the player looks at it
   *
   * The display must be in the middle of the view, close to the head, and
   * turned towards the eyes, so that it stays hidden while the weapon is
   * aimed. It appears after \ref ShowDelay and disappears once the player
   * has looked away for \ref HideDelay.
   */
  class VrWristGaze {

  public:

    /// Largest angle between the head's forward axis and the display, in radians (30 degrees)
    constexpr static float MaxAngle    = 0.5235988f;
    /// Largest distance between the head and the display, in metres
    constexpr static float MaxDistance = 0.8f;
    /// Largest angle between the display's normal and the line to the head, in radians (60 degrees)
    constexpr static float MaxFacingAngle = 1.0471976f;
    /// How long the display is looked at before it appears, in seconds
    constexpr static float ShowDelay   = 0.1f;
    /// How long the player looks away before it disappears, in seconds
    constexpr static float HideDelay   = 0.4f;

    /**
     * \brief Updates the display's visibility for a frame
     *
     * \param [in] head Head pose, \c nullptr if it is not tracked
     * \param [in] panel Display pose in the same space, \c nullptr if the
     *    hand is not tracked
     * \param [in] dt Time since the previous frame, in seconds
     * \returns \c true if the display is shown
     */
    bool update(const VrPose* head, const VrPose* panel, float dt);

    void reset();

  private:

    bool  m_visible  = false;
    float m_lookTime = 0.0f;
    float m_awayTime = 0.0f;

  };

  /**
   * \brief What one hand's activation pick found
   */
  struct VrHandPickResult {
    /// The hand is tracked and its ray was cast
    bool tracked = false;
    /// The ray found something to activate
    bool hit     = false;
    /// The game took a body near the ray because the ray itself missed
    bool fuzzy   = false;
  };

  /**
   * \brief Chooses the hand whose pick names the activation target
   *
   * Each tracked hand casts the game's activation pick along its ray. A
   * hand that finds a target wins over one that does not, and a ray that
   * reaches its target wins over a fuzzy one that only passes near a body.
   * Otherwise the previous hand keeps the target, so that it does not jump
   * between the hands while both point at something.
   *
   * \param [in] picks Result of each hand, indexed by VrHand
   * \param [in] previous Hand chosen on the previous pick
   * \returns The chosen hand, a tracked one if either is tracked
   */
  VrHand vrChooseActivateHand(
    const std::array<VrHandPickResult, VrHandCount>& picks,
          VrHand                  previous);

  /**
   * \brief Chooses the hand the interaction prompt is shown at
   *
   * The hand nearest the target is chosen. The prompt only moves to the
   * other hand once that one is clearly nearer, so that it does not jump
   * between the hands while they are about as far.
   */
  class VrPromptHand {

  public:

    /// How much nearer the other hand must be before the prompt moves, in metres
    constexpr static float SwitchMargin = 0.05f;

    /**
     * \brief Chooses the hand for a frame
     *
     * \param [in] controllers Hand controllers, in tracking space
     * \param [in] target Target in tracking space
     * \param [out] hand The chosen hand
     * \returns \c false if no hand is tracked
     */
    bool update(
      const std::array<VrControllerState, VrHandCount>& controllers,
      const VrVector3&            target,
            VrHand&               hand);

    /**
     * \brief The hand chosen last
     */
    VrHand hand() const {
      return m_hand;
    }

  private:

    VrHand m_hand = VrHand::Right;

  };

}
