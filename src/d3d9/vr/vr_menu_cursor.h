#pragma once

#include "vr_menu_pointer.h"

namespace dxvk {

  /**
   * \brief Moves the game's menu cursor and presses its mouse button
   *
   * Two calls of the game are redirected, see findings/input.md:
   *
   * - After OSInputGlobals::Poll has read the mouse, the left button and
   *   the wheel are replaced with the pointer's, and the real mouse's
   *   movement is dropped, while the pointer drives the cursor.
   * - Before InterfaceManager::UpdateCursor, the cursor's node is placed
   *   where the laser points. The game then derives the cursor's screen
   *   position, and the tile under it, as it does for the mouse.
   *
   * The interface only uses the cursor in mouse mode, which VrGamepadHook
   * switches to while the pointer drives the cursor.
   */
  class VrMenuCursor {

  public:

    /**
     * \brief Redirects the game's calls
     * \returns \c true if both calls matched and were redirected
     */
    static bool install();

    /**
     * \brief Whether the pointer may be used in the menus shown now
     *
     * Lockpicking and V.A.T.S. are driven by the sticks, which the
     * interface ignores in mouse mode.
     */
    static bool pointerAllowed();

    /**
     * \brief Sets the pointer's state for the next game frame
     *
     * Called once per frame, on the game's thread.
     */
    static void update(const VrPointerState& state);

  };

}
