#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The Pip-Boy's screen as a flat surface, fitted to its mesh
   *
   * All in the mesh's own space. The screen bulges a little: the plane
   * goes through its vertices' centre, and the texture coordinates are an
   * affine map of the point on that plane.
   */
  struct VrTouchScreen {
    bool      valid = false;
    VrVector3 centre;
    /// Unit normal, on the side the triangles face
    VrVector3 normal;
    /// Unit directions of growing u and v, in the plane
    VrVector3 uAxis;
    VrVector3 vAxis;
    /// Texture coordinates at the centre, and how fast they grow per unit
    /// along the plane: uv = uv0 + (gradU, gradV) . (point - centre)
    VrVector2 uv0;
    VrVector3 gradU;
    VrVector3 gradV;
    /// Range of the mesh's texture coordinates
    VrVector2 uvMin;
    VrVector2 uvMax;
  };

  /**
   * \brief Fits a flat surface to a screen's mesh
   *
   * \param [in] positions Vertex positions
   * \param [in] uvs Texture coordinates of the vertices
   * \param [in] count Number of vertices
   * \param [in] triangles Three vertex indices per triangle
   * \param [in] triangleCount Number of triangles
   * \returns The surface, not valid if the mesh has no area or its
   *    texture coordinates do not vary across it
   */
  VrTouchScreen vrFitTouchScreen(
    const VrVector3*            positions,
    const VrVector2*            uvs,
          uint32_t              count,
    const uint16_t*             triangles,
          uint32_t              triangleCount);

  /**
   * \brief Turns the surface's front to the other side
   */
  VrTouchScreen vrFlipTouchScreen(const VrTouchScreen& screen);

  /**
   * \brief Where a point is relative to a screen
   */
  struct VrTouchScreenPoint {
    /// Distance in front of the plane, negative behind it, in mesh units
    float     distance = 0.0f;
    /// Texture coordinates of the point's projection on the plane
    VrVector2 uv;
    /// Whether the projection is within the mesh's texture coordinates
    bool      inside = false;
    /// Position along uAxis and vAxis from the centre, in mesh units
    VrVector2 plane;
    /// The projection on the plane
    VrVector3 surface;
  };

  /**
   * \brief Projects a point on a screen
   */
  VrTouchScreenPoint vrPointOnScreen(const VrTouchScreen& screen, const VrVector3& point);

  /**
   * \brief Where the right index fingertip is relative to the Pip-Boy,
   *    measured on the drawn model
   */
  struct VrPipBoyTouchPoint {
    /// Distance in front of the screen, negative behind it, in metres
    float     distance = 0.0f;
    /// Texture point under the fingertip, see VrTouchScreenPoint
    VrVector2 uv;
    bool      inside = false;
    /// Position on the screen along u and v, in metres
    VrVector2 plane;
    /// The fingertip's projection on the screen, in the mesh's space
    VrVector3 surface;
    /// Tab button (0 Stats, 1 Items, 2 Data) nearest the fingertip, within
    /// reach of it, or -1. Only looked for away from the screen.
    int32_t   button = -1;
    /// Distance from the fingertip to that button's surface, in metres
    float     buttonDistance = 0.0f;
  };

  /**
   * \brief What the game's pick of the Pip-Boy looks along
   */
  enum class VrPipBoyAimKind : uint32_t {
    /// Nothing: the finger is away
    None,
    /// A point of the screen, in the mesh's space
    Screen,
    /// A tab button
    Button,
  };

  struct VrPipBoyAim {
    VrPipBoyAimKind kind    = VrPipBoyAimKind::None;
    VrVector3       surface;
    int32_t         button  = -1;
  };

  /**
   * \brief Input of the Pip-Boy's touch for one frame
   */
  struct VrPipBoyTouchInput {
    /// The fingertip measured on the open Pip-Boy, or \c nullptr
    const VrPipBoyTouchPoint* point = nullptr;
    /// Whether a gamepad menu button or the navigation stick was used
    bool  padUsed = false;
    float dt      = 0.0f;
  };

  /**
   * \brief What the touch does in a frame
   */
  struct VrPipBoyTouchState {
    /// The game's cursor follows the finger, the interface is in mouse mode
    bool        cursorActive = false;
    /// Cursor position on the interface, see VrPanelHit::uv
    VrVector2   cursor;
    /// Left mouse button
    bool        button = false;
    /// Mouse wheel movement, 120 per notch, positive scrolls up
    int32_t     wheel  = 0;
    /// Where the game's pick looks
    VrPipBoyAim aim;
    /// Vibration of the right controller to start now, 0 for none
    float       haptic = 0.0f;
  };

  /**
   * \brief Turns the right index finger into a mouse on the Pip-Boy's screen
   *
   * The finger hovers the screen within \ref HoverDistance: the game's
   * cursor follows it and the line under it is highlighted. It touches the
   * screen once it comes within \ref TouchDistance from the front, and a
   * tap, a touch lifted without sliding, clicks where it touched, pressing
   * and releasing the mouse button on two frames. Sliding along the screen
   * while touching scrolls, one wheel notch per \ref ScrollStep, as on a
   * phone. The tab buttons beside the screen are tapped the same way.
   *
   * The finger keeps the cursor until a gamepad menu button or the
   * navigation stick is used, so that the gamepad's highlight does not come
   * back between two taps.
   */
  class VrPipBoyTouch {

  public:

    /// The finger hovers the screen from this close, in metres
    constexpr static float HoverDistance   = 0.08f;
    /// The finger touches the screen from this close, in metres
    constexpr static float TouchDistance   = 0.006f;
    /// A touch ends once the finger is this far again, in metres
    constexpr static float ReleaseDistance = 0.015f;
    /// A finger deeper behind the screen is not on it, in metres
    constexpr static float MaxDepth        = 0.06f;
    /// Slide along the screen that turns a touch into a scroll, in metres
    constexpr static float ScrollStart     = 0.012f;
    /// Slide along the screen of one wheel notch, in metres
    constexpr static float ScrollStep      = 0.008f;
    /// Mouse wheel movement of one notch
    constexpr static int32_t WheelNotch    = 120;
    /// Vibration when the finger touches, and at each notch
    constexpr static float TouchHaptic     = 0.35f;
    constexpr static float NotchHaptic     = 0.15f;

    VrPipBoyTouchState update(const VrPipBoyTouchInput& input);

  private:

    bool        m_owned     = false;
    bool        m_hovering  = false;
    bool        m_touching  = false;
    bool        m_scrolled  = false;
    /// Whether the button pressed by a tap is released this frame
    bool        m_click     = false;
    VrVector2   m_cursor;
    VrPipBoyAim m_held;
    float       m_touchStart   = 0.0f;
    float       m_scrollAnchor = 0.0f;

    void reset();

    void updateTouch(const VrPipBoyTouchPoint& point, bool onScreen, float distance, VrPipBoyTouchState& state);

  };

}
