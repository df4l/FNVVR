#pragma once

namespace dxvk {

  /**
   * \brief Keeps the world running while a casino game, Caravan or the vigor tester is open
   *
   * The menu stays a menu for the game's interface, its scripts and the VR
   * layer: the scripts' GameMode blocks wait for its end, as the intro's
   * quest does to have Doc Mitchell react to the vigor tester. Only the tests that stop the world answer as if there were none,
   * and the player, whose update follows the first of them, stays still, with
   * no input reaching them. See findings/minigames.md §9.
   *
   * Caravan's challenger is held in place for the menu's time, as by the
   * script command SetRestrained, and released at its end: otherwise their
   * AI package would walk them off the table (findings/caravan.md §3.5).
   * During the game itself they also raise their left hand, which holds
   * their cards in the eyes (findings/caravan.md §3.8).
   */
  class VrLiveCasino {

  public:

    /**
     * \brief Redirects the game's tests of the menu mode
     *
     * Logs and changes nothing unless all the calls are the expected ones.
     *
     * \param [in] casino Whether the casino's games run live
     * \param [in] vigorTester Whether the vigor tester runs live
     * \param [in] caravan Whether Caravan runs live
     */
    static void install(bool casino, bool vigorTester, bool caravan);

  private:

    static bool __cdecl frameMenuModeHook();
    static bool __cdecl menuModeHook();
    static void __cdecl setMainRenderingHook(bool defer);
    static void __fastcall threadInterfaceIdleHook(void* main, void* unused);

  };

}
