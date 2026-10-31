#include "test.h"

#include "../vr_large_address_patch.h"

using namespace dxvk;

namespace {

  // Covers every offset the patch writes
  constexpr size_t ImageSize = 0x00FC7100;

}


TEST_CASE(large_address_identifies_steam_executables_in_any_case) {
  VrExecutableIdentity english = vrIdentifyExecutable("d068f394521a67c6e74fe572f59bd1be71e855f3");
  CHECK(english.state == VrExecutableIdentity::State::Original);
  CHECK(english.version == VrSteamExecutable::English);

  VrExecutableIdentity russian = vrIdentifyExecutable("ACA83D5A12A64AF8854E381752FE989692D46E04");
  CHECK(russian.state == VrExecutableIdentity::State::Original);
  CHECK(russian.version == VrSteamExecutable::Russian);
}


TEST_CASE(large_address_recognises_patched_and_gog_executables) {
  CHECK(vrIdentifyExecutable("0021023E37B1AF143305A61B7B29A1811CC7C5FB").state
    == VrExecutableIdentity::State::Patched);
  CHECK(vrIdentifyExecutable("946d2eaba04a75ff361b8617c7632b49f1ede9d3").state
    == VrExecutableIdentity::State::Patched);
}


TEST_CASE(large_address_does_not_know_other_executables) {
  CHECK(vrIdentifyExecutable("").state == VrExecutableIdentity::State::Unknown);
  CHECK(vrIdentifyExecutable("D068F394521A67C6E74FE572F59BD1BE71E855F").state
    == VrExecutableIdentity::State::Unknown);
  CHECK(vrIdentifyExecutable("0000000000000000000000000000000000000000").state
    == VrExecutableIdentity::State::Unknown);
}


TEST_CASE(large_address_patch_sets_the_flag_and_the_entry_point) {
  std::vector<uint8_t> image(ImageSize, 0x00);
  image[0x136] = 0x02;

  CHECK(vrApplyLargeAddressPatch(image, VrSteamExecutable::English));
  CHECK(image[0x136] == 0x22);
  CHECK(image[0x148] == 0x20 && image[0x149] == 0xA6 && image[0x14A] == 0x07);
  CHECK(image[0x178] == 0x3E && image[0x179] == 0xF9 && image[0x17A] == 0xFC);
  CHECK(image[0xF57277] == 0xE8);
  CHECK(image[0xFC7020] == 0x60);
}


TEST_CASE(large_address_patch_writes_the_app_id_of_the_language) {
  std::vector<uint8_t> english(ImageSize, 0x00);
  std::vector<uint8_t> russian(ImageSize, 0x00);

  CHECK(vrApplyLargeAddressPatch(english, VrSteamExecutable::English));
  CHECK(vrApplyLargeAddressPatch(russian, VrSteamExecutable::Russian));

  CHECK(std::string(reinterpret_cast<const char*>(&english[0xFC70C8])) == "22380");
  CHECK(std::string(reinterpret_cast<const char*>(&russian[0xFC70C8])) == "22490");
  CHECK(std::string(reinterpret_cast<const char*>(&english[0xFC70E0])) == "SteamGameId");
  CHECK(std::string(reinterpret_cast<const char*>(&english[0xFC7000])) == "nvse_steam_loader.dll");
}


TEST_CASE(large_address_patch_leaves_a_short_image_alone) {
  std::vector<uint8_t> image(0x00FC7000, 0x00);

  CHECK(!vrApplyLargeAddressPatch(image, VrSteamExecutable::English));
  CHECK(image[0x136] == 0x00);
}
