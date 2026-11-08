#pragma once

#include <array>
#include <cstdint>

#include "vr_caravan_pick.h"
#include "vr_game_images.h"
#include "vr_paint_panel.h"

namespace dxvk {

  /**
   * \brief Draws the values of the tracks
   *
   * Each value on a small dark plate, in the HUD's colour, red once the
   * track has bust, as the game's tiles show them.
   *
   * \param [out] list Paint list of VrCaravanBoard::ImageSize squared
   * \param [in] board The values and where they go
   * \param [in] colour The HUD's colour
   * \param [in] font Font of the text, or \c nullptr to draw nothing
   */
  void vrPaintCaravanValues(VrPaintList& list, const VrCaravanBoard& board,
    const VrColour& colour, const VrPaintFont* font);

  /**
   * \brief The values of Caravan's tracks, lying on the mat at the end of
   *    each track (findings/caravan.md §3.4)
   */
  class VrCaravanValues {

  public:

    VrCaravanValues(VrPainter& painter, IVRBackend& backend, VrGameImages& images);

    /**
     * \brief Shows the values
     * \returns Whether they are shown
     */
    bool show(const VrCaravanBoard& board);

    void hide();

  private:

    VrPaintPanel   m_panel;
    VrPaintList    m_list;
    VrGameImages&  m_images;

    // What the texture shows
    VrCaravanBoard m_drawnBoard;
    bool           m_drawn  = false;
    bool           m_failed = false;

  };

}
