#include "test.h"

#include "../vr_tips.h"
#include "../vr_tutorial_text.h"

using namespace dxvk;

namespace {

  VrTip run(VrTipQueue& queue, const VrTipInput& input, float seconds) {
    constexpr float Step = 0.1f;

    for (float time = 0.0f; time < seconds; time += Step) {
      VrTip tip = queue.update(input, Step);

      if (tip != VrTip::None)
        return tip;
    }

    return VrTip::None;
  }

}


TEST_CASE(tips_wait_for_the_situation_to_last) {
  VrTipQueue queue;
  VrTipInput input;
  input.playing = true;

  CHECK(run(queue, input, VrTipQueue::WaitTime - 0.5f) == VrTip::None);

  // A menu in between starts the wait again
  CHECK(run(queue, VrTipInput(), 1.0f) == VrTip::None);
  CHECK(run(queue, input, VrTipQueue::WaitTime - 0.5f) == VrTip::None);
  CHECK(run(queue, input, 1.0f) == VrTip::Welcome);
}


TEST_CASE(tips_come_after_the_welcome_and_only_once) {
  VrTipQueue queue;
  VrTipInput input;
  input.playing     = true;
  input.meleeWeapon = true;

  CHECK(run(queue, input, 3.0f) == VrTip::Welcome);
  CHECK(run(queue, input, 3.0f) == VrTip::MeleeWeapon);
  CHECK(run(queue, input, 3.0f) == VrTip::None);

  input.meleeWeapon = false;
  input.firearm     = true;
  CHECK(run(queue, input, 3.0f) == VrTip::Firearm);
  CHECK(run(queue, input, 3.0f) == VrTip::None);
}


TEST_CASE(tips_of_weapons_wait_for_the_player_to_be_free) {
  VrTipQueue queue;
  queue.parseSeen("welcome\n");

  VrTipInput input;
  input.firearm = true;

  CHECK(run(queue, input, 3.0f) == VrTip::None);

  input.playing = true;
  CHECK(run(queue, input, 3.0f) == VrTip::Firearm);
}


TEST_CASE(tips_of_minigames_need_no_welcome) {
  VrTipQueue queue;
  VrTipInput input;
  input.minigame = VrTip::Roulette;

  CHECK(run(queue, input, 3.0f) == VrTip::Roulette);
  CHECK(run(queue, input, 3.0f) == VrTip::None);
  CHECK(!queue.seen(VrTip::Welcome));
}


TEST_CASE(tips_seen_are_kept_in_text) {
  VrTipQueue queue;
  queue.parseSeen("welcome\r\nblackjack\nunknown\n");

  CHECK(queue.seen(VrTip::Welcome));
  CHECK(queue.seen(VrTip::BlackJack));
  CHECK(!queue.seen(VrTip::Firearm));
  CHECK(queue.formatSeen() == "welcome\nblackjack\n");

  VrTipQueue other;
  other.parseSeen(queue.formatSeen());
  CHECK(other.seen(VrTip::BlackJack));
}


TEST_CASE(tutorial_text_reads_tips) {
  VrTutorialTexts texts;

  texts.parse(
    "[tip welcome]\n"
    "The title\n"
    "\n"
    "First line\n"
    "Second line\n"
    "[tip empty]\n"
    "Only a title\n");

  const VrTipText* tip = texts.tip("welcome");
  CHECK(tip != nullptr);
  CHECK(tip->title == "The title");
  CHECK(tip->body == "First line\r\nSecond line");
  CHECK(texts.tip("empty") == nullptr);
  CHECK(texts.tip("other") == nullptr);
}
