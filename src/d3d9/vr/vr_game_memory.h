#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief Patches code in the host executable
   *
   * The VR layer lives inside d3d9.dll, which is loaded into the game's
   * process, so game code is reached by address. Every patch is checked
   * against the bytes it expects first, so that a different executable
   * version is left untouched instead of crashing.
   */
  class VrGameMemory {

  public:

    /**
     * \brief Redirects a relative call instruction
     *
     * \param [in] site Address of a 5-byte call (opcode E8)
     * \param [in] expectedTarget Function the call must currently reach
     * \param [in] newTarget Function to call instead
     * \returns \c true if the call matched and was redirected
     */
    static bool redirectCall(
            uintptr_t             site,
            uintptr_t             expectedTarget,
      const void*                 newTarget);

  };

}
