#pragma once

namespace dxvk {

  /**
   * \brief Replaces the game's slow fabs with the x87 instruction
   *
   * The game's C runtime has a double precision fabs that saves and loads
   * the x87 control word twice (fstcw, fldcw) around a bit mask, which
   * stalls the pipeline: the profiles of a 120 Hz run (findings/engine-
   * optimizations.md, sections 7 and 9) put it at about 4.5 % of the game's
   * samples. One fabs instruction gives the same result.
   */
  class VrFastMath {

  public:

    /**
     * \brief Redirects the C runtime's fabs
     *
     * Does nothing if the function is not the original one. Logs which
     * it was.
     */
    static void install();

  };

}
