#pragma once

#include <cstdint>
#include <vector>

#include "vr_menu_pointer.h"

namespace dxvk {

  /**
   * \brief Point on the desktop, in pixels
   */
  struct VrScreenPoint {
    int32_t x = 0;
    int32_t y = 0;
  };

  /**
   * \brief Rectangle on the desktop, in pixels, right and bottom excluded
   */
  struct VrScreenRect {
    int32_t left   = 0;
    int32_t top    = 0;
    int32_t right  = 0;
    int32_t bottom = 0;
  };

  /// Size of the image shown in the headset, in pixels. It holds the
  /// launcher's window and the dialogs it opens around its centre.
  constexpr int32_t VrLauncherCanvasWidth  = 1280;
  constexpr int32_t VrLauncherCanvasHeight = 800;

  /// Pixels of the launcher shown over the width of a menu panel
  constexpr float VrLauncherPixelsPerPanel = 1024.0f;

  /**
   * \brief Part of the desktop that is shown, centred on the main window
   */
  VrScreenRect vrLauncherCanvas(const VrScreenRect& mainWindow);

  /**
   * \brief Point of the desktop under a point of the panel
   *
   * \param [in] canvas Part of the desktop that is shown
   * \param [in] uv Point on the panel, see VrPanelHit::uv
   */
  VrScreenPoint vrLauncherPoint(const VrScreenRect& canvas, const VrVector2& uv);

  /**
   * \brief Size and place of the panel, for the lasers
   *
   * \param [in] pose Centre of the panel
   * \param [in] menuPanelWidth Width of the game's menu panel, in metres
   */
  VrPanelPlacement vrLauncherPlacement(const VrPose& pose, float menuPanelWidth);

  /**
   * \brief What the lasers do to the launcher's windows
   */
  struct VrLauncherMouseEvent {
    enum class Kind {
      /// The cursor moved to \c point
      Move,
      /// The left button went down at \c point
      ButtonDown,
      /// The left button went up at \c point
      ButtonUp,
      /// The wheel turned by \c wheel at \c point, 120 per notch
      Wheel,
      /// The cursor left the panel, from \c point
      Leave,
    };

    Kind          kind  = Kind::Move;
    VrScreenPoint point;
    int32_t       wheel = 0;
    /// The left button is held during this event
    bool          held  = false;
  };

  /**
   * \brief Turns the laser pointer's state into mouse events
   *
   * A button held while the cursor leaves the panel is released where the
   * cursor was last, so that no control keeps the mouse.
   */
  class VrLauncherMouse {

  public:

    /**
     * \brief Events of one frame, in the order they happen
     *
     * \param [in] pointer State of the laser pointer
     * \param [in] canvas Part of the desktop that is shown
     */
    std::vector<VrLauncherMouseEvent> update(const VrPointerState& pointer, const VrScreenRect& canvas);

  private:

    bool          m_active = false;
    bool          m_held   = false;
    VrScreenPoint m_point;

  };

}
