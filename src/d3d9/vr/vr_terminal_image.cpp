#include <algorithm>
#include <cmath>

#include "vr_terminal_image.h"

namespace dxvk {

  namespace {

    // Texels darker than this, joined to the plate's edge, are the empty
    // texture around the plate's pieces and are left transparent
    constexpr uint32_t BackgroundLuminance = 24;

    constexpr float TwoPi = 6.2831853f;

    const VrColour White = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Dark ink of the keys' names, and how much of it covers a key
    const VrColour Ink = VrColour::fromRgba(0x2E2B22FFu).faded(0.9f);

    // Size of a key's name: one character in font pixels per image
    // pixel, longer names shrunk to fit the key
    constexpr float CharScale      = 1.2f;
    constexpr float NameScale      = 0.72f;
    constexpr float MinScale       = 0.44f;
    constexpr float NameWidthShare = 0.85f;

    // A lit key is brightened, a pressed key darkened and its name moved down
    constexpr float HoverGain    = 1.3f;
    constexpr float HoverOffset  = 24.0f / 255.0f;
    constexpr float PressedGain  = 0.7f;
    constexpr float PressedShift = 1.6f;

    // The orange light while Shift or Caps Lock is on: its core, then a
    // glow fading out to GlowExtent times its radius
    const VrColour Light = { 1.0f, 168.0f / 255.0f, 64.0f / 255.0f, 1.0f };
    constexpr float GlowExtent  = 3.0f;
    constexpr float GlowOpacity = 0.6f;

    uint32_t channel(uint32_t pixel, uint32_t shift) {
      return (pixel >> shift) & 0xFF;
    }

    uint32_t luminance(uint32_t pixel) {
      return (channel(pixel, 16) * 3 + channel(pixel, 8) * 6 + channel(pixel, 0)) / 10;
    }

  }


  bool VrTerminalImage::initialize(const VrImage& texture, const VrBitmapFont& font, const VrImage& glyphs, const VrTexts& texts) {
    m_plate = VrImage();

    if (texture.width < uint32_t(VrTerminalKeyboard::Plate.right) + 1
     || texture.height < uint32_t(VrTerminalKeyboard::Plate.bottom) + 1
     || !glyphs.width || !glyphs.height)
      return false;

    m_font.metrics = font;
    m_font.glyphs  = glyphs;
    buildPlate(texture);

    uint32_t count = 0;
    const VrTerminalKey* keys = VrTerminalKeyboard::keys(count);
    m_labels.resize(count);

    for (uint32_t i = 0; i < count; i++) {
      if (keys[i].label)
        m_labels[i] = texts.text((std::string("key.") + keys[i].label).c_str(), keys[i].label);
    }

    return true;
  }


  void VrTerminalImage::paint(VrPaintList& list, const VrTerminalKeyboardView& view, float left, float top,
      const VrTexelRect* area, bool keepAlpha) const {
    if (m_plate.pixels.empty())
      return;

    const VrTexelRect& plate = VrTerminalKeyboard::Plate;
    const VrTexelRect& drawn = area ? *area : plate;

    // From texels of the terminal texture to pixels of the list
    auto x = [&] (float texelX) { return left + (texelX - plate.left) * float(Scale); };
    auto y = [&] (float texelY) { return top  + (texelY - plate.top)  * float(Scale); };

    VrPaintBlend over = keepAlpha ? VrPaintBlend::OverKeepAlpha : VrPaintBlend::Over;

    list.setBlend(over);
    list.drawImage(m_plate, VrPaintImageUse::Picture, x(drawn.left), y(drawn.top), x(drawn.right), y(drawn.bottom),
      (drawn.left  - plate.left) / plate.width(),  (drawn.top    - plate.top) / plate.height(),
      (drawn.right - plate.left) / plate.width(),  (drawn.bottom - plate.top) / plate.height(), White);

    uint32_t count = 0;
    const VrTerminalKey* keys = VrTerminalKeyboard::keys(count);

    // A pressed key is darkened, a lit one brightened
    for (uint32_t i = 0; i < count; i++) {
      const VrTexelRect& rect = keys[i].rect;

      if (int32_t(i) == view.pressedKey) {
        list.setBlend(VrPaintBlend::Multiply);
        list.fillRect(x(rect.left), y(rect.top), x(rect.right), y(rect.bottom),
          { PressedGain, PressedGain, PressedGain, 1.0f });
      } else if (int32_t(i) == view.hoveredKey) {
        list.setBlend(VrPaintBlend::Brighten);
        list.fillRect(x(rect.left), y(rect.top), x(rect.right), y(rect.bottom),
          { HoverGain - 1.0f, HoverGain - 1.0f, HoverGain - 1.0f, 1.0f });
        list.setBlend(VrPaintBlend::Add);
        list.fillRect(x(rect.left), y(rect.top), x(rect.right), y(rect.bottom),
          { HoverOffset, HoverOffset, HoverOffset, 1.0f });
      }
    }

    // The names, printed on the keys
    list.setBlend(VrPaintBlend::OverKeepAlpha);

    for (uint32_t i = 0; i < count; i++) {
      const VrTerminalKey& key = keys[i];

      char text[2] = { VrTerminalKeyboard::isShifted(key, view.shift, view.capsLock) ? key.upper : key.lower, '\0' };
      const char* label = key.label ? m_labels[i].c_str() : text;

      float scale = label[0] && label[1] ? NameScale : CharScale;
      float shift = int32_t(i) == view.pressedKey ? PressedShift : 0.0f;

      paintLabel(list, label, 0.5f * (x(key.rect.left) + x(key.rect.right)),
        0.5f * (y(key.rect.top) + y(key.rect.bottom)) + shift,
        (x(key.rect.right) - x(key.rect.left)) * NameWidthShare, scale);
    }

    // The light, opaque, then its glow fading out
    if (view.shift || view.capsLock) {
      float cx     = x(VrTerminalKeyboard::LightX);
      float cy     = y(VrTerminalKeyboard::LightY);
      float radius = VrTerminalKeyboard::LightRadius * float(Scale);

      list.setBlend(over);
      list.fillSector(cx, cy, 0.0f, radius, 0.0f, TwoPi, Light);
      list.fillGlow(cx, cy, radius, radius * GlowExtent, Light.faded(GlowOpacity), Light.faded(0.0f));
    }

    list.setBlend(VrPaintBlend::Over);
  }


  void VrTerminalImage::buildPlate(const VrImage& texture) {
    const VrTexelRect& plate = VrTerminalKeyboard::Plate;

    uint32_t left = uint32_t(plate.left);
    uint32_t top  = uint32_t(plate.top);
    uint32_t w    = uint32_t(plate.width());
    uint32_t h    = uint32_t(plate.height());

    // The plate's texels, with the empty texture around its pieces made
    // transparent: a flood fill from the edge through dark texels
    m_plate.width  = w;
    m_plate.height = h;
    m_plate.pixels.resize(size_t(w) * h);

    const VrTexelRect& other = VrTerminalKeyboard::OtherPiece;

    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        float tx = float(left + x);
        float ty = float(top + y);

        bool outside = tx >= other.left && tx < other.right && ty >= other.top && ty < other.bottom;
        m_plate.pixels[size_t(y) * w + x] = outside ? 0u : texture.at(left + x, top + y) | 0xFF000000u;
      }
    }

    std::vector<uint32_t> stack;

    auto push = [&] (uint32_t x, uint32_t y) {
      uint32_t& texel = m_plate.pixels[size_t(y) * w + x];

      if ((texel >> 24) && luminance(texel) < BackgroundLuminance) {
        texel = 0u;
        stack.push_back(y * w + x);
      }
    };

    for (uint32_t x = 0; x < w; x++) {
      push(x, 0);
      push(x, h - 1);
    }

    for (uint32_t y = 0; y < h; y++) {
      push(0, y);
      push(w - 1, y);
    }

    while (!stack.empty()) {
      uint32_t index = stack.back();
      stack.pop_back();

      uint32_t x = index % w;
      uint32_t y = index / w;

      if (x > 0)     push(x - 1, y);
      if (x + 1 < w) push(x + 1, y);
      if (y > 0)     push(x, y - 1);
      if (y + 1 < h) push(x, y + 1);
    }
  }


  void VrTerminalImage::paintLabel(VrPaintList& list, const char* text, float centreX, float centreY,
      float maxWidth, float scale) const {
    float width = VrPaintList::measureText(m_font, text, 1.0f);

    if (width <= 0.0f)
      return;

    scale = std::max(std::min(scale, maxWidth / width), MinScale);

    // Centred on the height of the capitals
    float capitals = m_font.metrics.glyph('A').ascent;
    list.drawText(m_font, text, centreX - 0.5f * width * scale, centreY + 0.5f * capitals * scale, scale, Ink);
  }

}
