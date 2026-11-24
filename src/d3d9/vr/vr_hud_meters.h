#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief Which meters VrHudMeters placed in the top right corner
   */
  struct VrHudMetersPlaced {
    bool radiation = false;
    bool xp        = false;
    bool sneak     = false;
    bool breath    = false;
  };

  /**
   * \brief How far below the middle of the screen the sneak meter is placed, in interface units
   */
  constexpr float VrSneakMeterBelowMiddle = 30.0f;

  /**
   * \brief Moves the HUD's meters to where the panel in front of the head shows them
   *
   * The panel in front of the head brings the top right corner towards the
   * centre (vrComputeHeadHudLayout), so both meters are placed there, in a
   * column: the radiation meter in the row of the messages, which are in
   * the top left corner, and the XP meter just below it, so that the two
   * never overlap.
   *
   * The game puts the radiation meter in the top right corner already, a
   * little above the messages. It shows the experience gained and the
   * "LEVEL UP" line in the XPMeter group, in the bottom right corner, in
   * the same rows as the subtitles, which the panel keeps at the bottom.
   * The game window shows no HUD in game, so moving the groups there
   * changes nothing on the screen.
   *
   * Only HUDMainMenu::Create sets the radiation meter's height. The game
   * keeps the XP meter's base height in a trait of its own and sets its
   * height from that base when the HUD's visibility mode changes
   * (HUDMainMenu::UpdateVisibilityState), so both are moved, by the same
   * distance. See findings/hud.md §11-12.
   *
   * The sneak meter ([HIDDEN], [CAUTION], ...), which the game puts at the
   * top of the screen in the middle, where the panel moves the corners
   * to, is placed just below the middle of the screen instead: the panel
   * keeps the lower half where it is, so the meter is seen just below the
   * line of sight. Only HUDMainMenu::Create sets its height. See
   * findings/sneak.md.
   *
   * The breath meter, which the game shows under water 80 units below the
   * sneak meter, takes the sneak meter's place: the game hides the sneak
   * meter while the player swims, so the two are never seen together.
   * Only HUDMainMenu::Create sets its height. See findings/hud.md §18.
   *
   * Only used on the game's main thread.
   */
  class VrHudMeters {

  public:

    /**
     * \brief Checks that the executable is the supported version
     * \returns \c false if the game's code was not found for either meter
     */
    bool initialize();

    /**
     * \brief Moves the meters to their places if they are not there
     *
     * Called once per frame, since the game builds the HUD again (and puts
     * the meters back) when it reads the HUD's menu file.
     *
     * \param [in] interfaceHeight Height of the screen, in interface units
     * \returns Which meters were placed
     */
    VrHudMetersPlaced place(float interfaceHeight);

  private:

    bool m_radiationAvailable = false;
    bool m_xpAvailable        = false;
    bool m_sneakAvailable     = false;
    bool m_breathAvailable    = false;
    bool m_loggedRadiation    = false;
    bool m_loggedXp           = false;
    bool m_loggedSneak        = false;
    bool m_loggedBreath       = false;

    void placeColumn(uintptr_t hud, VrHudMetersPlaced& placed);

    void placeBelowMiddle(uintptr_t hud, float interfaceHeight, VrHudMetersPlaced& placed);

    static bool placeAt(uintptr_t tile, float y, bool& logged, const char* name);

  };

}
