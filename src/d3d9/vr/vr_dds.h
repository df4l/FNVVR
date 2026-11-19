#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dxvk {

  /**
   * \brief Image in system memory
   *
   * Rows from the top, each pixel 0xAARRGGBB with straight alpha.
   */
  struct VrImage {
    uint32_t              width  = 0;
    uint32_t              height = 0;
    std::vector<uint32_t> pixels;

    uint32_t at(uint32_t x, uint32_t y) const {
      return pixels[size_t(y) * width + x];
    }
  };

  /**
   * \brief Decodes the top level of a DDS file
   *
   * Reads the formats the game's textures use: DXT1 to DXT5, and
   * uncompressed 8, 16, 24 and 32 bit pixels described by their channel
   * masks (such as the A4R4G4B4 of the game's font textures) or by a
   * D3DFORMAT number in place of the four-character code, luminance and
   * alpha-only images included. Textures saved by newer tools with the
   * DX10 extension are read in the same encodings (BC1 to BC3, RGBA,
   * BGRA, 5-6-5, 5-5-5-1, 4-4-4-4, 10-10-10-2, A8). Not read: BC4 to BC7
   * and floating point pixels, which carry no alpha a decal could use or
   * which D3D9 cannot load. Mipmaps, cube maps and volume textures beyond
   * the first image are ignored.
   *
   * \param [in] data The file's content
   * \param [in] size Size of the content in bytes
   * \param [out] image The decoded image
   * \returns \c false if the file is not a DDS file in one of these formats
   */
  bool vrDecodeDds(const uint8_t* data, size_t size, VrImage& image);

  /**
   * \brief Describes a DDS file's header for the log
   *
   * Size, pixel format flags, four-character code (and DXGI format),
   * bit count and channel masks: what tells why a file was not decoded.
   * \param [in] data The file's content
   * \param [in] size Size of the content in bytes
   * \returns One line of text
   */
  std::string vrDescribeDds(const uint8_t* data, size_t size);

}
