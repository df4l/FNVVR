#pragma once

#include "vr_paint.h"

namespace dxvk {

  /**
   * \brief What the game's faders put over the view
   *
   * The faders (fade to black, flash to white) are quads of the interface,
   * which VrFaderSize removes: a quad on a panel leaves the world visible
   * around it. This reads each fader's alpha instead and gives a fill for
   * the whole eye, drawn over the eye image before it goes to the runtime.
   */
  class VrFaderOverlay {

  public:

    /**
     * \brief Checks that the fader table can be read
     */
    bool initialize();

    /**
     * \brief Builds the fills of the faders that are shown now
     *
     * \param [in] width Width of an eye image, in pixels
     * \param [in] height Height of an eye image, in pixels
     * \returns \c false if no fader is shown
     */
    bool update(float width, float height);

    /**
     * \brief Fills to draw over each eye, in the order to draw them
     */
    const VrPaintList& fills() const {
      return m_fills;
    }

  private:

    bool         m_available = false;
    VrPaintList  m_fills = VrPaintList(1, 1);

  };

}
