#include "test.h"

#include "../vr_live_casino_state.h"

using namespace dxvk;

TEST_CASE(live_casino_runs_only_with_a_casino_game_on_top) {
  VrLiveCasinoState state;
  CHECK(!vrCasinoRunsLive(state));

  state.casinoOnTop = true;
  CHECK(vrCasinoRunsLive(state));
}

TEST_CASE(live_casino_stops_for_the_console_combat_and_death) {
  VrLiveCasinoState state;
  state.casinoOnTop = true;

  state.console = true;
  CHECK(!vrCasinoRunsLive(state));
  state.console = false;

  state.playerInCombat = true;
  CHECK(!vrCasinoRunsLive(state));
  state.playerInCombat = false;

  state.playerDead = true;
  CHECK(!vrCasinoRunsLive(state));
}
