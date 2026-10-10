#pragma once

#include <array>
#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Where a flat panel is, for pointing at it
   *
   * The panel is a rectangle centred on \c pose, facing along its +Z axis,
   * in the same space as the controller poses.
   */
  struct VrPanelPlacement {
    VrPose pose;
    float  width  = 0.0f;
    float  height = 0.0f;
  };

  /**
   * \brief Where a ray meets a panel
   */
  struct VrPanelHit {
    bool      hit      = false;
    /// Point on the panel, (0, 0) top left and (1, 1) bottom right
    VrVector2 uv;
    /// Distance from the ray's origin, in metres
    float     distance = 0.0f;
  };

  /**
   * \brief Intersects a ray with the front of a panel
   *
   * \param [in] ray Origin and orientation of the ray, which points along -Z
   * \param [in] panel Panel to hit
   */
  VrPanelHit vrIntersectPanel(const VrPose& ray, const VrPanelPlacement& panel);

  /**
   * \brief Laser of one hand, as it is drawn
   */
  struct VrPointerBeam {
    bool  visible = false;
    /// Length from the controller to the panel, in metres
    float length  = 0.0f;
    /// Whether this laser drives the cursor
    bool  active  = false;
  };

  /**
   * \brief Input of the menu pointer for one frame
   */
  struct VrPointerInput {
    /// Panel the lasers point at, \c nullptr if none can be used
    const VrPanelPlacement*                     panel = nullptr;
    std::array<VrControllerState, VrHandCount>  controllers;
    /// Trigger of each hand
    std::array<bool, VrHandCount>               triggers = { };
    /// Whether a gamepad menu button or the navigation stick was used
    bool                                        padUsed = false;
    /// Wheel stick, up is positive, in [-1, 1]
    float                                       wheel   = 0.0f;
    /// Time since the previous frame, in seconds
    float                                       dt      = 0.0f;
  };

  /**
   * \brief What the menu pointer does in a frame
   */
  struct VrPointerState {
    /// The game's cursor follows the laser, the interface is in mouse mode
    bool      cursorActive = false;
    /// Cursor position on the panel, see VrPanelHit::uv
    VrVector2 cursor;
    /// Left mouse button
    bool      button       = false;
    /// Mouse wheel movement, 120 per notch, positive scrolls up
    int32_t   wheel        = 0;
    std::array<VrPointerBeam, VrHandCount> beams;
    /// Whether the triggers belong to the pointer this frame, and must
    /// not reach the game as gamepad triggers
    bool      triggersTaken = false;
  };

  /**
   * \brief Turns the controllers into a mouse for the menu panel
   *
   * Each hand has a laser, shown while it points at the panel. The hand
   * whose trigger was pressed last drives the cursor; when it does not
   * point at the panel and the other one does, the other one drives it.
   * The trigger is the left mouse button, the wheel stick scrolls.
   *
   * The last input wins: using a gamepad menu button or the navigation
   * stick hands the menus back to the gamepad, until the laser moves by
   * \ref MoveThreshold or the trigger is pressed. The cursor is also off
   * while no laser points at the panel.
   */
  class VrMenuPointer {

  public:

    /// Laser movement that takes the menus back from the gamepad, in panel units
    constexpr static float MoveThreshold   = 0.03f;
    /// Wheel stick deflection that scrolls
    constexpr static float WheelThreshold  = 0.5f;
    /// Time between two wheel notches while the stick is held, in seconds
    constexpr static float WheelRepeatTime = 0.15f;
    /// Mouse wheel movement of one notch
    constexpr static int32_t WheelNotch    = 120;

    VrPointerState update(const VrPointerInput& input);

  private:

    VrHand    m_hand          = VrHand::Right;
    bool      m_cursorActive  = false;
    bool      m_hasAnchor     = false;
    VrVector2 m_anchor;
    std::array<bool, VrHandCount> m_triggers = { };
    float     m_wheelTimer    = 0.0f;

    int32_t updateWheel(float stick, float dt);

  };

}
