#include <algorithm>
#include <new>

#include "vr_block_heap.h"

namespace dxvk {

  namespace {

    /**
     * \brief Size classes: 4-byte steps up to 64 bytes, then eight classes
     * per doubling, so no block wastes more than an eighth of its size
     */
    struct ClassTable {
      std::array<uint32_t, VrBlockHeap::ClassCount> sizes = { };
      std::array<uint8_t, VrBlockHeap::MaxBlockSize / 4 + 1> bySize = { };

      constexpr ClassTable() {
        uint32_t count = 0;

        for (uint32_t size = 8; size <= 64; size += 4)
          sizes[count++] = size;

        for (uint32_t step = 8; step <= 256; step *= 2) {
          for (uint32_t i = 1; i <= 8; i++)
            sizes[count++] = 8 * step + i * step;
        }

        uint32_t index = 0;

        for (uint32_t quarter = 0; quarter < bySize.size(); quarter++) {
          while (sizes[index] < quarter * 4)
            index++;

          bySize[quarter] = uint8_t(index);
        }
      }
    };

    constexpr ClassTable Classes;

    static_assert(Classes.sizes[VrBlockHeap::ClassCount - 1] == VrBlockHeap::MaxBlockSize,
      "The last size class must be the largest block");

    uint32_t lowestZeroBit(uint32_t word) {
      return uint32_t(__builtin_ctz(~word));
    }

  }


  VrBlockHeap::VrBlockHeap(VrHeapPages& pages)
  : m_pages(pages) { }


  uint32_t VrBlockHeap::classIndex(size_t size) {
    return Classes.bySize[(std::min(size, MaxBlockSize) + 3) / 4];
  }


  uint32_t VrBlockHeap::classSize(uint32_t index) {
    return Classes.sizes[index];
  }


  void* VrBlockHeap::allocate(size_t size) {
    if (size > MaxBlockSize)
      return nullptr;

    uint32_t   index     = classIndex(size);
    SizeClass& sizeClass = m_classes[index];

    std::lock_guard<Lock> lock(sizeClass.lock);

    Slab* slab = sizeClass.partial;

    if (!slab) {
      slab = takeSlab(index);

      if (!slab)
        return nullptr;

      link(sizeClass, slab);
    }

    uint32_t block = takeBlock(slab);

    if (!slab->freeCount)
      unlink(sizeClass, slab);

    sizeClass.blocks.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<void*>(slab->base + uintptr_t(block) * slab->blockSize.load(std::memory_order_relaxed));
  }


  VrBlockFree VrBlockHeap::free(void* block) {
    uintptr_t address = reinterpret_cast<uintptr_t>(block);
    Slab*     slab    = findSlab(address);

    if (!slab)
      return VrBlockFree::NotOwned;

    // A live block keeps its slab in its class. A pointer into a slab that
    // another class took in the meantime was not a live block.
    uint32_t   index     = slab->classIndex.load(std::memory_order_acquire);
    SizeClass& sizeClass = m_classes[index];

    std::lock_guard<Lock> lock(sizeClass.lock);

    uint32_t size   = slab->blockSize.load(std::memory_order_relaxed);
    uintptr_t offset = address - slab->base;
    uint32_t number = uint32_t(offset / size);
    uint32_t word   = number / 32;
    uint32_t bit    = 1u << (number % 32);

    if (slab->classIndex.load(std::memory_order_relaxed) != index
     || offset % size || number >= slab->blockCount
     || !(slab->used[word] & bit)) {
      m_refused.fetch_add(1, std::memory_order_relaxed);
      return VrBlockFree::Refused;
    }

    slab->used[word] &= ~bit;
    slab->hint = std::min(slab->hint, word);
    sizeClass.blocks.fetch_sub(1, std::memory_order_relaxed);

    if (++slab->freeCount == 1)
      link(sizeClass, slab);

    // An empty slab goes back to the shared ones while its class has
    // another slab with room, so that classes do not hoard memory
    if (slab->freeCount == slab->blockCount && sizeClass.partialCount > 1) {
      unlink(sizeClass, slab);
      releaseSlab(slab);
    }

    return VrBlockFree::Freed;
  }


  size_t VrBlockHeap::blockSize(const void* block) const {
    Slab* slab = findSlab(reinterpret_cast<uintptr_t>(block));
    return slab ? slab->blockSize.load(std::memory_order_relaxed) : 0;
  }


  VrBlockHeapStats VrBlockHeap::stats() const {
    VrBlockHeapStats result;
    result.slabs      = m_slabCount.load(std::memory_order_relaxed);
    result.emptySlabs = m_emptyCount.load(std::memory_order_relaxed);
    result.refused    = m_refused.load(std::memory_order_relaxed);

    for (uint32_t i = 0; i < ClassCount; i++) {
      uint32_t blocks = m_classes[i].blocks.load(std::memory_order_relaxed);
      result.blocks += blocks;
      result.bytes  += uint64_t(blocks) * Classes.sizes[i];
    }

    return result;
  }


  VrBlockHeap::Slab* VrBlockHeap::findSlab(uintptr_t address) const {
    uintptr_t number = address >> SlabShift;

    if (number >= SlabCount)
      return nullptr;

    return m_slabs[number].load(std::memory_order_acquire);
  }


  VrBlockHeap::Slab* VrBlockHeap::takeSlab(uint32_t index) {
    std::lock_guard<Lock> lock(m_slabLock);

    Slab* slab = m_emptySlabs;

    if (slab) {
      m_emptySlabs = slab->next;
      m_emptyCount.fetch_sub(1, std::memory_order_relaxed);
      prepareSlab(slab, index);
      return slab;
    }

    slab = new (std::nothrow) Slab();

    if (!slab)
      return nullptr;

    uintptr_t base = newSlabAddress();

    if (!base) {
      delete slab;
      return nullptr;
    }

    slab->base = base;
    prepareSlab(slab, index);

    m_slabs[base >> SlabShift].store(slab, std::memory_order_release);
    m_slabCount.fetch_add(1, std::memory_order_relaxed);
    return slab;
  }


  void VrBlockHeap::releaseSlab(Slab* slab) {
    std::lock_guard<Lock> lock(m_slabLock);

    slab->next   = m_emptySlabs;
    m_emptySlabs = slab;
    m_emptyCount.fetch_add(1, std::memory_order_relaxed);
  }


  uintptr_t VrBlockHeap::newSlabAddress() {
    if (m_arenaNext == m_arenaEnd) {
      // A smaller range when the address space has no 16 MiB hole left
      for (size_t size : { ArenaSize, SlabSize }) {
        uintptr_t start = m_pages.reserve(size);

        if (start && (start + size - 1) >> SlabShift < SlabCount && !(start & (SlabSize - 1))) {
          m_arenaNext = start;
          m_arenaEnd  = start + size;
          break;
        }
      }

      if (m_arenaNext == m_arenaEnd)
        return 0;
    }

    uintptr_t base = m_arenaNext;

    if (!m_pages.commit(base, SlabSize)) {
      // Committing fails when memory runs out, a new range will not help
      return 0;
    }

    m_arenaNext += SlabSize;
    return base;
  }


  void VrBlockHeap::prepareSlab(Slab* slab, uint32_t index) {
    uint32_t size = Classes.sizes[index];

    slab->blockSize.store(size, std::memory_order_relaxed);
    slab->blockCount = uint32_t(SlabSize / size);
    slab->freeCount  = slab->blockCount;
    slab->hint       = 0;
    slab->prev       = nullptr;
    slab->next       = nullptr;
    slab->listed     = false;

    // Bits past the last block stay set, so they are never handed out
    slab->used.fill(~0u);
    std::fill(slab->used.begin(), slab->used.begin() + slab->blockCount / 32, 0u);

    if (slab->blockCount % 32)
      slab->used[slab->blockCount / 32] = ~0u << (slab->blockCount % 32);

    slab->classIndex.store(index, std::memory_order_release);
  }


  uint32_t VrBlockHeap::takeBlock(Slab* slab) {
    // Every word before the hint is full, and the slab has a free block
    uint32_t word = slab->hint;

    while (slab->used[word] == ~0u)
      word++;

    uint32_t bit = lowestZeroBit(slab->used[word]);
    slab->used[word] |= 1u << bit;
    slab->hint = word;
    slab->freeCount--;
    return word * 32 + bit;
  }


  void VrBlockHeap::link(SizeClass& sizeClass, Slab* slab) {
    slab->prev   = nullptr;
    slab->next   = sizeClass.partial;
    slab->listed = true;

    if (sizeClass.partial)
      sizeClass.partial->prev = slab;

    sizeClass.partial = slab;
    sizeClass.partialCount++;
  }


  void VrBlockHeap::unlink(SizeClass& sizeClass, Slab* slab) {
    if (!slab->listed)
      return;

    if (slab->prev)
      slab->prev->next = slab->next;
    else
      sizeClass.partial = slab->next;

    if (slab->next)
      slab->next->prev = slab->prev;

    slab->prev   = nullptr;
    slab->next   = nullptr;
    slab->listed = false;
    sizeClass.partialCount--;
  }

}
