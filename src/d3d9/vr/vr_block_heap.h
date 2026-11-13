#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dxvk {

  /**
   * \brief Address space for \ref VrBlockHeap
   *
   * Reserves and commits pages. Memory is never given back: a block the
   * game still reads after freeing it keeps readable memory, as with the
   * game's own pools.
   */
  class VrHeapPages {

  public:

    virtual ~VrHeapPages() = default;

    /**
     * \brief Reserves address space
     *
     * \param [in] size Number of bytes, a multiple of the slab size
     * \returns Start of the range, aligned to the slab size, or 0
     */
    virtual uintptr_t reserve(size_t size) = 0;

    /**
     * \brief Commits reserved pages, zero-filled and read-write
     *
     * \param [in] address Start of the pages, inside a reserved range
     * \param [in] size Number of bytes
     */
    virtual bool commit(uintptr_t address, size_t size) = 0;

  };


  /**
   * \brief Result of \ref VrBlockHeap::free
   */
  enum class VrBlockFree {
    /// The pointer is not in the heap, its owner frees it
    NotOwned,
    /// The block is free again
    Freed,
    /// Freed twice, or not the start of a block: nothing changed
    Refused,
  };


  /**
   * \brief Counters of \ref VrBlockHeap, read without locking
   */
  struct VrBlockHeapStats {
    uint32_t slabs       = 0;
    uint32_t emptySlabs  = 0;
    uint64_t blocks      = 0;
    uint64_t bytes       = 0;
    uint32_t refused     = 0;
  };


  /**
   * \brief Allocator for small blocks
   *
   * Takes the game's blocks of up to 4 KiB (see VrGameHeap). Each 64 KiB
   * slab holds blocks of one size class. What the heap knows about its
   * blocks (owning slab, used bits) lives outside of them, so a game
   * write past a block or into a freed one cannot corrupt the allocator,
   * a block freed twice is noticed and left alone, and a pointer the heap
   * does not own is told apart by its address alone, so the previous
   * allocator keeps freeing its own blocks. Slabs are taken from address
   * space reserved in 16 MiB ranges when needed, never ahead.
   *
   * Thread safe: one lock per size class, one for taking slabs.
   *
   * Addresses must be below 4 GiB (the game's 32-bit process).
   */
  class VrBlockHeap {

  public:

    static constexpr uint32_t SlabShift     = 16;
    static constexpr size_t   SlabSize      = size_t(1) << SlabShift;
    static constexpr size_t   ArenaSize     = size_t(16) << 20;
    static constexpr size_t   MaxBlockSize  = 4096;
    static constexpr uint32_t ClassCount    = 63;

    explicit VrBlockHeap(VrHeapPages& pages);

    VrBlockHeap(const VrBlockHeap&) = delete;
    VrBlockHeap& operator = (const VrBlockHeap&) = delete;

    /**
     * \brief Allocates a block
     *
     * \param [in] size Number of bytes, 0 gives the smallest block
     * \returns The block, aligned to 4 bytes (8 from 8-byte classes on, 16
     *    from 16-byte classes), or \c nullptr when the size is above
     *    \ref MaxBlockSize or no address space is left
     */
    void* allocate(size_t size);

    /**
     * \brief Frees a block
     *
     * \param [in] block Pointer returned by \ref allocate, or any other
     */
    VrBlockFree free(void* block);

    /**
     * \brief Usable size of a block
     *
     * \returns The block's size class, or 0 when the heap does not own it
     */
    size_t blockSize(const void* block) const;

    /**
     * \brief Reads the counters
     */
    VrBlockHeapStats stats() const;

    /**
     * \brief Size class of a request, for sizes up to \ref MaxBlockSize
     */
    static uint32_t classIndex(size_t size);

    /**
     * \brief Block size of a size class
     */
    static uint32_t classSize(uint32_t index);

  private:

#ifdef _WIN32
    class Lock {
    public:
      void lock()   { AcquireSRWLockExclusive(&m_lock); }
      void unlock() { ReleaseSRWLockExclusive(&m_lock); }
    private:
      SRWLOCK m_lock = SRWLOCK_INIT;
    };
#else
    using Lock = std::mutex;
#endif

    static constexpr uint32_t BitmapWords   = uint32_t(SlabSize / 8 / 32);
    static constexpr uint32_t SlabCount     = uint32_t((uint64_t(1) << 32) >> SlabShift);

    struct Slab {
      uintptr_t             base        = 0;
      std::atomic<uint32_t> classIndex  = { 0 };
      std::atomic<uint32_t> blockSize   = { 0 };
      uint32_t              blockCount  = 0;
      uint32_t              freeCount   = 0;
      uint32_t              hint        = 0;
      Slab*                 prev        = nullptr;
      Slab*                 next        = nullptr;
      bool                  listed      = false;
      std::array<uint32_t, BitmapWords> used = { };
    };

    struct alignas(64) SizeClass {
      Lock                  lock;
      Slab*                 partial       = nullptr;
      uint32_t              partialCount  = 0;
      std::atomic<uint32_t> blocks        = { 0 };
    };

    VrHeapPages&            m_pages;

    std::array<SizeClass, ClassCount> m_classes;

    Lock                    m_slabLock;
    Slab*                   m_emptySlabs  = nullptr;
    uintptr_t               m_arenaNext   = 0;
    uintptr_t               m_arenaEnd    = 0;
    std::atomic<uint32_t>   m_slabCount   = { 0 };
    std::atomic<uint32_t>   m_emptyCount  = { 0 };
    std::atomic<uint32_t>   m_refused     = { 0 };

    // Owning slab of every 64 KiB of the address space
    std::array<std::atomic<Slab*>, SlabCount> m_slabs = { };

    Slab* findSlab(uintptr_t address) const;

    Slab* takeSlab(uint32_t index);

    void releaseSlab(Slab* slab);

    uintptr_t newSlabAddress();

    static void prepareSlab(Slab* slab, uint32_t index);

    static uint32_t takeBlock(Slab* slab);

    static void link(SizeClass& sizeClass, Slab* slab);

    static void unlink(SizeClass& sizeClass, Slab* slab);

  };

}
