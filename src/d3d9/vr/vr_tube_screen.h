#pragma once

#include "../d3d9_include.h"
#include "../../util/com/com_pointer.h"

#include "vr_painter.h"

namespace dxvk {

  /**
   * \brief The picture of a pre-war tube screen
   *
   * Turns a view of the world into what a small monochrome picture tube shows:
   * curved glass, a soft signal, scanlines, static, a rolling hum bar, phosphor
   * that keeps glowing, framing marks and a recording light. The GPU draws it
   * with a pixel shader, findings/optics.md §13.
   *
   * Only used on the game's main thread.
   */
  class VrTubeScreen {

  public:

    VrTubeScreen(IDirect3DDevice9* device, VrPainter& painter);

    /**
     * \brief Draws the next picture
     *
     * \param [in] view The view to show, or \c nullptr to keep the last one
     * \param [in] shutterFlash The shutter's flash, from 0 to 1
     * \returns \c false if there is no picture to show
     */
    bool update(IDirect3DTexture9* view, float shutterFlash);

    /**
     * \brief The latest picture, with mipmaps
     */
    IDirect3DTexture9* picture() const {
      return m_pictures[m_latest].ptr();
    }

  private:

    IDirect3DDevice9*          m_device;
    VrPainter&                 m_painter;
    Com<IDirect3DPixelShader9> m_shader;
    Com<IDirect3DTexture9>     m_pictures[2];
    uint32_t                   m_latest  = 0;
    uint32_t                   m_counter = 0;
    bool                       m_failed  = false;
    Com<IDirect3DTexture9>     m_view;

    bool prepare();

  };

}
