#pragma once

#include <cstdint>
#include <vector>

namespace dxvk {

  /**
   * \brief Reads the game's data files
   *
   * Files are looked up as the game's texture loader looks them up: in
   * the loaded archives, which the game decompresses, then loose in the
   * Data folder.
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

    /**
     * \brief Logs why a file \ref read did not find may be missing
     *
     * What each of the game's lookups found, whether a plugin replaced
     * them, and every loaded archive with the file's folder and the
     * file's raw entry there. See findings/optics.md §15.
     *
     * \param [in] path Path of the file, as given to \ref read
     * \param [in] archiveType The game's archive type looked in
     */
    static void explainMissing(const char* path, uint32_t archiveType);

  };

}
