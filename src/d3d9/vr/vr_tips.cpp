#include <sstream>

#include "vr_tips.h"

namespace dxvk {

  namespace {

    constexpr const char* TipNames[] = {
      "welcome",
      "melee_weapon",
      "firearm",
      "slot_machine",
      "blackjack",
      "roulette",
    };

  }


  const char* vrTipName(VrTip tip) {
    return tip < VrTip::Count ? TipNames[uint32_t(tip)] : nullptr;
  }


  VrTip VrTipQueue::choose(const VrTipInput& input) const {
    if (input.minigame != VrTip::None)
      return seen(input.minigame) ? VrTip::None : input.minigame;

    if (!input.playing)
      return VrTip::None;

    // The first one the player sees is always the welcome
    if (!seen(VrTip::Welcome))
      return VrTip::Welcome;

    if (input.meleeWeapon && !seen(VrTip::MeleeWeapon))
      return VrTip::MeleeWeapon;

    if (input.firearm && !seen(VrTip::Firearm))
      return VrTip::Firearm;

    return VrTip::None;
  }


  VrTip VrTipQueue::update(const VrTipInput& input, float dt) {
    VrTip next = choose(input);

    if (next != m_next) {
      m_next = next;
      m_time = 0.0f;
    }

    if (next == VrTip::None)
      return VrTip::None;

    m_time += dt;

    if (m_time < WaitTime)
      return VrTip::None;

    m_seen |= 1u << uint32_t(next);
    m_next = VrTip::None;
    m_time = 0.0f;
    return next;
  }


  void VrTipQueue::parseSeen(const std::string& text) {
    std::istringstream stream(text);
    std::string word;

    while (stream >> word) {
      for (uint32_t i = 0; i < uint32_t(VrTip::Count); i++) {
        if (word == TipNames[i])
          m_seen |= 1u << i;
      }
    }
  }


  std::string VrTipQueue::formatSeen() const {
    std::string text;

    for (uint32_t i = 0; i < uint32_t(VrTip::Count); i++) {
      if (seen(VrTip(i))) {
        text += TipNames[i];
        text += "\n";
      }
    }

    return text;
  }

}
