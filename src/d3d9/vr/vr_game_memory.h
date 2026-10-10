#pragma once

#include <cstddef>
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
     * \brief Checks that memory is readable and holds the given bytes
     *
     * \param [in] address Start of the range
     * \param [in] bytes Expected content
     * \param [in] size Number of bytes
     */
    static bool matches(
            uintptr_t             address,
      const uint8_t*              bytes,
            size_t                size);

    /**
     * \brief Checks that memory is committed and readable
     */
    static bool readable(
            uintptr_t             address,
            size_t                size);

    /**
     * \brief Checks that a relative call instruction reaches a function
     *
     * \param [in] site Address of a 5-byte call (opcode E8)
     * \param [in] target Function the call must reach
     */
    static bool callsTo(
            uintptr_t             site,
            uintptr_t             target);

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

    /**
     * \brief Replaces an entry of a virtual function table
     *
     * \param [in] slot Address of the table entry
     * \param [in] expectedTarget Function the entry must currently hold
     * \param [in] newTarget Function to store instead
     * \returns \c true if the entry matched and was replaced
     */
    static bool redirectVirtual(
            uintptr_t             slot,
            uintptr_t             expectedTarget,
      const void*                 newTarget);

  };

}
