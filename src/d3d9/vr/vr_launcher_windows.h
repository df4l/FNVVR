#pragma once

#include <windows.h>

#include <functional>
#include <vector>

#include "vr_launcher_input.h"

namespace dxvk {

  /**
   * \brief Runs work on the launcher's thread
   *
   * The launcher's loop sleeps 10 ms each time it has looked at its messages,
   * so a message sent from another thread takes that long to get an answer.
   * Sent one by one, the messages of a single picture of the main window took
   * 235 ms. A hook on the launcher's thread runs the work as one message, on
   * that thread, where the messages it sends are plain calls.
   */
  class VrLauncherThreadCall {

  public:

    /**
     * \brief Sets the hook
     *
     * Must be called on the thread that owns the launcher's windows. Without
     * the hook, \ref run does the work on the caller's thread: slow, but it
     * works.
     */
    static void install();

    /**
     * \param [in] thread Thread that owns the launcher's windows
     */
    explicit VrLauncherThreadCall(DWORD thread);

    /**
     * \brief Does the work and returns when it is done
     *
     * \param [in] work What to do
     * \returns \c false if the launcher was too busy, and nothing was done
     */
    bool run(const std::function<void()>& work);

  private:

    DWORD m_thread;

  };


  /**
   * \brief Takes pictures of the launcher's windows
   *
   * The launcher is made of plain Win32 dialogs drawn by GDI, see
   * findings/launcher.md. They are asked to draw into a bitmap, one window
   * at a time, because the child windows, of which the launcher's main
   * dialog is entirely made, cannot be relied on with WM_PRINT under Wine.
   *
   * To be used through \ref VrLauncherThreadCall: from another thread each
   * of the many messages waits for the launcher's loop. A window that does
   * not answer in time skips the picture.
   */
  class VrLauncherCapture {

  public:

    /**
     * \param [in] thread Thread that owns the launcher's windows
     */
    explicit VrLauncherCapture(DWORD thread);

    /**
     * \brief Takes a picture
     * \returns \c true if the picture differs from the previous one
     */
    bool update();

    /**
     * \brief Whether a window is shown at all
     */
    bool hasWindows() const { return m_hasWindows; }

    /**
     * \brief Part of the desktop in the picture
     */
    const VrScreenRect& canvas() const { return m_canvas; }

    /**
     * \brief The picture, RGBA8, top row first
     *
     * Transparent where the launcher has no window.
     */
    const uint8_t* rgba() const { return m_rgba.data(); }

  private:

    /**
     * \brief Bitmap of the canvas's size that GDI draws into
     */
    struct Surface {
      Surface();
      ~Surface();

      Surface(const Surface&) = delete;
      Surface& operator = (const Surface&) = delete;

      HDC       dc        = nullptr;
      HBITMAP   bitmap    = nullptr;
      HGDIOBJ   oldBitmap = nullptr;
      uint32_t* pixels    = nullptr;
    };

    DWORD                 m_thread;
    Surface               m_canvasSurface;
    Surface               m_windowSurface;
    std::vector<uint32_t> m_previous;
    std::vector<uint8_t>  m_rgba;
    VrScreenRect          m_canvas;
    bool                  m_hasWindows = false;

    /**
     * \brief Draws a window and its children on the canvas
     *
     * \param [in] window Window to draw
     * \param [in] clip What may be drawn, in desktop pixels
     * \returns \c false if a window did not answer
     */
    bool paintWindow(HWND window, const RECT& clip);

    /**
     * \brief Copies what a window drew at the bitmap's corner to the canvas
     *
     * \param [in] rect Where the drawing goes, in desktop pixels
     * \param [in] clip What may be drawn, in desktop pixels
     */
    void copyToCanvas(const RECT& rect, const RECT& clip);

    void makeOpaque(HWND window);

  };


  /**
   * \brief Delivers mouse events to the launcher's windows
   *
   * The events are posted as the real mouse's would be: to the window that
   * holds the mouse capture, else to the deepest window under the point.
   * The real cursor does not move, and the window may be anywhere on the
   * desktop or covered. To be used through \ref VrLauncherThreadCall: finding
   * the window asks the windows under the point.
   */
  class VrLauncherMouseRouter {

  public:

    /**
     * \param [in] thread Thread that owns the launcher's windows
     */
    explicit VrLauncherMouseRouter(DWORD thread);

    void send(const VrLauncherMouseEvent& event);

  private:

    DWORD m_thread;
    HWND  m_hover = nullptr;

    HWND targetAt(const POINT& point) const;

  };

}
