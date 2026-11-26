#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The Pip-Boy's screen as a surface, fitted to its mesh
   *
   * All in the mesh's own space. The mesh's points are an affine map of
   * their texture coordinates, which gives the plane, plus a quadratic
   * height above that plane for the glass's bulge. Only the vertices and
   * their texture coordinates are used, whatever the mesh's triangles.
   */
  struct VrTouchScreen {
    bool      valid = false;
    /// The vertices' centre, at texture coordinates uv0
    VrVector3 centre;
    VrVector2 uv0;
    /// Unit normal of the plane, on the side the texture reads right from
    VrVector3 normal;
    /// Movement along the plane per unit of u and of v
    VrVector3 alongU;
    VrVector3 alongV;
    /// Unit directions of alongU and alongV
    VrVector3 uAxis;
    VrVector3 vAxis;
    /// How fast u and v grow along the plane:
    /// uv = uv0 + (gradU, gradV) . (point - centre)
    VrVector3 gradU;
    VrVector3 gradV;
    /// Height above the plane at (du, dv) from uv0: curve[0] + curve[1] du
    /// + curve[2] dv + curve[3] du² + curve[4] du dv + curve[5] dv²
    float     curve[6] = { };
    /// Largest distance of a vertex from the plane, in mesh units
    float     bulge = 0.0f;
    /// Range of the mesh's texture coordinates
    VrVector2 uvMin;
    VrVector2 uvMax;
  };

  /**
   * \brief Fits a surface to a screen's mesh
   *
   * \param [in] positions Vertex positions
   * \param [in] uvs Texture coordinates of the vertices
   * \param [in] count Number of vertices
   * \returns The surface, not valid if the mesh has no area or its
   *    texture coordinates do not vary across it
   */
  VrTouchScreen vrFitTouchScreen(
    const VrVector3*            positions,
    const VrVector2*            uvs,
          uint32_t              count);

  /**
   * \brief Turns the surface's front to the other side
   */
  VrTouchScreen vrFlipTouchScreen(const VrTouchScreen& screen);

  /**
   * \brief Where a point is relative to a screen
   */
  struct VrTouchScreenPoint {
    /// Distance in front of the surface, negative behind it, in mesh units
    float     distance = 0.0f;
    /// Texture coordinates of the point's projection on the surface
    VrVector2 uv;
    /// Whether the projection is within the mesh's texture coordinates
    bool      inside = false;
    /// Position along uAxis and vAxis from the centre, in mesh units
    VrVector2 plane;
    /// The projection on the surface
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
    /// Whether the screen shows a map, which a slide drags
    bool      map = false;
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
    /// Right mouse button
    bool        rightButton = false;
    /// Mouse wheel movement, 120 per notch, positive scrolls up
    int32_t     wheel  = 0;
    /// Whether the finger drags the map: the aim follows it
    bool        dragsMap = false;
    /// Zoom steps of the map, positive zooms in
    int32_t     zoom   = 0;
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
   * A touch held still for \ref LongPressTime is the right mouse button,
   * as on a phone's long press: the PC game drops the item under it and
   * places a marker on the map. On a map, a slide drags the map instead of
   * scrolling, and a slide that starts with a second tap, within
   * \ref DoubleTapTime of the first, zooms it: down zooms in, up zooms out.
   *
   * The finger keeps the cursor until a gamepad menu button or the
   * navigation stick is used, so that the gamepad's highlight does not come
   * back between two taps.
   */
  class VrPipBoyTouch {

  public:

    /// The finger hovers the screen from this close, in metres
    constexpr static float HoverDistance   = 0.08f;
    /// The finger touches the screen from this close, in metres: the
    /// fingertip is measured inside the finger, about this far from its pad
    constexpr static float TouchDistance   = 0.008f;
    /// A touch ends once the finger is this far again, in metres
    constexpr static float ReleaseDistance = 0.015f;
    /// A finger deeper behind the screen is not hovering it, in metres. A
    /// touch is kept however deep the finger goes, the drawn hand stays on
    /// the glass.
    constexpr static float MaxDepth        = 0.06f;
    /// Slide along the screen that turns a touch into a scroll, in metres
    constexpr static float ScrollStart     = 0.012f;
    /// Slide along the screen of one wheel notch, in metres
    constexpr static float ScrollStep      = 0.008f;
    /// Mouse wheel movement of one notch
    constexpr static int32_t WheelNotch    = 120;
    /// A touch held still this long is a long press, in seconds
    constexpr static float LongPressTime   = 0.6f;
    /// ... if it never slid further than this, in metres
    constexpr static float LongPressSlop   = 0.005f;
    /// A touch this soon after a tap is a double tap, in seconds
    constexpr static float DoubleTapTime   = 0.35f;
    /// ... if it is this close to the tap, in metres
    constexpr static float DoubleTapReach  = 0.025f;
    /// Slide along the screen of one zoom step, in metres
    constexpr static float ZoomStep        = 0.01f;
    /// Vibration when the finger touches, at each notch and on a long press
    constexpr static float TouchHaptic     = 0.35f;
    constexpr static float NotchHaptic     = 0.15f;
    constexpr static float LongPressHaptic = 0.6f;

    VrPipBoyTouchState update(const VrPipBoyTouchInput& input);

  private:

    bool        m_owned     = false;
    bool        m_hovering  = false;
    bool        m_touching  = false;
    bool        m_scrolled  = false;
    /// Whether the button pressed by a tap is released this frame
    bool        m_click     = false;
    /// Whether the touch was a long press, which a lift does not click,
    /// and whether it slid too far to be one
    bool        m_longPressed = false;
    bool        m_slid        = false;
    /// Whether the touch drags a map, and whether it zooms it
    bool        m_dragsMap  = false;
    bool        m_zooms     = false;
    VrVector2   m_cursor;
    VrPipBoyAim m_held;
    VrVector2   m_touchStart;
    float       m_touchTime    = 0.0f;
    float       m_scrollAnchor = 0.0f;
    /// Time since the last tap, and where it touched
    float       m_sinceTap     = DoubleTapTime + 1.0f;
    VrVector2   m_tapPoint;

    void reset();

    void updateTouch(const VrPipBoyTouchPoint& point, bool onScreen, float distance, float dt, VrPipBoyTouchState& state);

    void slide(const VrPipBoyTouchPoint& point, VrPipBoyTouchState& state);

  };

}
