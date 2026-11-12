#pragma once

#include <string>

namespace dxvk {

  /**
   * \brief Applies the engine patches ported from lStewieAl's Tweaks
   *
   * The inline groups of that plugin (bInlineMisc, bInlineRendering, ...)
   * shrink or remove hundreds of small functions the executable keeps as
   * real, unoptimised calls. The patches whose bytes are fully known are
   * ported here as data (vr_engine_patch_table.h, generated from the
   * plugin's source by tools/engine-patches). Each unit, one game function,
   * is applied only when every range still holds the original bytes, so a
   * function Tweaks, NVTF or another plugin already rewrote stays as it is,
   * and functions the VR code hooks are not in the table.
   *
   * The group Accessors adds the one-load accessors MSVC left unoptimised
   * (a frame, this stored to the stack and reloaded, one load): each
   * becomes the load alone, leaving the registers as the original does.
   *
   * Machine code from lStewieAl's Tweaks (MIT licence according to the
   * project, notice to be added next to DXVK's LICENSE before a release),
   * see findings/engine-optimizations.md, section 11.
   */
  class VrEnginePatches {

  public:

    /**
     * \brief Applies the units of the chosen groups
     *
     * \param [in] groups Comma-separated group names, or "all"
     */
    static void install(const std::string& groups);

  };

}
