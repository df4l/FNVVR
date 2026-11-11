#pragma once

namespace dxvk {

  /**
   * \brief Replaces slow functions of the game's math code
   *
   * Two kinds. The C runtime's fabs saves and loads the x87 control word
   * twice around a bit mask, which stalls the pipeline: about 4.5 % of the
   * game's samples in the profiles of a 120 Hz run. And the small NiPoint3,
   * NiMatrix3 and NiTransform operations the executable keeps as real,
   * unoptimised calls (stack frames, x87 loads and stores): together about
   * 10 %. See findings/engine-optimizations.md, sections 7 to 10.
   *
   * Every function is checked against its original bytes first, so one
   * that a plugin already rewrote, or another executable, is left alone.
   */
  class VrFastMath {

  public:

    /**
     * \brief Redirects the functions
     *
     * Logs how many were replaced, and the ones that were not.
     */
    static void install();

  };

}
