#pragma once

#include "../d3d9_include.h"

namespace dxvk {

  /**
   * \brief Keeps the game's window off the desktop while a headset is in use
   *
   * With a headset the game renders at the eye's height, in a window far
   * larger than most monitors, that shows nothing the player needs. The
   * game has no use for the window either: the controllers reach it through
   * the virtual gamepad and the menu cursor's hook, and the sounds and the
   * game's timer do not depend on the focus (VrSoundFocus,
   * VrGameSettings::keepActive). See findings/resolution.md.
   */
  class VrGameWindow {

  public:

    /**
     * \brief Hides the window of a swap chain, if it is shown
     *
     * Called on every present: the game could show the window again.
     *
     * \returns \c true if the window is hidden
     */
    static bool hide(IDirect3DSwapChain9* swapchain);

    /**
     * \brief Waits for about a headset frame
     *
     * For the frames nothing else paces, such as a loading screen's.
     */
    static void waitUnpacedFrame();

  };

}
