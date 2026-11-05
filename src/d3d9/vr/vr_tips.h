#pragma once

#include <cstdint>
#include <string>

namespace dxvk {

  /**
   * \brief The mod's own pop-ups, each shown once
   */
  enum class VrTip : uint32_t {
    Welcome,
    MeleeWeapon,
    Firearm,
    Count,
    None = Count,
  };

  /**
   * \brief What a pop-up can depend on, for one frame
   */
  struct VrTipInput {
    /// The player is free to move, out of combat and of any menu
    bool  playing      = false;
    /// A melee weapon is equipped
    bool  meleeWeapon  = false;
    /// A weapon reloaded by hand is equipped
    bool  firearm      = false;
  };

  /**
   * \brief The name of a pop-up, in the text files
   *
   * \returns The name, or \c nullptr for \c VrTip::None
   */
  const char* vrTipName(VrTip tip);

  /**
   * \brief Decides when a pop-up appears
   *
   * The situation has to last a moment first, so that a pop-up does not
   * come up during a transition or right after another one.
   */
  class VrTipQueue {

  public:

    /** Time a pop-up's situation has to last before it shows, in seconds */
    static constexpr float WaitTime = 2.0f;

    /**
     * \brief Advances by a frame
     *
     * \param [in] input The frame's situation
     * \param [in] dt Time since the last frame, in seconds
     * \returns The pop-up to show now, which counts as seen from then on
     */
    VrTip update(const VrTipInput& input, float dt);

    /**
     * \brief Reads the pop-ups already seen
     *
     * \param [in] text Names of the pop-ups, one per line
     */
    void parseSeen(const std::string& text);

    /** The pop-ups already seen, in the format of \ref parseSeen */
    std::string formatSeen() const;

    bool seen(VrTip tip) const {
      return (m_seen >> uint32_t(tip)) & 1u;
    }

  private:

    uint32_t m_seen = 0;
    float    m_time = 0.0f;
    VrTip    m_next = VrTip::None;

    VrTip choose(const VrTipInput& input) const;

  };

}
