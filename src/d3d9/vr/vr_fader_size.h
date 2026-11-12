#pragma once

namespace dxvk {

  /**
   * \brief Takes the game's fader quads out of the interface
   *
   * FaderManager::CreateFader builds each fader as a screen-sized quad of
   * the interface, which in the headset lands on a flat panel: the fade to
   * black of a cell change then showed as a black rectangle with the world
   * around it. The quads are given no extent, and VrFaderOverlay draws the
   * faders over each whole eye instead. The game keeps running its faders
   * as before: only what they draw is gone. See findings/render.md, faders.
   */
  class VrFaderSize {

  public:

    /**
     * \brief Gives the faders created from now on no extent
     *
     * Logs if the code is not the supported version, which is then left
     * as it is.
     *
     * \returns \c true if the quads are gone, and the overlay must draw them
     */
    static bool install();

  };

}
