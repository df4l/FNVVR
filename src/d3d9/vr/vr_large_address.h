#pragma once

#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief Offers to give the game 4 GB of address space, or to load NVSE
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
   * Under Proton every executable gets 4 GB, but NVSE then has to be
   * started by nvse_loader.exe. So when the process already has 4 GB, the
   * executable is an unmodified Steam one and nvse_loader.exe and
   * nvse_steam_loader.dll are in the game directory, the same patch is
   * offered for what it also does: load nvse_steam_loader.dll, hence NVSE,
   * at startup. Its "don't ask again" file is \c fnvvr\\no_nvse_prompt.txt.
   *
   * See findings/laa.md. Only used on the game's main thread.
   */
  class VrLargeAddress {

  public:

    /**
     * \brief Hooks the main menu if the prompt has to be shown
     *
     * \param [in] texts The prompt's texts, which must outlive the game's menus
     * \returns \c true if the main menu will show the prompt
     */
    static bool install(const VrTexts& texts);

  };

}
