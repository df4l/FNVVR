#include <algorithm>
#include <cmath>
#include <utility>

#include "vr_math.h"
#include "vr_pipboy_touch.h"

namespace dxvk {

  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    float gap(const VrVector2& a, const VrVector2& b) {
      return std::hypot(a.x - b.x, a.y - b.y);
    }

    bool normalized(const VrVector3& v, VrVector3& unit) {
      float length = vrLength(v);

      if (!(length > 1e-12f))
        return false;

      unit = v * (1.0f / length);
      return true;
    }

    /**
     * \brief Solves m * x = b, n unknowns, by Gaussian elimination
     * \returns \c false if m is singular
     */
    template<uint32_t N>
    bool solve(float (&m)[N][N], float (&b)[N], float (&x)[N]) {
      for (uint32_t c = 0; c < N; c++) {
        uint32_t pivot = c;

        for (uint32_t r = c + 1; r < N; r++) {
          if (std::abs(m[r][c]) > std::abs(m[pivot][c]))
            pivot = r;
        }

        if (!(std::abs(m[pivot][c]) > 1e-12f))
          return false;

        std::swap(m[c], m[pivot]);
        std::swap(b[c], b[pivot]);

        for (uint32_t r = c + 1; r < N; r++) {
          float k = m[r][c] / m[c][c];

          for (uint32_t j = c; j < N; j++)
            m[r][j] -= k * m[c][j];

          b[r] -= k * b[c];
        }
      }

      for (uint32_t c = N; c-- > 0; ) {
        float sum = b[c];

        for (uint32_t j = c + 1; j < N; j++)
          sum -= m[c][j] * x[j];

        x[c] = sum / m[c][c];
      }

      return true;
    }

    void curveTerms(float du, float dv, float (&terms)[6]) {
      terms[0] = 1.0f;
      terms[1] = du;
      terms[2] = dv;
      terms[3] = du * du;
      terms[4] = du * dv;
      terms[5] = dv * dv;
    }

    float height(const VrTouchScreen& screen, float du, float dv) {
      float terms[6];
      curveTerms(du, dv, terms);

      float h = 0.0f;

      for (uint32_t i = 0; i < 6; i++)
        h += screen.curve[i] * terms[i];

      return h;
    }

  }


  VrTouchScreen vrFitTouchScreen(
    const VrVector3*            positions,
    const VrVector2*            uvs,
          uint32_t              count) {
    VrTouchScreen screen;

    if (!positions || !uvs || count < 3)
      return screen;

    VrVector3 centre;
    VrVector2 uv0 = { 0.0f, 0.0f };
    screen.uvMin = uvs[0];
    screen.uvMax = uvs[0];

    for (uint32_t i = 0; i < count; i++) {
      centre = centre + positions[i];
      uv0.x += uvs[i].x;
      uv0.y += uvs[i].y;
      screen.uvMin.x = std::min(screen.uvMin.x, uvs[i].x);
      screen.uvMin.y = std::min(screen.uvMin.y, uvs[i].y);
      screen.uvMax.x = std::max(screen.uvMax.x, uvs[i].x);
      screen.uvMax.y = std::max(screen.uvMax.y, uvs[i].y);
    }

    screen.centre = centre * (1.0f / float(count));
    screen.uv0    = { uv0.x / float(count), uv0.y / float(count) };

    // Least squares of point = centre + alongU du + alongV dv, with (du, dv)
    // the texture coordinates from their mean
    float m[2][2] = { };
    VrVector3 bu, bv;

    for (uint32_t i = 0; i < count; i++) {
      float du = uvs[i].x - screen.uv0.x;
      float dv = uvs[i].y - screen.uv0.y;
      VrVector3 offset = positions[i] - screen.centre;

      m[0][0] += du * du;
      m[0][1] += du * dv;
      m[1][1] += dv * dv;
      bu = bu + offset * du;
      bv = bv + offset * dv;
    }

    float det = m[0][0] * m[1][1] - m[0][1] * m[0][1];

    if (!(std::abs(det) > 1e-20f))
      return screen;

    screen.alongU = (bu * m[1][1] - bv * m[0][1]) * (1.0f / det);
    screen.alongV = (bv * m[0][0] - bu * m[0][1]) * (1.0f / det);

    // Texture rows go down: seen from the front, u grows to the right and v
    // downwards
    if (!normalized(cross(screen.alongV, screen.alongU), screen.normal)
     || !normalized(screen.alongU, screen.uAxis)
     || !normalized(screen.alongV, screen.vAxis))
      return screen;

    // The dual basis of alongU and alongV in the plane
    float uu = vrDot(screen.alongU, screen.alongU);
    float uv = vrDot(screen.alongU, screen.alongV);
    float vv = vrDot(screen.alongV, screen.alongV);
    float gram = uu * vv - uv * uv;

    if (!(gram > 1e-20f))
      return screen;

    screen.gradU = (screen.alongU * vv - screen.alongV * uv) * (1.0f / gram);
    screen.gradV = (screen.alongV * uu - screen.alongU * uv) * (1.0f / gram);

    // The glass's bulge above the plane, flat if the mesh has too few
    // vertices to tell
    float cm[6][6] = { };
    float cb[6] = { };

    for (uint32_t i = 0; i < count; i++) {
      float h = vrDot(positions[i] - screen.centre, screen.normal);
      screen.bulge = std::max(screen.bulge, std::abs(h));

      float terms[6];
      curveTerms(uvs[i].x - screen.uv0.x, uvs[i].y - screen.uv0.y, terms);

      for (uint32_t r = 0; r < 6; r++) {
        for (uint32_t c = 0; c < 6; c++)
          cm[r][c] += terms[r] * terms[c];

        cb[r] += terms[r] * h;
      }
    }

    float curve[6] = { };

    if (count >= 6 && solve(cm, cb, curve)) {
      for (uint32_t i = 0; i < 6; i++)
        screen.curve[i] = std::isfinite(curve[i]) ? curve[i] : 0.0f;
    }

    screen.valid = true;
    return screen;
  }


  VrTouchScreen vrFlipTouchScreen(const VrTouchScreen& screen) {
    VrTouchScreen flipped = screen;
    flipped.normal = screen.normal * -1.0f;

    for (uint32_t i = 0; i < 6; i++)
      flipped.curve[i] = -screen.curve[i];

    return flipped;
  }


  VrTouchScreenPoint vrPointOnScreen(const VrTouchScreen& screen, const VrVector3& point) {
    VrTouchScreenPoint result;

    if (!screen.valid)
      return result;

    VrVector3 offset = point - screen.centre;
    float du = vrDot(screen.gradU, offset);
    float dv = vrDot(screen.gradV, offset);
    float h  = height(screen, du, dv);

    VrVector3 across = screen.alongU * du + screen.alongV * dv;

    result.distance = vrDot(offset, screen.normal) - h;
    result.surface  = screen.centre + across + screen.normal * h;
    result.uv       = { screen.uv0.x + du, screen.uv0.y + dv };
    result.plane    = { vrDot(across, screen.uAxis), vrDot(across, screen.vAxis) };

    result.inside = result.uv.x >= screen.uvMin.x && result.uv.x <= screen.uvMax.x
                 && result.uv.y >= screen.uvMin.y && result.uv.y <= screen.uvMax.y;
    return result;
  }


  VrPipBoyTouchState VrPipBoyTouch::update(const VrPipBoyTouchInput& input) {
    VrPipBoyTouchState state;

    if (!input.point) {
      reset();
      return state;
    }

    const VrPipBoyTouchPoint& point = *input.point;
    m_sinceTap = std::min(m_sinceTap + input.dt, DoubleTapTime + 1.0f);

    // The gamepad takes the menus back, and drops a touch on the way
    if (input.padUsed)
      reset();

    // The screen first: a button is only looked for away from it. A touch
    // is kept however deep the finger goes, the drawn hand stays on the glass
    bool touchingScreen = m_touching && m_held.kind == VrPipBoyAimKind::Screen;
    bool onScreen = point.inside && (touchingScreen || point.distance >= -MaxDepth);
    bool onButton = !onScreen && point.button >= 0;
    float distance = onScreen ? point.distance : point.buttonDistance;
    bool near = (onScreen || onButton) && distance <= HoverDistance;

    if (m_click) {
      // The frame after a tap releases the button where it touched
      state.aim = m_held;
      m_click   = false;
    } else if (m_touching || (near && m_hovering && distance <= TouchDistance && distance >= -MaxDepth)) {
      updateTouch(point, onScreen, distance, input.dt, state);
    } else if (near) {
      state.aim.kind = onScreen ? VrPipBoyAimKind::Screen : VrPipBoyAimKind::Button;
      state.aim.surface = point.surface;
      state.aim.button  = onScreen ? -1 : point.button;

      if (onScreen)
        m_cursor = point.uv;
    }

    // A touch only starts from a finger in front of the screen, not one
    // coming from behind it or from the side
    m_hovering = near && distance > TouchDistance;
    m_owned    = m_owned || m_hovering || m_touching;

    state.cursorActive = m_owned;
    state.cursor       = m_cursor;
    return state;
  }


  void VrPipBoyTouch::updateTouch(
    const VrPipBoyTouchPoint&   point,
          bool                  onScreen,
          float                 distance,
          float                 dt,
          VrPipBoyTouchState&   state) {
    if (!m_touching) {
      m_touching     = true;
      m_scrolled     = false;
      m_longPressed  = false;
      m_slid         = false;
      m_touchStart   = point.plane;
      m_touchTime    = 0.0f;
      m_scrollAnchor = point.plane.y;

      m_held.kind    = onScreen ? VrPipBoyAimKind::Screen : VrPipBoyAimKind::Button;
      m_held.surface = point.surface;
      m_held.button  = onScreen ? -1 : point.button;

      // A second tap on a map zooms it as it slides
      m_dragsMap = onScreen && point.map;
      m_zooms    = m_dragsMap && m_sinceTap <= DoubleTapTime
                && gap(point.plane, m_tapPoint) <= DoubleTapReach;

      if (onScreen)
        m_cursor = point.uv;

      state.haptic = TouchHaptic;
    } else {
      m_touchTime += dt;
    }

    bool onHeld = m_held.kind == VrPipBoyAimKind::Screen ? onScreen : !onScreen && point.button == m_held.button;

    // Lifted: a tap clicks where it touched, a slide or a long press does not
    if (!onHeld || distance > ReleaseDistance) {
      m_touching = false;

      if (!m_scrolled && !m_longPressed) {
        m_click      = true;
        state.button = true;
        m_sinceTap   = 0.0f;
        m_tapPoint   = m_touchStart;
      }

      state.aim = m_held;
      return;
    }

    state.aim = m_held;

    if (m_held.kind != VrPipBoyAimKind::Screen || m_longPressed)
      return;

    // A map is dragged any way, a list only scrolls up and down
    float moved = m_dragsMap ? gap(point.plane, m_touchStart) : std::abs(point.plane.y - m_touchStart.y);

    m_slid = m_slid || gap(point.plane, m_touchStart) > LongPressSlop;

    if (!m_scrolled && moved > ScrollStart)
      m_scrolled = true;

    if (m_scrolled) {
      slide(point, state);
    } else if (!m_slid && m_touchTime >= LongPressTime) {
      // Held still: the right mouse button, pressed on this frame and
      // released on the next
      m_longPressed     = true;
      state.rightButton = true;
      state.haptic      = LongPressHaptic;
    }
  }


  void VrPipBoyTouch::slide(
    const VrPipBoyTouchPoint&   point,
          VrPipBoyTouchState&   state) {
    if (m_dragsMap && !m_zooms) {
      // The game's pick follows the finger, the map follows the pick
      m_held.surface = point.surface;
      state.aim      = m_held;
      state.dragsMap = true;
      return;
    }

    // A finger sliding down drags a list down, as on a phone: the lines
    // above come into view, which the game calls scrolling up. On a map it
    // zooms in.
    float step = m_zooms ? ZoomStep : ScrollStep;
    int32_t notches = 0;

    while (point.plane.y - m_scrollAnchor >= step) {
      notches++;
      m_scrollAnchor += step;
    }

    while (point.plane.y - m_scrollAnchor <= -step) {
      notches--;
      m_scrollAnchor -= step;
    }

    if (m_zooms)
      state.zoom  += notches;
    else
      state.wheel += notches * WheelNotch;

    if (notches)
      state.haptic = std::max(state.haptic, NotchHaptic);
  }


  void VrPipBoyTouch::reset() {
    m_owned    = false;
    m_hovering = false;
    m_touching = false;
    m_scrolled = false;
    m_click    = false;
    m_longPressed = false;
    m_dragsMap = false;
    m_zooms    = false;
    m_held     = VrPipBoyAim();
  }

}
