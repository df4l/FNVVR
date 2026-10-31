#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dxvk {

  /**
   * \brief Language versions of the Steam executable the patch knows
   *
   * They differ in their PE checksum and, for the Russian one, in the
   * Steam app id. See findings/laa.md.
   */
  enum class VrSteamExecutable : uint32_t {
    English = 0,
    German  = 1,
    Russian = 2,
  };

  /**
   * \brief What the SHA-1 of a FalloutNV.exe tells about it
   */
  struct VrExecutableIdentity {
    enum class State {
      Unknown,   ///< Not a version the patch knows
      Original,  ///< An unmodified Steam executable the patch applies to
      Patched,   ///< Already large address aware: patched by us or the FNV 4GB Patcher
    };

    State             state   = State::Unknown;
    VrSteamExecutable version = VrSteamExecutable::English;
  };

  /**
   * \brief Identifies a FalloutNV.exe by the SHA-1 of the whole file
   *
   * \param [in] sha1 Hexadecimal digest, upper or lower case
   */
  VrExecutableIdentity vrIdentifyExecutable(const std::string& sha1);

  /**
   * \brief Makes a Steam FalloutNV.exe large address aware
   *
   * Writes the same bytes as the FNV 4GB Patcher (Nexus Mods 62552), so
   * the result is the file that tool produces and that it, NVSE and other
   * tools recognise as patched: the large address aware flag, a new entry
   * point that sets SteamAppId and SteamGameId, a hook in SteamStub that
   * turns off its check of the modified file once the payload is decrypted,
   * and a load of \c nvse_steam_loader.dll (ignored if the file is absent)
   * before the game's own entry point. See findings/laa.md.
   *
   * \param [in,out] image Content of the original executable, which
   *    \c vrIdentifyExecutable reported as \c Original
   * \param [in] version Its language version
   * \returns \c false if the image is too small to be that executable,
   *    in which case it is left unchanged
   */
  bool vrApplyLargeAddressPatch(std::vector<uint8_t>& image, VrSteamExecutable version);

}
