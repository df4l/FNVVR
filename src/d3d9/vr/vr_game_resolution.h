#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Makes the game render at a resolution chosen for the headset
   *
   * The game takes its resolution from the iSize W and iSize H display
   * settings (FalloutPrefs.ini), which the launcher limits to the monitor's
   * modes. The renderer setup copies them once, after the first D3D9
   * interface was created, so they are replaced in memory before that copy.
   * Windowed mode is forced, since a fullscreen game only accepts the
   * monitor's modes. See findings/resolution.md.
   *
   * The size settings are put back once the device exists, so that the game
   * never saves the headset resolution into the player's settings file. The
   * fullscreen setting stays off for the session, because the game reads it
   * while it runs.
   */
  class VrGameResolution {

  public:

    /**
     * \brief Replaces the resolution and turns fullscreen off
     *
     * \param [in] size Resolution to render at
     * \returns \c false if the executable is not the supported version
     */
    bool apply(const VrExtent& size);

    /**
     * \brief Puts back the size settings the player chose
     *
     * Does nothing if \c apply did not change them.
     */
    void restoreSettings();

  private:

    bool     m_applied = false;
    int32_t  m_width   = 0;
    int32_t  m_height  = 0;

  };

}
