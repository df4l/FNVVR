#pragma once

namespace dxvk {

  /**
   * \brief What decides whether the world runs behind a casino game
   */
  struct VrLiveCasinoState {
    /// Blackjack, roulette, a slot machine or the vigor tester is the top
    /// menu, as far as each is allowed to run live. A message or the pause
    /// menu above it stops the world as any menu.
    bool casinoOnTop    = false;
    bool console        = false;
    /// The player cannot defend from a table: the game stops as it always did
    /// until they leave it, which also spares a death inside a menu
    bool playerInCombat = false;
    bool playerDead     = false;
  };

  inline bool vrCasinoRunsLive(const VrLiveCasinoState& state) {
    return state.casinoOnTop && !state.console && !state.playerInCombat && !state.playerDead;
  }

}
