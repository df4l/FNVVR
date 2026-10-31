#include <cctype>
#include <cstring>

#include "vr_large_address_patch.h"

namespace dxvk {

  namespace {

    struct KnownExecutable {
      const char*                 sha1;
      VrExecutableIdentity::State state;
      VrSteamExecutable           version;
    };

    // The FNV 4GB Patcher's table. Two builds of each language exist; the
    // GOG executable is large address aware from the start.
    constexpr KnownExecutable KnownExecutables[] = {
      { "D068F394521A67C6E74FE572F59BD1BE71E855F3", VrExecutableIdentity::State::Original, VrSteamExecutable::English },
      { "07AFFDA66C89F09B0876A50C77759640BC416673", VrExecutableIdentity::State::Original, VrSteamExecutable::English },
      { "3980940522F0264ED9AF14AEA1773BB19F5160AB", VrExecutableIdentity::State::Original, VrSteamExecutable::German  },
      { "F65049B0957D83E61ECCCACC730015AE77FB4C8B", VrExecutableIdentity::State::Original, VrSteamExecutable::German  },
      { "5394B94A18FFA6FA846E1D6033AD7F81919F13AC", VrExecutableIdentity::State::Original, VrSteamExecutable::Russian },
      { "ACA83D5A12A64AF8854E381752FE989692D46E04", VrExecutableIdentity::State::Original, VrSteamExecutable::Russian },
      { "946D2EABA04A75FF361B8617C7632B49F1EDE9D3", VrExecutableIdentity::State::Patched,  VrSteamExecutable::English },
      { "0021023E37B1AF143305A61B7B29A1811CC7C5FB", VrExecutableIdentity::State::Patched,  VrSteamExecutable::English },
      { "37CAE4E713B6B182311F66E31668D5005D1B9F5B", VrExecutableIdentity::State::Patched,  VrSteamExecutable::English },
      { "600CD576CDE7746FB2CD152FDD24DB97453ED135", VrExecutableIdentity::State::Patched,  VrSteamExecutable::English },
      { "34B65096CAEF9374DD6AA39AF855E43308B417F2", VrExecutableIdentity::State::Patched,  VrSteamExecutable::English },
    };

    struct PatchBytes {
      uint32_t       offset;
      const uint8_t* bytes;
      uint32_t       size;
    };

    /// COFF characteristics, low byte: IMAGE_FILE_LARGE_ADDRESS_AWARE added
    constexpr uint8_t Characteristics[] = { 0x22 };

    /// AddressOfEntryPoint, low three bytes: RVA 0x0107A620 (EntryPoint below)
    constexpr uint8_t EntryPointRva[] = { 0x20, 0xA6, 0x07 };

    /// CheckSum, low three bytes, per language
    constexpr uint8_t CheckSums[][3] = {
      { 0x3E, 0xF9, 0xFC }, { 0x88, 0xC4, 0xFC }, { 0x15, 0x43, 0xFD },
    };

    /// In SteamStub, "call [ebp-0x3F4]" (the decrypted payload's check)
    /// becomes a call to DisableCheck
    constexpr uint8_t CallDisableCheck[] = { 0xE8, 0x04, 0xFD, 0x06, 0x00, 0x90 };

    /// In SteamStub, the register restore before the jump to the game's
    /// entry point becomes a jump to LoadNvse
    constexpr uint8_t JumpLoadNvse[] = { 0xE9, 0x56, 0xFC, 0x06, 0x00 };

    /// DisableCheck: VirtualProtect, payload + 0x20C = 0x74 (jz), protection
    /// restored, then the payload is entered. LoadNvse, at +0x60:
    /// LoadLibraryA("nvse_steam_loader.dll"), SteamStub's register restore,
    /// jmp eax. The name follows at +0x80.
    constexpr uint8_t DisableCheckAndLoadNvse[] = {
      0x90, 0x50, 0x50, 0x8B, 0xC4, 0x50, 0xB8, 0x40, 0x00, 0x00, 0x00, 0x50, 0xB8, 0x04, 0x00, 0x00,
      0x00, 0x50, 0x8B, 0x85, 0x0C, 0xFC, 0xFF, 0xFF, 0x05, 0x0C, 0x02, 0x00, 0x00, 0x50, 0xFF, 0x55,
      0x9C, 0x8B, 0x85, 0x0C, 0xFC, 0xFF, 0xFF, 0x05, 0x0C, 0x02, 0x00, 0x00, 0xC6, 0x00, 0x74, 0x8B,
      0xC4, 0x50, 0x8B, 0x44, 0x24, 0x04, 0x50, 0xB8, 0x04, 0x00, 0x00, 0x00, 0x50, 0x8B, 0x85, 0x0C,
      0xFC, 0xFF, 0xFF, 0x05, 0x0C, 0x02, 0x00, 0x00, 0x50, 0xFF, 0x55, 0x9C, 0x58, 0x58, 0xFF, 0xA5,
      0x0C, 0xFC, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x50, 0x68, 0x00, 0xA6, 0x47, 0x01, 0xFF, 0x15, 0xB0, 0xF0, 0xFD, 0x00, 0x58, 0x5D, 0x5F, 0x5E,
      0x5A, 0x59, 0x5B, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x6E, 0x76, 0x73, 0x65, 0x5F, 0x73, 0x74, 0x65, 0x61, 0x6D, 0x5F, 0x6C, 0x6F, 0x61, 0x64, 0x65,
      0x72, 0x2E, 0x64, 0x6C, 0x6C,
    };

    /// EntryPoint: pushad, SetEnvironmentVariableA("SteamGameId", id),
    /// SetEnvironmentVariableA("SteamAppId", id), popad, jump to SteamStub
    constexpr uint8_t EntryPoint[] = {
      0x60, 0x68, 0xC8, 0xA6, 0x47, 0x01, 0x68, 0xE0, 0xA6, 0x47, 0x01, 0xFF, 0x15, 0xF4, 0xF1, 0xFD,
      0x00, 0x68, 0xC8, 0xA6, 0x47, 0x01, 0x68, 0xD0, 0xA6, 0x47, 0x01, 0xFF, 0x15, 0xF4, 0xF1, 0xFD,
      0x00, 0x61, 0xE9, 0xA7, 0xEC, 0xF8, 0xFF,
    };

    /// First four digits of the app id, per language: 22380 or 22490
    constexpr uint8_t AppIdDigits[][4] = {
      { '2', '2', '3', '8' }, { '2', '2', '3', '8' }, { '2', '2', '4', '9' },
    };

    /// Last digit of the app id, then the variable names
    constexpr uint8_t AppIdEndAndNames[] = {
      '0', 0x00, 0x00, 0x00, 'S', 't', 'e', 'a', 'm', 'A', 'p', 'p', 'I', 'd', 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 'S', 't', 'e', 'a', 'm', 'G', 'a', 'm', 'e', 'I', 'd',
    };

    bool sameDigest(const std::string& a, const char* b) {
      if (a.size() != std::strlen(b))
        return false;

      for (size_t i = 0; i < a.size(); i++) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != b[i])
          return false;
      }

      return true;
    }

  }


  VrExecutableIdentity vrIdentifyExecutable(const std::string& sha1) {
    for (const auto& known : KnownExecutables) {
      if (sameDigest(sha1, known.sha1)) {
        VrExecutableIdentity identity;
        identity.state   = known.state;
        identity.version = known.version;
        return identity;
      }
    }

    return VrExecutableIdentity();
  }


  bool vrApplyLargeAddressPatch(std::vector<uint8_t>& image, VrSteamExecutable version) {
    uint32_t index = uint32_t(version);

    const PatchBytes patches[] = {
      { 0x00000136, Characteristics,         sizeof(Characteristics)         },
      { 0x00000148, EntryPointRva,           sizeof(EntryPointRva)           },
      { 0x00000178, CheckSums[index],        sizeof(CheckSums[index])        },
      { 0x00F57277, CallDisableCheck,        sizeof(CallDisableCheck)        },
      { 0x00F57385, JumpLoadNvse,            sizeof(JumpLoadNvse)            },
      { 0x00FC6F80, DisableCheckAndLoadNvse, sizeof(DisableCheckAndLoadNvse) },
      { 0x00FC7020, EntryPoint,              sizeof(EntryPoint)              },
      { 0x00FC70C8, AppIdDigits[index],      sizeof(AppIdDigits[index])      },
      { 0x00FC70CC, AppIdEndAndNames,        sizeof(AppIdEndAndNames)        },
    };

    for (const auto& patch : patches) {
      if (patch.offset + patch.size > image.size())
        return false;
    }

    for (const auto& patch : patches)
      std::memcpy(&image[patch.offset], patch.bytes, patch.size);

    return true;
  }

}
