#pragma once

namespace dxvk {

  /**
   * \brief Replaces game settings that cost frame time or cause stutter
   *
   * The values come from the Viva New Vegas guide's configuration file. They
   * are written in memory on the first D3D9 interface: the game has read its
   * INI files by then, and has not read these settings yet. Nothing writes
   * them to Fallout.ini; the game itself may save the renderer settings it
   * holds into FalloutPrefs.ini. See findings/game-settings.md.
   */
  class VrGameSettings {

  public:

    /**
     * \brief Writes the settings
     *
     * Each setting is checked against the name the game gave it, and left
     * alone if it differs, so another executable version is not touched.
     */
    static void apply();

    /**
     * \brief Keeps the game running while its window is not active
     *
     * A deactivated window otherwise stops the game's timer. Same check of
     * the setting's name as \ref apply.
     */
    static void keepActive();

  private:

    /**
     * \brief Lets the game end its audio thread when it exits
     *
     * Without this, the game waits for that thread forever when it quits
     * with bMultiThreadAudio on. See findings/game-settings.md.
     *
     * \returns \c true if the game's code was the expected one and is fixed
     */
    static bool fixAudioThreadExit();

  };

}
