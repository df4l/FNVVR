#pragma once

#include <cstdint>

#include "vr_pipboy_gaze.h"

namespace dxvk {

  /**
   * \brief Opens the game's Pip-Boy and reads its state
   *
   * Reads the Pip-Boy's state machine and makes the same request as the
   * game's Pip-Boy control, so the game raises the arm, enters menu mode
   * and checks whether the player may open it (V.A.T.S., controls
   * disabled, ...). See findings/pipboy.md §1. The Pip-Boy is closed with
   * the menu's back button instead (VrControls::setMenuBack).
   *
   * The game opens the Pip-Boy when its own Pip-Boy control is released,
   * and that control is the menu's back button on the gamepad. Once the
   * arm is no longer lowered (VrPipBoyAnimation), the release of the back
   * button that closed the Pip-Boy came after it was closed, and opened it
   * again. The game's control can be blocked for that time.
   *
   * Only used on the game's main thread, outside InterfaceManager::Update.
   */
  class VrPipBoyControl {

  public:

    ~VrPipBoyControl();

    /**
     * \brief Checks that the executable is the supported version
     * \returns \c false if the Pip-Boy cannot be driven
     */
    bool initialize();

    /**
     * \brief Reads the Pip-Boy's state
     *
     * Reports \c Changing when the state cannot be read, so that nothing
     * is requested.
     */
    VrPipBoyState state() const;

    /**
     * \brief Asks the game to open the Pip-Boy on its last tab
     */
    void open() const;

    /**
     * \brief How long the game's Pip-Boy control is held to switch the light
     *
     * The game switches the Pip-Boy light itself when its Pip-Boy control
     * is held that long while the Pip-Boy is closed, see findings/pipboy.md §6.
     *
     * \returns Duration in seconds, or 0 if the light cannot be switched
     */
    float lightHoldTime() const;

    /**
     * \brief Blocks the game's own Pip-Boy control from opening it
     * \param [in] blocked Whether the control's requests are dropped
     */
    void blockGameOpen(bool blocked);

  private:

    bool m_available = false;
    bool m_lightAvailable = false;
    bool m_gameOpenBlocked = false;
    bool m_loggedBlockedOpen = false;

    static bool chainGameOpen();

    static void __fastcall gameOpenHook(
            void*                 interfaceManager,
            void*                 unused,
            void*                 callback,
            int32_t               menu);

  };

}
