#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_bitmap_font.h"
#include "vr_dds.h"

namespace dxvk {

  /**
   * \brief Colour with straight alpha, each channel from 0 to 1
   */
  struct VrColour {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    /// The same colour with its alpha scaled
    VrColour faded(float factor) const {
      return { r, g, b, a * factor };
    }

    /// The colour from 0xRRGGBBAA, as the game stores the HUD's colour
    static VrColour fromRgba(uint32_t rgba);
  };

  /**
   * \brief A game font and the texture holding its glyphs
   */
  struct VrPaintFont {
    VrBitmapFont metrics;
    /// The font's texture, glyphs in its alpha
    VrImage      glyphs;
  };

  /**
   * \brief How the GPU reads an image drawn on a panel
   */
  enum class VrPaintImageUse : uint32_t {
    /// The image's alpha times its brightness, in the drawing's colour,
    /// as the game tints its interface icons; filtered with mipmaps
    Shape,
    /// The image's alpha in the drawing's colour, such as a font's glyphs
    Mask,
    /// The image's colours, multiplied by the drawing's colour
    Picture,
    /// The image's colours, filtered with mipmaps
    PictureWithMipmaps,
  };

  /**
   * \brief How what is drawn is blended with what is there
   *
   * Colours are premultiplied by their alpha: a panel is drawn with
   * premultiplied alpha, and a picture drawn Over must be premultiplied.
   */
  enum class VrPaintBlend : uint32_t {
    /// Over what is there
    Over,
    /// Over what is there, whose alpha is kept
    OverKeepAlpha,
    /// Replaces what is there
    Copy,
    /// Multiplies the colours there, whose alpha is kept
    Multiply,
    /// Multiplies the colours there by one plus the drawn colour, alpha kept
    Brighten,
    /// Adds to the colours there, whose alpha is kept
    Add,
  };

  /**
   * \brief Corner of a triangle drawn on a panel
   *
   * Position in pixels of the target, its top left corner at 0 and pixel
   * centres at half pixels; texture coordinates from 0 to 1 across the
   * image; colour 0xAARRGGBB premultiplied by its alpha.
   */
  struct VrPaintVertex {
    float    x      = 0.0f;
    float    y      = 0.0f;
    float    u      = 0.0f;
    float    v      = 0.0f;
    uint32_t colour = 0u;
  };

  /**
   * \brief Triangles drawn with one image and one blending
   */
  struct VrPaintBatch {
    /// The image, or \c nullptr to draw the vertex colours only
    const VrImage*  image = nullptr;
    VrPaintImageUse use   = VrPaintImageUse::Shape;
    VrPaintBlend    blend = VrPaintBlend::Over;
    /// First vertex and number of vertices, three per triangle
    uint32_t        first = 0;
    uint32_t        count = 0;
  };

  /**
   * \brief What the GPU draws on a panel, see VrPainter
   *
   * Shapes are made of triangles whose edges fade out over one pixel, so
   * that they are smooth without multisampling. Angles are in radians,
   * clockwise from the top of the target. Each drawing is blended as
   * setBlend last chose, Over at first.
   */
  class VrPaintList {

  public:

    VrPaintList(uint32_t width, uint32_t height)
    : m_width(width), m_height(height) { }

    uint32_t width() const {
      return m_width;
    }

    uint32_t height() const {
      return m_height;
    }

    const std::vector<VrPaintVertex>& vertices() const {
      return m_vertices;
    }

    const std::vector<VrPaintBatch>& batches() const {
      return m_batches;
    }

    /**
     * \brief Forgets everything drawn, and blends Over again
     */
    void clear();

    /**
     * \brief Chooses how the next drawings are blended
     */
    void setBlend(VrPaintBlend blend) {
      m_blend = blend;
    }

    /**
     * \brief Fills a rectangle with sharp edges
     *
     * Edges between pixels stay sharp; others cover pixels partly.
     */
    void fillRect(float left, float top, float right, float bottom, const VrColour& colour);

    /**
     * \brief Fills a rectangle with rounded corners
     *
     * \param [in] left Left edge, in pixels
     * \param [in] top Top edge, in pixels
     * \param [in] right Right edge, in pixels
     * \param [in] bottom Bottom edge, in pixels
     * \param [in] radius Radius of the corners, in pixels
     * \param [in] colour Colour
     */
    void fillRoundedRect(float left, float top, float right, float bottom, float radius, const VrColour& colour);

    /**
     * \brief Fills part of a ring
     *
     * The sector runs clockwise from \c start to \c end, \c end may wrap
     * past 2 pi; a span of 2 pi or more is the whole ring.
     *
     * \param [in] cx Horizontal centre, in pixels
     * \param [in] cy Vertical centre, in pixels
     * \param [in] inner Inner radius, in pixels, 0 for a disc
     * \param [in] outer Outer radius, in pixels
     * \param [in] start First angle
     * \param [in] end Last angle
     * \param [in] colour Colour
     */
    void fillSector(float cx, float cy, float inner, float outer, float start, float end, const VrColour& colour);

    /**
     * \brief Fills a ring whose colour fades from its inner to its outer edge
     *
     * \param [in] cx Horizontal centre, in pixels
     * \param [in] cy Vertical centre, in pixels
     * \param [in] inner Inner radius, in pixels
     * \param [in] outer Outer radius, in pixels
     * \param [in] innerColour Colour at the inner edge
     * \param [in] outerColour Colour at the outer edge
     */
    void fillGlow(float cx, float cy, float inner, float outer, const VrColour& innerColour, const VrColour& outerColour);

    /**
     * \brief Draws an image's shape in one colour
     *
     * See VrPaintImageUse::Shape. The image keeps its aspect and fits in a
     * square.
     *
     * \param [in] image The image
     * \param [in] cx Horizontal centre of the square, in pixels
     * \param [in] cy Vertical centre of the square, in pixels
     * \param [in] size Side of the square, in pixels
     * \param [in] colour Colour
     */
    void drawShape(const VrImage& image, float cx, float cy, float size, const VrColour& colour);

    /**
     * \brief Draws part of an image on a rectangle
     *
     * \param [in] image The image
     * \param [in] use How the image is read
     * \param [in] left Left edge of the rectangle, in pixels
     * \param [in] top Top edge, in pixels
     * \param [in] right Right edge, in pixels
     * \param [in] bottom Bottom edge, in pixels
     * \param [in] u0 Left edge of the part of the image, from 0 to 1
     * \param [in] v0 Top edge of the part
     * \param [in] u1 Right edge of the part
     * \param [in] v1 Bottom edge of the part
     * \param [in] colour Colour the image is multiplied by
     */
    void drawImage(const VrImage& image, VrPaintImageUse use, float left, float top, float right, float bottom,
      float u0, float v0, float u1, float v1, const VrColour& colour);

    /**
     * \brief Width of a line of text, in pixels
     */
    static float measureText(const VrPaintFont& font, const std::string& text, float scale);

    /**
     * \brief Draws a line of text
     *
     * \param [in] font The font
     * \param [in] text The text, one byte per character
     * \param [in] x Left end of the line, in pixels
     * \param [in] baseline Height of the line's baseline, in pixels
     * \param [in] scale Pixels per font pixel
     * \param [in] colour Colour
     */
    void drawText(const VrPaintFont& font, const std::string& text, float x, float baseline, float scale,
      const VrColour& colour);

    /**
     * \brief Draws lines of text centred on a point
     *
     * Words are wrapped to fit the width; lines that do not fit in
     * \c maxLines are dropped, the last one shown ending with "...".
     * The text shrinks down to \c minScale before it wraps.
     *
     * \param [in] font The font
     * \param [in] text The text
     * \param [in] cx Horizontal centre, in pixels
     * \param [in] cy Vertical centre of the lines, in pixels
     * \param [in] maxWidth Widest a line may be, in pixels
     * \param [in] scale Pixels per font pixel
     * \param [in] minScale Smallest scale before wrapping
     * \param [in] maxLines Most lines drawn
     * \param [in] colour Colour
     */
    void drawTextBlock(const VrPaintFont& font, const std::string& text, float cx, float cy, float maxWidth,
      float scale, float minScale, uint32_t maxLines, const VrColour& colour);

  private:

    uint32_t                   m_width;
    uint32_t                   m_height;
    VrPaintBlend               m_blend = VrPaintBlend::Over;
    std::vector<VrPaintVertex> m_vertices;
    std::vector<VrPaintBatch>  m_batches;

    void begin(const VrImage* image, VrPaintImageUse use);

    void triangle(const VrPaintVertex& a, const VrPaintVertex& b, const VrPaintVertex& c);

    void quad(const VrPaintVertex& a, const VrPaintVertex& b, const VrPaintVertex& c, const VrPaintVertex& d);

  };

  /**
   * \brief Splits a text into lines no wider than a width
   *
   * Words longer than the width are kept whole on their own line.
   *
   * \param [in] font The font
   * \param [in] text The text
   * \param [in] scale Pixels per font pixel
   * \param [in] maxWidth Widest a line may be, in pixels
   */
  std::vector<std::string> vrWrapText(const VrPaintFont& font, const std::string& text, float scale, float maxWidth);

  /**
   * \brief A colour as 0xAARRGGBB premultiplied by its alpha
   */
  uint32_t vrPremultiplied(const VrColour& colour);

}
