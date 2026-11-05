#pragma once

#include <cstdint>
#include <vector>

#include "vr_bitmap_font.h"
#include "vr_dds.h"
#include "vr_paint.h"
#include "vr_terminal_keyboard.h"
#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief What the keyboard shows
   */
  struct VrTerminalKeyboardView {
    /// Key the active laser points at, -1 if none
    int32_t hoveredKey = -1;
    /// Key drawn pressed, -1 if none
    int32_t pressedKey = -1;
    bool    shift      = false;
    bool    capsLock   = false;

    bool operator == (const VrTerminalKeyboardView& other) const {
      return hoveredKey == other.hoveredKey && pressedKey == other.pressedKey
          && shift == other.shift && capsLock == other.capsLock;
    }

    bool operator != (const VrTerminalKeyboardView& other) const {
      return !(*this == other);
    }
  };

  /**
   * \brief Image of the console's keyboard
   *
   * The keyboard plate of the game's terminal texture, enlarged, with the
   * name of each key printed on it in the game's terminal font, the key
   * the laser points at lit, the key just typed pressed down, and the
   * orange light lit while Shift or Caps Lock is on. The plate's dark
   * surroundings on the texture are transparent. Drawn by the GPU, see
   * VrPainter.
   */
  class VrTerminalImage {

  public:

    /// Image pixels per texel of the terminal texture, the scale of the
    /// 3D keyboard's texture too (VrTerminalTexture)
    constexpr static uint32_t Scale  = 4;
    constexpr static uint32_t Width  = uint32_t(VrTerminalKeyboard::Plate.width())  * Scale;
    constexpr static uint32_t Height = uint32_t(VrTerminalKeyboard::Plate.height()) * Scale;

    /**
     * \brief Prepares the plate and the font
     *
     * \param [in] texture The terminal texture (terminal01.dds)
     * \param [in] font The terminal font's metrics
     * \param [in] glyphs The terminal font's texture, glyphs in its alpha
     * \param [in] texts The names printed on the keys
     * \returns \c false if the texture is too small to hold the plate
     */
    bool initialize(const VrImage& texture, const VrBitmapFont& font, const VrImage& glyphs, const VrTexts& texts);

    /**
     * \brief The plate's texels, premultiplied by their alpha
     *
     * Each texel is opaque or transparent black, so that the GPU's
     * filtering does not darken the plate's edges. Empty until
     * initialized.
     */
    const VrImage& plate() const {
      return m_plate;
    }

    /**
     * \brief Draws the keyboard
     *
     * \param [out] list Paint list the keyboard is added to
     * \param [in] view What the keyboard shows
     * \param [in] left Where the plate's left edge is, in pixels of the list
     * \param [in] top Where the plate's top edge is
     * \param [in] area The part of the plate drawn, in texels of the
     *    terminal texture, or \c nullptr for all of it; the keys must lie
     *    in it
     * \param [in] keepAlpha Whether the alpha already drawn is kept, as on
     *    the terminal texture, instead of the plate's
     */
    void paint(VrPaintList& list, const VrTerminalKeyboardView& view, float left, float top,
      const VrTexelRect* area, bool keepAlpha) const;

  private:

    VrImage     m_plate;
    VrPaintFont m_font;
    /// What is printed on each named key, in the order of the keys
    std::vector<std::string> m_labels;

    void buildPlate(const VrImage& texture);

    void paintLabel(VrPaintList& list, const char* text, float centreX, float centreY, float maxWidth,
      float scale) const;

  };

}
