#pragma once

namespace dxvk {

  /**
   * \brief Replaces the game's floor and ceil with SSE4.1 versions
   *
   * The game's C runtime has a double precision floor and ceil that go
   * through the x87 control word (fnstcw, fldcw twice, frndint), which is
   * slow: the profile of a 120 Hz run (findings/engine-optimizations.md,
   * section 7) puts the control word handling at about 4.5 % of the game's
   * samples, and the interface, navigation meshes and faces call them
   * thousands of times per frame. One rounding instruction gives the same
   * result.
   */
  class VrFastMath {

  public:

    /**
     * \brief Redirects the C runtime's floor and ceil
     *
     * Needs a processor with SSE4.1 and the executable's original code,
     * otherwise nothing changes. Logs which one it was.
     */
    static void install();

  };

}
