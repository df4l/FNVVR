#pragma once

namespace dxvk {

  /**
   * \brief Offers to give the game 4 GB of address space
   *
   * A 32-bit process gets 4 GB on 64-bit Windows only if its executable is
   * large address aware, which the kernel reads when it creates the process.
   * Without it the game has 2 GB, which VR and mods fill until it crashes.
   *
   * When the process is limited to 2 GB and FalloutNV.exe is an unmodified
   * Steam executable, the main menu shows a message box once its fade-in
   * ends. If the player accepts, FalloutNV.exe is patched as the FNV 4GB
   * Patcher does (\c vrApplyLargeAddressPatch), the original is kept as
   * FalloutNV_backup.exe, and a second message box asks for a restart. The
   * player can also decline for this run only, or for good, which writes
   * \c fnvvr\\no_large_address_prompt.txt next to d3d9.dll.
   *
   * Under Proton every executable gets 4 GB, so nothing is shown there.
   * See findings/laa.md. Only used on the game's main thread.
   */
  class VrLargeAddress {

  public:

    /**
     * \brief Hooks the main menu if the prompt has to be shown
     *
     * \returns \c true if the main menu will show the prompt
     */
    static bool install();

  };

}
