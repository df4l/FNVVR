#pragma once

namespace dxvk {

  /**
   * \brief What decides whether the world runs behind a casino game
   */
  struct VrLiveCasinoState {
    /// Blackjack, roulette, a slot machine, Caravan or the vigor tester is
    /// the top menu, as far as each is allowed to run live. A message or the pause
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


  /**
   * \brief What to do with the Caravan challenger this frame
   *
   * The challenger is held while the menu is on top, from the first frame
   * they stand alive (a script may have restrained them, or they may be
   * down), and released once there is no challenger to hold or another one.
   */
  struct VrCaravanHoldStep {
    bool release = false;
    bool hold    = false;
  };

  inline VrCaravanHoldStep vrCaravanHoldStep(const void* held, const void* challenger, bool challengerAlive) {
    VrCaravanHoldStep step;
    step.release = held && held != challenger;
    step.hold    = challenger && challenger != held && challengerAlive;
    return step;
  }

}
