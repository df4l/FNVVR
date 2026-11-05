#pragma once

namespace dxvk {

  /**
   * \brief Keeps the game's sounds audible without the window's focus
   *
   * Started from the headset, the game's window can stay in the background,
   * where DirectSound silences every buffer created without
   * DSBCAPS_GLOBALFOCUS. The player looks into the headset and has no reason
   * to bring the window forward. See findings/audio.md §4.
   */
  class VrSoundFocus {

  public:

    /**
     * \brief Adds the flag to the buffers the game creates from now on
     *
     * To be called before the game starts its audio. Logs if the executable
     * is not the supported version, which is then left as it is.
     */
    static void install();

    /**
     * \brief Keeps a plugin from silencing the game for its hidden window
     *
     * ITR's bAltTabMute, on by default, sets every volume to 0 while the
     * foreground window is not the game's, which is always the case once the
     * window is hidden. Only that plugin's import of GetForegroundWindow is
     * replaced, and nothing happens without it. See
     * findings/nvse-compat.md §5.7.
     */
    static void keepPluginSound();

  };

}
