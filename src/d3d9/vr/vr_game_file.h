#pragma once

#include <cstdint>
#include <vector>

namespace dxvk {

  /**
   * \brief Reads the game's data files
   *
   * Files are looked up as the game looks them up: loose in the Data
   * folder, then in the loaded archives, which the game decompresses.
   * Paths are relative to the game's folder, such as
   * "Data\\Textures\\Terminals\\terminal01.dds". See findings/console.md §7.
   *
   * Only used on the game's main thread, once the game has loaded its
   * archives.
   */
  class VrGameFile {

  public:

    /**
     * \brief Reads a whole file
     *
     * \param [in] path Path of the file
     * \param [in] archiveType The game's archive type to look in
     * \param [out] data The file's content
     * \returns \c false if the file was not found or could not be read,
     *    or if the executable is not the supported version
     */
    static bool read(const char* path, uint32_t archiveType, std::vector<uint8_t>& data);

  };

}
