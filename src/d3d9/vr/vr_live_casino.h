#pragma once

namespace dxvk {

  /**
   * \brief Keeps the world running while a casino game is open
   *
   * The menu stays a menu for the game's interface, its scripts and the VR
   * layer. Only the tests that stop the world answer as if there were none,
   * and the player, whose update follows the first of them, stays still, with
   * no input reaching them. See findings/minigames.md §9.
   */
  class VrLiveCasino {

  public:

    /**
     * \brief Redirects the game's tests of the menu mode
     *
     * Logs and changes nothing unless all the calls are the expected ones.
     */
    static void install();

  private:

    static bool __cdecl frameMenuModeHook();
    static bool __cdecl menuModeHook();
    static void __cdecl setMainRenderingHook(bool defer);
    static void __fastcall threadInterfaceIdleHook(void* main, void* unused);

  };

}
