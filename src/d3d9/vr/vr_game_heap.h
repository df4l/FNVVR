#pragma once

namespace dxvk {

  /**
   * \brief Gives the game's small blocks to an allocator of the mod
   *
   * MemoryManager::Allocate, Deallocate and Size jump to replacements:
   * blocks of up to 4 KiB come from a VrBlockHeap, larger ones and every
   * block the heap does not own (all those allocated before the install,
   * when the game's pools and heaps were set up) go through the game's own
   * functions, run from a copy of their first instructions. So the heap can
   * be installed while the game runs, and a failure of the new heap (no
   * address space left) falls back to the game's allocator.
   *
   * Differences with the game's pools: no fixed size per pool (a full pool
   * spilled into the slower default heap), no free list inside freed
   * blocks, double frees refused and logged instead of corrupting a pool.
   *
   * Nothing is installed when one of the three functions does not hold
   * its original bytes: another heap replacer (New Vegas Heap Replacer) or
   * a plugin that rewrote them keeps them. See findings/performance.md.
   */
  class VrGameHeap {

  public:

    /**
     * \brief Installs the replacements, once
     *
     * \returns \c true if the game's small blocks now come from the heap
     */
    static bool install();

    /**
     * \brief Logs the heap's counters about once a minute
     *
     * Also checks that the three functions still jump to the heap.
     */
    static void logStatsIfDue();

  };

}
