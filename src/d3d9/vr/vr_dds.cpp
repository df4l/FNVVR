#include <algorithm>
#include <cstring>
#include <sstream>

#include "vr_dds.h"

namespace dxvk {

  namespace {

    constexpr uint32_t Magic             = 0x20534444u; // "DDS "
    constexpr size_t   HeaderSize        = 128;
    constexpr size_t   HeightOffset      = 12;
    constexpr size_t   WidthOffset       = 16;
    constexpr size_t   FormatFlagsOffset = 80;
    constexpr size_t   FourCcOffset      = 84;
    constexpr size_t   BitCountOffset    = 88;
    constexpr size_t   MasksOffset       = 92;

    // The DX10 extension follows the header, its DXGI format first
    constexpr size_t   Dx10HeaderSize    = 20;

    constexpr uint32_t FlagAlphaPixels = 0x1;
    constexpr uint32_t FlagAlphaOnly   = 0x2;
    constexpr uint32_t FlagFourCc      = 0x4;
    constexpr uint32_t FlagLuminance   = 0x20000;

    // DXGI formats of the same encodings, plain and sRGB
    constexpr uint32_t DxgiR10G10B10A2  = 24;
    constexpr uint32_t DxgiR8G8B8A8     = 28;
    constexpr uint32_t DxgiR8G8B8A8Srgb = 29;
    constexpr uint32_t DxgiA8           = 65;
    constexpr uint32_t DxgiBc1          = 71;
    constexpr uint32_t DxgiBc1Srgb      = 72;
    constexpr uint32_t DxgiBc2          = 74;
    constexpr uint32_t DxgiBc2Srgb      = 75;
    constexpr uint32_t DxgiBc3          = 77;
    constexpr uint32_t DxgiBc3Srgb      = 78;
    constexpr uint32_t DxgiB5G6R5       = 85;
    constexpr uint32_t DxgiB5G5R5A1     = 86;
    constexpr uint32_t DxgiB8G8R8A8     = 87;
    constexpr uint32_t DxgiB8G8R8X8     = 88;
    constexpr uint32_t DxgiB8G8R8A8Srgb = 91;
    constexpr uint32_t DxgiB8G8R8X8Srgb = 93;
    constexpr uint32_t DxgiB4G4R4A4     = 115;

    // D3DFORMAT values some tools write as the four-character code of
    // uncompressed images instead of channel masks
    constexpr uint32_t D3dR8G8B8        = 20;
    constexpr uint32_t D3dA8R8G8B8      = 21;
    constexpr uint32_t D3dX8R8G8B8      = 22;
    constexpr uint32_t D3dR5G6B5        = 23;
    constexpr uint32_t D3dX1R5G5B5      = 24;
    constexpr uint32_t D3dA1R5G5B5      = 25;
    constexpr uint32_t D3dA4R4G4B4      = 26;
    constexpr uint32_t D3dA8            = 28;
    constexpr uint32_t D3dX4R4G4B4      = 30;
    constexpr uint32_t D3dA2B10G10R10   = 31;
    constexpr uint32_t D3dA8B8G8R8      = 32;
    constexpr uint32_t D3dX8B8G8R8      = 33;
    constexpr uint32_t D3dA2R10G10B10   = 35;
    constexpr uint32_t D3dL8            = 50;
    constexpr uint32_t D3dA8L8          = 51;
    constexpr uint32_t D3dA4L4          = 52;

    // Images larger than this are not textures of the game
    constexpr uint32_t MaxExtent = 16384;

    constexpr uint32_t fourCc(char a, char b, char c, char d) {
      return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8
           | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
    }

    uint32_t read32(const uint8_t* data) {
      uint32_t value;
      std::memcpy(&value, data, sizeof(value));
      return value;
    }

    uint16_t read16(const uint8_t* data) {
      uint16_t value;
      std::memcpy(&value, data, sizeof(value));
      return value;
    }

    uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
      return a << 24 | r << 16 | g << 8 | b;
    }

    // R5G6B5 to 8 bits per channel, alpha left at zero
    void expand565(uint16_t c, uint32_t rgb[3]) {
      uint32_t r = (c >> 11) & 0x1F;
      uint32_t g = (c >> 5)  & 0x3F;
      uint32_t b =  c        & 0x1F;
      rgb[0] = (r << 3) | (r >> 2);
      rgb[1] = (g << 2) | (g >> 4);
      rgb[2] = (b << 3) | (b >> 2);
    }

    // The colour part of a DXT block: two end points and 2-bit indices.
    // DXT1 blocks whose first end point is not above the second have a
    // transparent fourth colour, the other formats always use four colours.
    void decodeColours(const uint8_t* block, bool dxt1, uint32_t out[16], bool transparent[16]) {
      uint16_t c0 = read16(block);
      uint16_t c1 = read16(block + 2);
      uint32_t indices = read32(block + 4);

      uint32_t palette[4][3];
      expand565(c0, palette[0]);
      expand565(c1, palette[1]);

      bool threeColours = dxt1 && c0 <= c1;

      for (uint32_t i = 0; i < 3; i++) {
        if (threeColours) {
          palette[2][i] = (palette[0][i] + palette[1][i]) / 2;
          palette[3][i] = 0;
        } else {
          palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3;
          palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3;
        }
      }

      for (uint32_t i = 0; i < 16; i++) {
        uint32_t index = (indices >> (2 * i)) & 0x3;
        out[i] = argb(0, palette[index][0], palette[index][1], palette[index][2]);
        transparent[i] = threeColours && index == 3;
      }
    }

    // DXT3: 4 explicit bits per pixel
    void decodeExplicitAlpha(const uint8_t* block, uint32_t alpha[16]) {
      for (uint32_t i = 0; i < 16; i++) {
        uint32_t a = (block[i / 2] >> (4 * (i % 2))) & 0xF;
        alpha[i] = a * 17;
      }
    }

    // DXT5: two end points and 3-bit indices
    void decodeInterpolatedAlpha(const uint8_t* block, uint32_t alpha[16]) {
      uint32_t a0 = block[0];
      uint32_t a1 = block[1];

      uint32_t palette[8] = { a0, a1 };

      if (a0 > a1) {
        for (uint32_t i = 1; i < 7; i++)
          palette[i + 1] = ((7 - i) * a0 + i * a1) / 7;
      } else {
        for (uint32_t i = 1; i < 5; i++)
          palette[i + 1] = ((5 - i) * a0 + i * a1) / 5;

        palette[6] = 0;
        palette[7] = 255;
      }

      uint64_t indices = 0;

      for (uint32_t i = 0; i < 6; i++)
        indices |= uint64_t(block[2 + i]) << (8 * i);

      for (uint32_t i = 0; i < 16; i++)
        alpha[i] = palette[(indices >> (3 * i)) & 0x7];
    }

    bool decodeBlocks(const uint8_t* data, size_t size, uint32_t format, VrImage& image) {
      bool   dxt1      = format == fourCc('D', 'X', 'T', '1');
      size_t blockSize = dxt1 ? 8 : 16;

      uint32_t blocksX = (image.width  + 3) / 4;
      uint32_t blocksY = (image.height + 3) / 4;

      if (size < size_t(blocksX) * blocksY * blockSize)
        return false;

      for (uint32_t by = 0; by < blocksY; by++) {
        for (uint32_t bx = 0; bx < blocksX; bx++) {
          const uint8_t* block = data + (size_t(by) * blocksX + bx) * blockSize;

          uint32_t colours[16];
          bool     transparent[16];
          uint32_t alpha[16];

          if (dxt1) {
            decodeColours(block, true, colours, transparent);

            for (uint32_t i = 0; i < 16; i++)
              alpha[i] = transparent[i] ? 0 : 255;
          } else {
            decodeColours(block + 8, false, colours, transparent);

            if (format == fourCc('D', 'X', 'T', '5') || format == fourCc('D', 'X', 'T', '4'))
              decodeInterpolatedAlpha(block, alpha);
            else
              decodeExplicitAlpha(block, alpha);
          }

          for (uint32_t i = 0; i < 16; i++) {
            uint32_t x = bx * 4 + i % 4;
            uint32_t y = by * 4 + i / 4;

            if (x < image.width && y < image.height)
              image.pixels[size_t(y) * image.width + x] = colours[i] | alpha[i] << 24;
          }
        }
      }

      return true;
    }

    // A channel of an uncompressed pixel, scaled to 8 bits
    uint32_t channel(uint32_t pixel, uint32_t mask, uint32_t missing) {
      if (!mask)
        return missing;

      uint32_t shift = 0;

      while (!((mask >> shift) & 1))
        shift++;

      uint32_t maximum = mask >> shift;
      return ((pixel & mask) >> shift) * 255 / maximum;
    }

    // Uncompressed pixels described by their channel masks: red, green,
    // blue and alpha. A luminance image keeps its one value in red.
    struct PixelLayout {
      uint32_t bits      = 0;
      uint32_t masks[4]  = { };
      bool     luminance = false;
    };

    bool decodePixels(const uint8_t* data, size_t size, const PixelLayout& layout, VrImage& image) {
      if (layout.bits != 8 && layout.bits != 16 && layout.bits != 24 && layout.bits != 32)
        return false;

      size_t bytesPerPixel = layout.bits / 8;

      if (size < size_t(image.width) * image.height * bytesPerPixel)
        return false;

      const uint32_t* masks = layout.masks;

      for (size_t i = 0; i < image.pixels.size(); i++) {
        uint32_t pixel = 0;
        std::memcpy(&pixel, data + i * bytesPerPixel, bytesPerPixel);

        uint32_t r = channel(pixel, masks[0], 0);
        uint32_t g = layout.luminance ? r : channel(pixel, masks[1], 0);
        uint32_t b = layout.luminance ? r : channel(pixel, masks[2], 0);

        image.pixels[i] = argb(channel(pixel, masks[3], 255), r, g, b);
      }

      return true;
    }

    PixelLayout headerLayout(const uint8_t* header, uint32_t flags) {
      PixelLayout layout;
      layout.bits      = read32(header + BitCountOffset);
      layout.luminance = (flags & FlagLuminance) != 0;

      for (uint32_t i = 0; i < 4; i++)
        layout.masks[i] = read32(header + MasksOffset + 4 * i);

      if (!(flags & (FlagAlphaPixels | FlagAlphaOnly)))
        layout.masks[3] = 0;

      return layout;
    }

    bool isBlockFormat(uint32_t format) {
      return format == fourCc('D', 'X', 'T', '1') || format == fourCc('D', 'X', 'T', '2')
          || format == fourCc('D', 'X', 'T', '3') || format == fourCc('D', 'X', 'T', '4')
          || format == fourCc('D', 'X', 'T', '5');
    }

    // DXT2 and DXT4 are DXT3 and DXT5 with colours multiplied by alpha
    bool isPremultiplied(uint32_t format) {
      return format == fourCc('D', 'X', 'T', '2') || format == fourCc('D', 'X', 'T', '4');
    }

    void unpremultiply(VrImage& image) {
      for (uint32_t& pixel : image.pixels) {
        uint32_t a = pixel >> 24;

        if (!a)
          continue;

        uint32_t c[3] = { (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF };

        for (uint32_t& value : c)
          value = std::min<uint32_t>(255, (value * 255 + a / 2) / a);

        pixel = argb(a, c[0], c[1], c[2]);
      }
    }

    // A D3DFORMAT four-character code as the channel masks it stands for
    bool legacyFormat(uint32_t format, PixelLayout& layout) {
      switch (format) {
        case D3dR8G8B8:      layout = { 24, { 0xFF0000u, 0xFF00u, 0xFFu, 0u }, false }; return true;
        case D3dA8R8G8B8:    layout = { 32, { 0xFF0000u, 0xFF00u, 0xFFu, 0xFF000000u }, false }; return true;
        case D3dX8R8G8B8:    layout = { 32, { 0xFF0000u, 0xFF00u, 0xFFu, 0u }, false }; return true;
        case D3dR5G6B5:      layout = { 16, { 0xF800u, 0x07E0u, 0x001Fu, 0u }, false }; return true;
        case D3dX1R5G5B5:    layout = { 16, { 0x7C00u, 0x03E0u, 0x001Fu, 0u }, false }; return true;
        case D3dA1R5G5B5:    layout = { 16, { 0x7C00u, 0x03E0u, 0x001Fu, 0x8000u }, false }; return true;
        case D3dA4R4G4B4:    layout = { 16, { 0x0F00u, 0x00F0u, 0x000Fu, 0xF000u }, false }; return true;
        case D3dX4R4G4B4:    layout = { 16, { 0x0F00u, 0x00F0u, 0x000Fu, 0u }, false }; return true;
        case D3dA8:          layout = { 8,  { 0u, 0u, 0u, 0xFFu }, false }; return true;
        case D3dA2B10G10R10: layout = { 32, { 0x3FFu, 0xFFC00u, 0x3FF00000u, 0xC0000000u }, false }; return true;
        case D3dA8B8G8R8:    layout = { 32, { 0xFFu, 0xFF00u, 0xFF0000u, 0xFF000000u }, false }; return true;
        case D3dX8B8G8R8:    layout = { 32, { 0xFFu, 0xFF00u, 0xFF0000u, 0u }, false }; return true;
        case D3dA2R10G10B10: layout = { 32, { 0x3FF00000u, 0xFFC00u, 0x3FFu, 0xC0000000u }, false }; return true;
        case D3dL8:          layout = { 8,  { 0xFFu, 0u, 0u, 0u }, true }; return true;
        case D3dA8L8:        layout = { 16, { 0xFFu, 0u, 0u, 0xFF00u }, true }; return true;
        case D3dA4L4:        layout = { 8,  { 0x0Fu, 0u, 0u, 0xF0u }, true }; return true;
        default:             return false;
      }
    }

    // The DXGI formats of the DX10 extension this reads, as the legacy
    // header would describe them: a DXT four-character code or a layout
    bool dxgiFormat(uint32_t format, uint32_t& blocks, PixelLayout& layout) {
      blocks = 0;

      switch (format) {
        case DxgiBc1: case DxgiBc1Srgb: blocks = fourCc('D', 'X', 'T', '1'); return true;
        case DxgiBc2: case DxgiBc2Srgb: blocks = fourCc('D', 'X', 'T', '3'); return true;
        case DxgiBc3: case DxgiBc3Srgb: blocks = fourCc('D', 'X', 'T', '5'); return true;

        case DxgiR8G8B8A8: case DxgiR8G8B8A8Srgb:
          layout = { 32, { 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u }, false };
          return true;

        case DxgiB8G8R8A8: case DxgiB8G8R8A8Srgb:
          layout = { 32, { 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u }, false };
          return true;

        case DxgiB8G8R8X8: case DxgiB8G8R8X8Srgb:
          layout = { 32, { 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0u }, false };
          return true;

        case DxgiR10G10B10A2:
          layout = { 32, { 0x3FFu, 0xFFC00u, 0x3FF00000u, 0xC0000000u }, false };
          return true;

        case DxgiB5G6R5:
          layout = { 16, { 0xF800u, 0x07E0u, 0x001Fu, 0u }, false };
          return true;

        case DxgiB5G5R5A1:
          layout = { 16, { 0x7C00u, 0x03E0u, 0x001Fu, 0x8000u }, false };
          return true;

        case DxgiB4G4R4A4:
          layout = { 16, { 0x0F00u, 0x00F0u, 0x000Fu, 0xF000u }, false };
          return true;

        case DxgiA8:
          layout = { 8, { 0u, 0u, 0u, 0xFFu }, false };
          return true;

        default:
          return false;
      }
    }

  }


  bool vrDecodeDds(const uint8_t* data, size_t size, VrImage& image) {
    image = VrImage();

    if (!data || size < HeaderSize || read32(data) != Magic)
      return false;

    uint32_t width  = read32(data + WidthOffset);
    uint32_t height = read32(data + HeightOffset);

    if (!width || !height || width > MaxExtent || height > MaxExtent)
      return false;

    image.width  = width;
    image.height = height;
    image.pixels.assign(size_t(width) * height, 0u);

    uint32_t flags  = read32(data + FormatFlagsOffset);
    uint32_t format = read32(data + FourCcOffset);

    const uint8_t* pixels = data + HeaderSize;
    size_t         left   = size - HeaderSize;

    bool decoded = false;

    PixelLayout layout = headerLayout(data, flags);
    uint32_t    blocks = 0;

    // Some exporters fill in the four-character code without its flag
    if ((flags & FlagFourCc) || (!layout.bits && isBlockFormat(format))) {
      if (isBlockFormat(format))
        blocks = format;
      else if (format == fourCc('D', 'X', '1', '0') && left >= Dx10HeaderSize
            && dxgiFormat(read32(pixels), blocks, layout)) {
        pixels += Dx10HeaderSize;
        left   -= Dx10HeaderSize;
      } else if (!legacyFormat(format, layout))
        layout.bits = 0;
    }

    if (blocks) {
      decoded = decodeBlocks(pixels, left, blocks, image);

      if (decoded && isPremultiplied(blocks))
        unpremultiply(image);
    } else if (layout.bits)
      decoded = decodePixels(pixels, left, layout, image);

    if (!decoded)
      image = VrImage();

    return decoded;
  }



  std::string vrDescribeDds(const uint8_t* data, size_t size) {
    std::ostringstream text;

    if (!data || size < HeaderSize || read32(data) != Magic) {
      text << size << " bytes, not a DDS file";
      return text.str();
    }

    uint32_t format = read32(data + FourCcOffset);

    text << read32(data + WidthOffset) << "x" << read32(data + HeightOffset) << ", " << size
         << " bytes, pixel format flags 0x" << std::hex << read32(data + FormatFlagsOffset) << std::dec;

    if (format) {
      std::string code(reinterpret_cast<const char*>(data + FourCcOffset), 4);
      bool printable = std::all_of(code.begin(), code.end(), [] (char c) { return c >= 0x20 && c < 0x7F; });

      if (printable)
        text << ", four-character code '" << code << "'";
      else
        text << ", four-character code " << format;

      if (format == fourCc('D', 'X', '1', '0') && size >= HeaderSize + Dx10HeaderSize)
        text << ", DXGI format " << read32(data + HeaderSize);
    }

    text << ", " << read32(data + BitCountOffset) << " bits, masks" << std::hex;

    for (uint32_t i = 0; i < 4; i++)
      text << " 0x" << read32(data + MasksOffset + 4 * i);

    return text.str();
  }

}
