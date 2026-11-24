#pragma once

#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_backend.h"
#include "vr_console_screen.h"
#include "vr_menu_pointer.h"
#include "vr_paint_panel.h"
#include "vr_terminal_image.h"
#include "vr_terminal_texture.h"

namespace dxvk {

  /**
   * \brief Shows the game's console on a terminal, with a keyboard
   *
   * While the game's console is open, the game's own terminal monitor
   * stands in front of the player at eye height, as when a terminal of
   * the world is used, with the console's output and command line on its
   * screen (VrConsoleScreen). The keyboard of the world's terminals
   * unfolds below it, its keys named in the terminal font, and the lasers
   * type on it. The keys go to the game's console through VrGameConsole,
   * so commands run as typed ones do. The right stick scrolls the output,
   * the back button and the EXIT and ESC keys close the console; the
   * keyboard then folds before the terminal disappears.
   *
   * Where there is no stereo view (the main menu), or when the game's
   * terminal or the keyboard's model cannot be used, the keyboard is a
   * flat image on its own panel (VrPanelId::Terminal) over the bottom of
   * the monitor, and without the terminal the console is seen where the
   * game draws it.
   *
   * Only used on the game's main thread.
   */
  class VrConsoleTerminal {

  public:

    /// Distance from the eyes to the terminal's screen, in metres
    constexpr static float ScreenDistance   = 0.7f;
    /// Width of the game's view of the terminal, in metres
    constexpr static float ScreenWidth      = 0.62f;
    /// Distance from the eyes to the keyboard, in metres
    constexpr static float KeyboardDistance = 0.5f;
    /// Width of the keyboard, in metres
    constexpr static float KeyboardWidth    = 0.55f;
    /// How far below the eyes the keyboard's centre is, in metres
    constexpr static float KeyboardDrop     = 0.3f;
    /// The text cursor is lit and off for this long each, in seconds, as
    /// on the game's terminals
    constexpr static float BlinkTime        = 0.4f;
    /// How long a key is drawn pressed, in seconds
    constexpr static float PressTime        = 0.12f;

    VrConsoleTerminal(VrPainter& painter, IVRBackend& backend, const VrTexts& texts);

    /**
     * \brief Where the keyboard is, for the lasers
     *
     * While the 3D keyboard moves, the placement has no size: the lasers
     * hit nothing, and the controls still belong to the console.
     *
     * \returns The keyboard's placement, or \c nullptr while the console
     *    is closed
     */
    const VrPanelPlacement* placement() const {
      return m_active ? &m_placement : nullptr;
    }

    /**
     * \brief Whether the terminal's monitor is in the eye images
     *
     * The menu panel, which the console is opened from, is then hidden:
     * the runtime would draw it over the monitor.
     */
    bool showsScreen() const {
      return m_screen && m_screen->isShown();
    }

    /**
     * \brief Shows the terminal while the console is open
     *
     * Called once per frame, after the controls pointed the lasers at
     * the keyboard's placement.
     *
     * \param [in] head Head pose in tracking space, which places the
     *    terminal when the console opens
     * \param [in] stereo Whether the game draws the eye images this frame
     * \param [in] pointer What the lasers did on the keyboard
     * \param [in] back Whether the menu's back button was pressed
     * \param [in] dt Duration of a frame in seconds
     */
    void update(const VrPose& head, bool stereo, const VrPointerState& pointer, bool back, float dt);

    /**
     * \brief Draws the monitor into the eye image being drawn
     *
     * \param [in] eye The eye's pose in tracking space and field of view
     */
    void drawEye(const VrEyeView& eye);

  private:

    VrPainter&             m_painter;
    IVRBackend&            m_backend;
    const VrTexts&         m_texts;

    std::unique_ptr<VrConsoleScreen> m_screen;

    // The flat keyboard's panel
    VrPaintPanel           m_flatPanel;
    VrPaintList            m_flatList;

    // The 3D keyboard's copy of the terminal texture
    Com<IDirect3DTexture9> m_modelTexture;
    VrPaintList            m_modelList;

    VrTerminalImage        m_image;
    VrTerminalTexture      m_modelImage;
    VrTerminalKeyboardView m_view;
    // What the keyboard was last drawn with
    VrTerminalKeyboardView m_drawnView;
    VrPanelPlacement       m_placement;
    VrPanelPlacement       m_flatPlacement;
    VrPose                 m_screenAnchor;

    bool  m_loaded       = false;
    bool  m_failed       = false;
    bool  m_modelFailed  = false;
    bool  m_active       = false;
    bool  m_viewDrawn    = false;
    bool  m_modelDrawn   = false;
    bool  m_button       = false;
    float m_blinkTime    = 0.0f;
    float m_pressTime    = 0.0f;

    bool load();

    void place(const VrPose& head);

    void press(int32_t index, const VrPointerState& pointer);

    void updateScreen(bool stereo, float dt);

    void close(bool stereo, float dt);

    bool showFlatKeyboard(bool changed);

    void hideFlatKeyboard();

    bool showModelKeyboard(bool changed);

    bool createModelTexture();

  };

}
