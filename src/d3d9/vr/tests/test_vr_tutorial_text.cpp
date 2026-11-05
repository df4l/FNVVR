#include "test.h"

#include "../vr_tutorial_text.h"

using namespace dxvk;

TEST_CASE(tutorial_text_reads_messages_and_controls) {
  VrTutorialTexts texts;

  texts.parse(
    "# A comment\n"
    "[control Vats]\n"
    "the right stick\n"
    "\n"
    "[message 0x183]\n"
    "First line\r\n"
    "\r\n"
    "# Not part of the text\n"
    "Second line\n"
    "\n");

  CHECK(texts.controlCount() == 1);
  CHECK(texts.messageCount() == 1);
  CHECK(*texts.control(16) == "the right stick");
  CHECK(*texts.message(0x183) == "First line\r\n\r\nSecond line");
  CHECK(texts.control(4) == nullptr);
  CHECK(texts.message(0x184) == nullptr);
}


TEST_CASE(tutorial_text_keeps_what_a_second_file_does_not_hold) {
  VrTutorialTexts texts;

  texts.parse("[message 1]\nEnglish one\n[message 2]\nEnglish two\n");
  texts.parse("\xEF\xBB\xBF[message 2]\nFrench two\n");

  CHECK(*texts.message(1) == "English one");
  CHECK(*texts.message(2) == "French two");
}


TEST_CASE(tutorial_text_ignores_what_it_does_not_know) {
  VrTutorialTexts texts;

  texts.parse(
    "Text before any section\n"
    "[control Nothing]\nA name\n"
    "[message nope]\nA text\n"
    "[other 1]\nA text\n"
    "[message 5]\n\n"
    "[control Use]\n" + std::string(VrTutorialTexts::MaxControlName + 1, 'x') + "\n");

  CHECK(texts.messageCount() == 0);
  CHECK(texts.controlCount() == 0);
}


TEST_CASE(tutorial_text_source_shares_the_gamepad_text) {
  CHECK(vrTutorialTextSource(0x17D) == 0x183);
  CHECK(vrTutorialTextSource(0x183) == 0x183);
  CHECK(vrTutorialTextSource(0x12345) == 0x12345);
}


TEST_CASE(tutorial_text_converts_to_the_game_encoding) {
  CHECK(vrToGameEncoding("Plain") == "Plain");
  CHECK(vrToGameEncoding("\xC3\xA9t\xC3\xA9") == "\xE9t\xE9");
  CHECK(vrToGameEncoding("c\xC5\x93ur \xE2\x82\xAC") == "c\x9Cur \x80");
  CHECK(vrToGameEncoding("\xE2\x86\x92") == "?");
  CHECK(vrToGameEncoding("\xF0\x9F\x98\x80!") == "?!");
  CHECK(vrToGameEncoding("end\xC3") == "end?");
  CHECK(vrToGameEncoding("\xFF") == "?");
}
