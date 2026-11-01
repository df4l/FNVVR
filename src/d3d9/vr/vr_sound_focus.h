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

  };

}
