#include <algorithm>
#include <cmath>

#include "vr_math.h"
#include "vr_pipboy_touch.h"

namespace dxvk {

  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    bool normalized(const VrVector3& v, VrVector3& unit) {
      float length = vrLength(v);

      if (!(length > 1e-12f))
        return false;

      unit = v * (1.0f / length);
      return true;
    }

    float determinant(const float m[3][3]) {
      return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
           - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
           + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    /**
     * \brief Solves m * x = b by Cramer's rule
     * \returns \c false if m is singular
     */
    bool solve(const float m[3][3], const float b[3], float x[3]) {
      float d = determinant(m);

      if (!(std::abs(d) > 1e-20f))
        return false;

      for (uint32_t c = 0; c < 3; c++) {
        float column[3][3];

        for (uint32_t i = 0; i < 3; i++) {
          for (uint32_t j = 0; j < 3; j++)
            column[i][j] = j == c ? b[i] : m[i][j];
        }

        x[c] = determinant(column) / d;
      }

      return true;
    }

  }


  VrTouchScreen vrFitTouchScreen(
    const VrVector3*            positions,
    const VrVector2*            uvs,
          uint32_t              count,
    const uint16_t*             triangles,
          uint32_t              triangleCount) {
    VrTouchScreen screen;

    if (!positions || !uvs || !triangles || count < 3 || !triangleCount)
      return screen;

    // The triangles' area-weighted normal
    VrVector3 normal;

    for (uint32_t i = 0; i < triangleCount; i++) {
      uint16_t a = triangles[3 * i + 0];
      uint16_t b = triangles[3 * i + 1];
      uint16_t c = triangles[3 * i + 2];

      if (a >= count || b >= count || c >= count)
        return screen;

      normal = normal + cross(positions[b] - positions[a], positions[c] - positions[a]);
    }

    if (!normalized(normal, screen.normal))
      return screen;

    VrVector3 centre;
    screen.uvMin = uvs[0];
    screen.uvMax = uvs[0];

    for (uint32_t i = 0; i < count; i++) {
      centre = centre + positions[i];
      screen.uvMin.x = std::min(screen.uvMin.x, uvs[i].x);
      screen.uvMin.y = std::min(screen.uvMin.y, uvs[i].y);
      screen.uvMax.x = std::max(screen.uvMax.x, uvs[i].x);
      screen.uvMax.y = std::max(screen.uvMax.y, uvs[i].y);
    }

    screen.centre = centre * (1.0f / float(count));

    // Any two directions across the plane
    VrVector3 other = std::abs(screen.normal.x) < 0.9f ? VrVector3 { 1.0f, 0.0f, 0.0f } : VrVector3 { 0.0f, 1.0f, 0.0f };
    VrVector3 e1, e2;

    if (!normalized(cross(screen.normal, other), e1))
      return screen;

    e2 = cross(screen.normal, e1);

    // Least squares of uv = k1 * a + k2 * b + k0 over the vertices, with
    // (a, b) the vertex's position along e1 and e2 from the centre
    float m[3][3] = { };
    float bu[3] = { };
    float bv[3] = { };

    for (uint32_t i = 0; i < count; i++) {
      VrVector3 offset = positions[i] - screen.centre;
      float row[3] = { vrDot(offset, e1), vrDot(offset, e2), 1.0f };

      for (uint32_t r = 0; r < 3; r++) {
        for (uint32_t c = 0; c < 3; c++)
          m[r][c] += row[r] * row[c];

        bu[r] += row[r] * uvs[i].x;
        bv[r] += row[r] * uvs[i].y;
      }
    }

    float ku[3], kv[3];

    if (!solve(m, bu, ku) || !solve(m, bv, kv))
      return screen;

    screen.gradU = e1 * ku[0] + e2 * ku[1];
    screen.gradV = e1 * kv[0] + e2 * kv[1];
    screen.uv0   = { ku[2], kv[2] };

    screen.valid = normalized(screen.gradU, screen.uAxis)
                && normalized(screen.gradV, screen.vAxis);
    return screen;
  }


  VrTouchScreen vrFlipTouchScreen(const VrTouchScreen& screen) {
    VrTouchScreen flipped = screen;
    flipped.normal = screen.normal * -1.0f;
    return flipped;
  }


  VrScreenPoint vrPointOnScreen(const VrTouchScreen& screen, const VrVector3& point) {
    VrScreenPoint result;

    if (!screen.valid)
      return result;

    VrVector3 offset = point - screen.centre;
    result.distance  = vrDot(offset, screen.normal);
    result.surface   = point - screen.normal * result.distance;

    VrVector3 across = result.surface - screen.centre;
    result.uv.x  = screen.uv0.x + vrDot(screen.gradU, across);
    result.uv.y  = screen.uv0.y + vrDot(screen.gradV, across);
    result.plane = { vrDot(across, screen.uAxis), vrDot(across, screen.vAxis) };

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

    // The gamepad takes the menus back, and drops a touch on the way
    if (input.padUsed)
      reset();

    // The screen first: a button is only looked for away from it
    bool onScreen = point.inside && point.distance >= -MaxDepth;
    bool onButton = !onScreen && point.button >= 0;
    float distance = onScreen ? point.distance : point.buttonDistance;
    bool near = (onScreen || onButton) && distance <= HoverDistance;

    if (m_click) {
      // The frame after a tap releases the button where it touched
      state.aim = m_held;
      m_click   = false;
    } else if (m_touching || (near && m_hovering && distance <= TouchDistance && distance >= -MaxDepth)) {
      updateTouch(point, onScreen, distance, state);
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
          VrPipBoyTouchState&   state) {
    if (!m_touching) {
      m_touching     = true;
      m_scrolled     = false;
      m_touchStart   = point.plane.y;
      m_scrollAnchor = point.plane.y;

      m_held.kind    = onScreen ? VrPipBoyAimKind::Screen : VrPipBoyAimKind::Button;
      m_held.surface = point.surface;
      m_held.button  = onScreen ? -1 : point.button;

      if (onScreen)
        m_cursor = point.uv;

      state.haptic = TouchHaptic;
    }

    bool onHeld = m_held.kind == VrPipBoyAimKind::Screen ? onScreen : !onScreen && point.button == m_held.button;

    // Lifted: a tap clicks where it touched, a slide does not
    if (!onHeld || distance > ReleaseDistance || distance < -MaxDepth) {
      m_touching = false;

      if (!m_scrolled) {
        m_click      = true;
        state.button = true;
      }

      state.aim = m_held;
      return;
    }

    state.aim = m_held;

    if (m_held.kind != VrPipBoyAimKind::Screen)
      return;

    // A finger sliding down drags the list down, as on a phone: the lines
    // above come into view, which the game calls scrolling up
    if (!m_scrolled && std::abs(point.plane.y - m_touchStart) > ScrollStart)
      m_scrolled = true;

    if (!m_scrolled)
      return;

    while (point.plane.y - m_scrollAnchor >= ScrollStep) {
      state.wheel    += WheelNotch;
      m_scrollAnchor += ScrollStep;
    }

    while (point.plane.y - m_scrollAnchor <= -ScrollStep) {
      state.wheel    -= WheelNotch;
      m_scrollAnchor -= ScrollStep;
    }

    if (state.wheel)
      state.haptic = std::max(state.haptic, NotchHaptic);
  }


  void VrPipBoyTouch::reset() {
    m_owned    = false;
    m_hovering = false;
    m_touching = false;
    m_scrolled = false;
    m_click    = false;
    m_held     = VrPipBoyAim();
  }

}
