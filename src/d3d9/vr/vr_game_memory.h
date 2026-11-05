#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/**
 * \brief Address the current function returns to
 *
 * Tells the game's callers of a hooked function apart. Only valid in the
 * hook itself, which the game calls through a pointer, so it is never
 * inlined.
 */
#ifdef _MSC_VER
#define VR_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())
#else
#define VR_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(__builtin_return_address(0))
#endif

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
     * \brief Reads the function a relative call instruction reaches
     *
     * \param [in] site Address of a 5-byte call (opcode E8)
     * \param [out] target Function the call reaches
     * \returns \c false if there is no such call at the site
     */
    static bool callTarget(
            uintptr_t             site,
            uintptr_t*            target);

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
     * \brief Checks that \ref chainCall can take a relative call instruction
     *
     * \param [in] site Address of a 5-byte call (opcode E8)
     * \param [in] expectedTarget Function the call reaches without other mods
     */
    static bool canChainCall(
            uintptr_t             site,
            uintptr_t             expectedTarget);

    /**
     * \brief Redirects a relative call instruction, over another mod's hook
     *
     * xNVSE and its plugins patch the game before the first Direct3D call,
     * so a call may already reach their code. That hook stays in the chain:
     * the new function calls \c previous instead of the game's function.
     *
     * \param [in] site Address of a 5-byte call (opcode E8)
     * \param [in] expectedTarget Function the call reaches without other mods
     * \param [in] newTarget Function to call instead
     * \param [out] previous Function the call reached, unchanged on failure
     * \returns \c true if the call was redirected. A call that reaches another
     *    function of the game, or this library, is left alone.
     */
    static bool chainCall(
            uintptr_t             site,
            uintptr_t             expectedTarget,
      const void*                 newTarget,
            uintptr_t*            previous);

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

    /**
     * \brief Replaces an entry of a virtual function table, over another mod's hook
     *
     * xNVSE and its plugins patch the game before the first Direct3D call,
     * so an entry may already reach their code. That hook stays in the chain:
     * the new function calls \c previous instead of the game's function.
     *
     * \param [in] slot Address of the table entry
     * \param [in] expectedTarget Function the entry holds without other mods
     * \param [in] newTarget Function to store instead
     * \param [out] previous Function the entry held
     * \returns \c true if the entry was replaced. An entry that holds another
     *    function of the game means a different executable and is left alone.
     */
    static bool chainVirtual(
            uintptr_t             slot,
            uintptr_t             expectedTarget,
      const void*                 newTarget,
            uintptr_t*            previous);

    /**
     * \brief Replaces bytes of the game's code
     *
     * \param [in] site Address of the bytes
     * \param [in] expected Bytes that must currently be there
     * \param [in] replacement Bytes to store instead
     * \param [in] size Number of bytes
     * \returns \c true if the bytes matched and were replaced
     */
    static bool replaceCode(
            uintptr_t             site,
      const uint8_t*              expected,
      const uint8_t*              replacement,
            size_t                size);

  };

}
