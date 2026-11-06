#include "test.h"

#include "../vr_terminal_keyboard.h"
#include "../vr_text_entry.h"

using namespace dxvk;

namespace {

  constexpr float Frame = 1.0f / 90.0f;

  /// Runs frames with the field open and nothing happening until a step
  /// other than None comes, at most \c seconds
  VrTextEntryStep waitForStep(VrTextEntry& entry, float seconds) {
    for (float t = 0.0f; t < seconds; t += Frame) {
      VrTextEntryStep step = entry.update(true, false, VrKeyboardEvent::None, Frame);

      if (step != VrTextEntryStep::None)
        return step;
    }

    return VrTextEntryStep::None;
  }

}


TEST_CASE(text_entry_converts_typed_text_to_the_game_encoding) {
  CHECK(vrGameTextFromUtf8("Courier") == "Courier");
  // é, € and Œ have Windows-1252 bytes
  CHECK(vrGameTextFromUtf8("Ren\xC3\xA9 \xE2\x82\xAC\xC5\x92") == "Ren\xE9 \x80\x8C");
  // A character the game cannot hold, the cursor marker and controls are left out
  CHECK(vrGameTextFromUtf8("A\xE2\x98\x83" "B|C\n\tD") == "ABCD");
  // A malformed sequence does not swallow the next character
  CHECK(vrGameTextFromUtf8("\xC3" "A") == "A");
}


TEST_CASE(text_entry_converts_game_text_to_utf8) {
  CHECK(vrUtf8FromGameText("Ren\xE9") == "Ren\xC3\xA9");
  CHECK(vrUtf8FromGameText("\x80") == "\xE2\x82\xAC");
  // An unassigned byte is left out
  CHECK(vrUtf8FromGameText("A\x81" "B") == "AB");
}


TEST_CASE(text_entry_keys_replace_and_confirm_the_text) {
  std::vector<uint32_t> keys = vrTextEntryKeys(2, "Bo\xE9");

  std::vector<uint32_t> expected = {
    VrConsoleKey::End, VrConsoleKey::Backspace, VrConsoleKey::Backspace,
    'B', 'o', 0xE9, VrConsoleKey::Enter,
  };

  CHECK(keys == expected);
}


TEST_CASE(text_entry_shows_the_keyboard_after_the_field_opens) {
  VrTextEntry entry;

  CHECK(entry.update(false, false, VrKeyboardEvent::None, 1.0f) == VrTextEntryStep::None);
  CHECK(entry.update(true, false, VrKeyboardEvent::None, Frame) == VrTextEntryStep::None);
  CHECK(waitForStep(entry, VrTextEntry::OpenDelay + 0.1f) == VrTextEntryStep::Show);
  CHECK(entry.shown());

  // The field closes: the keyboard goes
  CHECK(entry.update(false, false, VrKeyboardEvent::None, Frame) == VrTextEntryStep::Hide);
  CHECK(!entry.shown());
  CHECK(entry.update(false, false, VrKeyboardEvent::None, Frame) == VrTextEntryStep::None);
}


TEST_CASE(text_entry_applies_the_text_and_reopens_if_the_game_keeps_the_field) {
  VrTextEntry entry;
  CHECK(waitForStep(entry, 1.0f) == VrTextEntryStep::Show);

  CHECK(entry.update(true, false, VrKeyboardEvent::Done, Frame) == VrTextEntryStep::Apply);
  CHECK(!entry.shown());

  // The keys are typed: nothing else happens meanwhile
  for (int i = 0; i < 100; i++)
    CHECK(entry.update(true, true, VrKeyboardEvent::None, Frame) == VrTextEntryStep::None);

  // The game refused the text: the keyboard comes back
  CHECK(waitForStep(entry, VrTextEntry::OpenDelay + 0.1f) == VrTextEntryStep::Show);
}


TEST_CASE(text_entry_reopens_a_keyboard_closed_without_its_text) {
  VrTextEntry entry;
  CHECK(waitForStep(entry, 1.0f) == VrTextEntryStep::Show);

  CHECK(entry.update(true, false, VrKeyboardEvent::Closed, Frame) == VrTextEntryStep::None);
  CHECK(!entry.shown());
  CHECK(waitForStep(entry, VrTextEntry::ReopenDelay - 0.1f) == VrTextEntryStep::None);
  CHECK(waitForStep(entry, 0.2f) == VrTextEntryStep::Show);
}


TEST_CASE(text_entry_does_not_ask_again_for_a_refused_keyboard) {
  VrTextEntry entry;
  CHECK(waitForStep(entry, 1.0f) == VrTextEntryStep::Show);

  entry.refuse();
  CHECK(!entry.shown());
  CHECK(waitForStep(entry, 5.0f) == VrTextEntryStep::None);

  // Until the next field
  CHECK(entry.update(false, false, VrKeyboardEvent::None, Frame) == VrTextEntryStep::None);
  CHECK(waitForStep(entry, 1.0f) == VrTextEntryStep::Show);
}
