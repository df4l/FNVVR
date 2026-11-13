#include <cstring>
#include <memory>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include <sys/mman.h>

#include "../vr_block_heap.h"

#include "test.h"

using namespace dxvk;

namespace {

  /**
   * \brief Address space below 4 GiB, as in the game's process
   */
  class LowPages : public VrHeapPages {

  public:

    uintptr_t reserve(size_t size) override {
      size_t total = size + VrBlockHeap::SlabSize;
      void* range = mmap(nullptr, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_NORESERVE, -1, 0);

      if (range == MAP_FAILED)
        return 0;

      uintptr_t start   = reinterpret_cast<uintptr_t>(range);
      uintptr_t aligned = (start + VrBlockHeap::SlabSize - 1) & ~uintptr_t(VrBlockHeap::SlabSize - 1);

      if (aligned > start)
        munmap(range, aligned - start);

      if (aligned + size < start + total)
        munmap(reinterpret_cast<void*>(aligned + size), start + total - aligned - size);

      reserved += size;
      return aligned;
    }

    bool commit(uintptr_t address, size_t size) override {
      committed += size;
      return !mprotect(reinterpret_cast<void*>(address), size, PROT_READ | PROT_WRITE);
    }

    size_t reserved  = 0;
    size_t committed = 0;

  };


  class NoPages : public VrHeapPages {

  public:

    uintptr_t reserve(size_t) override { return 0; }
    bool commit(uintptr_t, size_t) override { return false; }

  };

}


TEST_CASE(block_heap_classes_fit_every_size_with_little_waste) {
  CHECK(VrBlockHeap::classSize(VrBlockHeap::classIndex(0)) == 8);
  CHECK(VrBlockHeap::classSize(VrBlockHeap::classIndex(9)) == 12);
  CHECK(VrBlockHeap::classSize(VrBlockHeap::classIndex(VrBlockHeap::MaxBlockSize)) == VrBlockHeap::MaxBlockSize);

  bool fits = true;

  for (size_t size = 1; size <= VrBlockHeap::MaxBlockSize; size++) {
    uint32_t block = VrBlockHeap::classSize(VrBlockHeap::classIndex(size));
    size_t   waste = std::max<size_t>(7, size / 8);
    fits &= block >= size && block - size <= waste && block % 4 == 0;
  }

  CHECK(fits);
}


TEST_CASE(block_heap_gives_distinct_blocks_and_frees_them) {
  LowPages pages;
  auto owner = std::make_unique<VrBlockHeap>(pages);
  VrBlockHeap& heap = *owner;

  std::set<uintptr_t> blocks;

  for (uint32_t i = 0; i < 20000; i++) {
    void* block = heap.allocate(24);
    CHECK(block != nullptr);
    CHECK(heap.blockSize(block) == 24);
    blocks.insert(reinterpret_cast<uintptr_t>(block));
    std::memset(block, 0xAB, 24);
  }

  CHECK(blocks.size() == 20000);
  CHECK(*blocks.begin() % 8 == 0);
  CHECK(heap.stats().blocks == 20000);
  CHECK(heap.stats().bytes == 20000 * 24);

  for (uintptr_t block : blocks)
    CHECK(heap.free(reinterpret_cast<void*>(block)) == VrBlockFree::Freed);

  CHECK(heap.stats().blocks == 0);

  // Freed again, or a pointer inside a block: refused, nothing changes
  void* again = heap.allocate(24);
  CHECK(heap.free(again) == VrBlockFree::Freed);
  CHECK(heap.free(again) == VrBlockFree::Refused);

  char* live = static_cast<char*>(heap.allocate(24));
  CHECK(heap.free(live + 4) == VrBlockFree::Refused);
  CHECK(heap.stats().refused == 2);
  CHECK(heap.stats().blocks == 1);
  CHECK(heap.free(live) == VrBlockFree::Freed);

  // Memory the heap does not own goes to its owner
  int local = 0;
  CHECK(heap.free(&local) == VrBlockFree::NotOwned);
  CHECK(heap.blockSize(&local) == 0);
  CHECK(heap.free(nullptr) == VrBlockFree::NotOwned);

  CHECK(heap.allocate(VrBlockHeap::MaxBlockSize + 1) == nullptr);
}


TEST_CASE(block_heap_reuses_empty_slabs_across_classes) {
  LowPages pages;
  auto owner = std::make_unique<VrBlockHeap>(pages);
  VrBlockHeap& heap = *owner;

  // Three slabs of 8-byte blocks
  std::vector<void*> small;

  for (uint32_t i = 0; i < 3 * VrBlockHeap::SlabSize / 8; i++)
    small.push_back(heap.allocate(8));

  uint32_t slabs = heap.stats().slabs;
  CHECK(slabs == 3);

  for (void* block : small)
    heap.free(block);

  // The class keeps one slab, the others are shared again
  CHECK(heap.stats().emptySlabs == 2);

  void* large1 = heap.allocate(4000);
  void* large2 = heap.allocate(3000);
  CHECK(heap.stats().slabs == slabs);
  CHECK(heap.stats().emptySlabs == 0);
  CHECK(heap.blockSize(large1) == 4096);
  CHECK(heap.blockSize(large2) == 3072);
  CHECK(reinterpret_cast<uintptr_t>(large1) % 16 == 0);

  // Address space is taken a range at a time, memory a slab at a time
  CHECK(pages.reserved == VrBlockHeap::ArenaSize);
  CHECK(pages.committed == slabs * VrBlockHeap::SlabSize);
}


TEST_CASE(block_heap_without_address_space_lets_the_caller_fall_back) {
  NoPages pages;
  auto owner = std::make_unique<VrBlockHeap>(pages);
  VrBlockHeap& heap = *owner;

  CHECK(heap.allocate(16) == nullptr);
  CHECK(heap.stats().slabs == 0);
}


TEST_CASE(block_heap_is_thread_safe) {
  LowPages pages;
  auto owner = std::make_unique<VrBlockHeap>(pages);
  VrBlockHeap& heap = *owner;

  constexpr uint32_t ThreadCount = 4;
  std::vector<std::thread> threads;
  std::vector<uint32_t> errors(ThreadCount, 0);

  for (uint32_t t = 0; t < ThreadCount; t++) {
    threads.emplace_back([&heap, &errors, t] {
      std::mt19937 random(t + 1);
      std::vector<std::pair<uint8_t*, size_t>> live;

      for (uint32_t i = 0; i < 100000; i++) {
        if (live.size() < 500 && (live.empty() || random() % 3)) {
          size_t size = 1 + random() % 600;
          uint8_t* block = static_cast<uint8_t*>(heap.allocate(size));

          if (!block) {
            errors[t]++;
            continue;
          }

          std::memset(block, uint8_t(t * 16 + size % 16), size);
          live.emplace_back(block, size);
        } else {
          size_t pick = random() % live.size();
          auto [block, size] = live[pick];

          for (size_t j = 0; j < size; j++)
            errors[t] += block[j] != uint8_t(t * 16 + size % 16);

          errors[t] += heap.free(block) != VrBlockFree::Freed;
          live[pick] = live.back();
          live.pop_back();
        }
      }

      for (auto [block, size] : live)
        errors[t] += heap.free(block) != VrBlockFree::Freed;
    });
  }

  for (std::thread& thread : threads)
    thread.join();

  for (uint32_t count : errors)
    CHECK(count == 0);

  CHECK(heap.stats().blocks == 0);
  CHECK(heap.stats().refused == 0);
}
