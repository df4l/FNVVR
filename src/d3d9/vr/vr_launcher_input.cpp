#include <algorithm>
#include <cmath>

#include "vr_launcher_input.h"

namespace dxvk {

  VrScreenRect vrLauncherCanvas(const VrScreenRect& mainWindow) {
    int32_t centreX = mainWindow.left + (mainWindow.right  - mainWindow.left) / 2;
    int32_t centreY = mainWindow.top  + (mainWindow.bottom - mainWindow.top)  / 2;

    VrScreenRect canvas;
    canvas.left   = centreX - VrLauncherCanvasWidth  / 2;
    canvas.top    = centreY - VrLauncherCanvasHeight / 2;
    canvas.right  = canvas.left + VrLauncherCanvasWidth;
    canvas.bottom = canvas.top  + VrLauncherCanvasHeight;
    return canvas;
  }


  VrScreenPoint vrLauncherPoint(const VrScreenRect& canvas, const VrVector2& uv) {
    int32_t width  = canvas.right  - canvas.left;
    int32_t height = canvas.bottom - canvas.top;

    VrScreenPoint point;
    point.x = canvas.left + std::clamp(int32_t(std::floor(uv.x * float(width))),  0, width  - 1);
    point.y = canvas.top  + std::clamp(int32_t(std::floor(uv.y * float(height))), 0, height - 1);
    return point;
  }


  VrPanelPlacement vrLauncherPlacement(const VrPose& pose, float menuPanelWidth) {
    float metresPerPixel = menuPanelWidth / VrLauncherPixelsPerPanel;

    VrPanelPlacement placement;
    placement.pose   = pose;
    placement.width  = float(VrLauncherCanvasWidth)  * metresPerPixel;
    placement.height = float(VrLauncherCanvasHeight) * metresPerPixel;
    return placement;
  }


  std::vector<VrLauncherMouseEvent> VrLauncherMouse::update(const VrPointerState& pointer, const VrScreenRect& canvas) {
    using Kind = VrLauncherMouseEvent::Kind;

    std::vector<VrLauncherMouseEvent> events;

    auto add = [&] (Kind kind, bool held, int32_t wheel = 0) {
      VrLauncherMouseEvent event;
      event.kind  = kind;
      event.point = m_point;
      event.wheel = wheel;
      event.held  = held;
      events.push_back(event);
    };

    if (!pointer.cursorActive) {
      if (m_held)
        add(Kind::ButtonUp, false);

      if (m_active)
        add(Kind::Leave, false);

      m_active = false;
      m_held   = false;
      return events;
    }

    VrScreenPoint point = vrLauncherPoint(canvas, pointer.cursor);
    bool moved = !m_active || point.x != m_point.x || point.y != m_point.y;

    m_active = true;
    m_point  = point;

    if (moved)
      add(Kind::Move, m_held);

    if (pointer.button != m_held) {
      m_held = pointer.button;
      add(m_held ? Kind::ButtonDown : Kind::ButtonUp, m_held);
    }

    if (pointer.wheel)
      add(Kind::Wheel, m_held, pointer.wheel);

    return events;
  }

}
