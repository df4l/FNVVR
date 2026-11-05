#pragma once

#include <string>

#include "vr_options.h"

namespace dxvk {

  /**
   * \brief The options file written by the VR options page
   *
   * \c fnvvr\\vr_options.ini in the folder that holds d3d9.dll. It only
   * exists once the player changed an option.
   */
  class VrOptionsFile {

  public:

    /**
     * \brief Updates the options with the values the file holds
     *
     * \param [in,out] options Options to update. Left alone if the file
     *    does not exist or cannot be read.
     * \returns \c true if the file was read
     */
    static bool load(VrOptions& options);

    /**
     * \brief Writes the options, creating the folder if needed
     *
     * \param [in] options Options to write
     * \returns \c true if the file was written
     */
    static bool save(const VrOptions& options);

    /**
     * \brief Writes a text file of the options' folder, creating it if needed
     *
     * \param [in] name The file's name
     * \param [in] text The file's text
     * \returns \c true if the file was written
     */
    static bool writeText(const wchar_t* name, const std::string& text);

    /**
     * \brief Reads a text file of the options' folder
     *
     * \param [in] name The file's name
     * \param [out] text The file's text
     * \returns \c false if there is no such file
     */
    static bool readText(const wchar_t* name, std::string& text);

  };

}
