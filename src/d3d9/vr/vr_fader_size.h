#pragma once

namespace dxvk {

  /**
   * \brief Makes the game's faders cover the whole of each eye
   *
   * FaderManager::CreateFader builds its quad from half the screen's width
   * and height, which fills the flat screen exactly. Drawn with an eye's
   * wider projection, the fade to black of a cell change left the edges of
   * the view uncovered. Without the halving the quad is twice as large and
   * reaches past every edge. See findings/render.md, faders.
   */
  class VrFaderSize {

  public:

    /**
     * \brief Removes the halving from faders created from now on
     *
     * Logs if the code is not the supported version, which is then left
     * as it is.
     */
    static void install();

  };

}
