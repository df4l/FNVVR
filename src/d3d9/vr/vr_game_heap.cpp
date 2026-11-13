#include <atomic>
#include <chrono>
#include <cstring>

#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_block_heap.h"
#include "vr_game_addresses.h"
#include "vr_game_heap.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    using AllocateFn  = void*    (__thiscall*)(void* manager, uint32_t size);
    using FreeFn      = void     (__thiscall*)(void* manager, void* block);
    using SizeFn      = uint32_t (__thiscall*)(void* manager, void* block);

    constexpr size_t   JumpLength        = 5;
    constexpr size_t   EntryLength       = 8;
    constexpr size_t   TrampolineStride  = 16;
    constexpr uint32_t LoggedRefusals    = 10;
    constexpr auto     StatsPeriod       = std::chrono::seconds(60);

    /**
     * \brief Address space of the game's process
     *
     * Ranges are reserved from the top down, away from the low addresses
     * where Direct3D, DXVK and the game's own heaps take large ranges.
     */
    class GamePages : public VrHeapPages {

    public:

      uintptr_t reserve(size_t size) override {
        return reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE));
      }

      bool commit(uintptr_t address, size_t size) override {
        return VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
      }

    };

    struct Function {
      const char*     name;
      uintptr_t       address;
      const uint8_t*  start;
      const void*     replacement;
    };

    // Never destroyed: plugins may free game blocks while the process exits
    GamePages*            g_pages     = nullptr;
    VrBlockHeap*          g_heap      = nullptr;

    AllocateFn            g_allocate  = nullptr;
    FreeFn                g_free      = nullptr;
    SizeFn                g_size      = nullptr;

    std::atomic<uint32_t> g_refusalsLogged = { 0 };
    bool                  g_overwrittenLogged = false;


    void* __fastcall allocateHook(void* manager, void*, uint32_t size) {
      void* block = g_heap->allocate(size);
      return block ? block : g_allocate(manager, size);
    }


    void __fastcall freeHook(void* manager, void*, void* block) {
      if (!block)
        return;

      switch (g_heap->free(block)) {
        case VrBlockFree::Freed:
          return;

        case VrBlockFree::NotOwned:
          g_free(manager, block);
          return;

        case VrBlockFree::Refused:
          if (g_refusalsLogged.fetch_add(1, std::memory_order_relaxed) < LoggedRefusals) {
            Logger::warn(str::format("VR: The game freed ", block, ", which is not an allocated block of the heap"
              " (freed twice?), from ", reinterpret_cast<void*>(VR_RETURN_ADDRESS()), "; left as it is"));
          }
          return;
      }
    }


    uint32_t __fastcall sizeHook(void* manager, void*, void* block) {
      size_t size = g_heap->blockSize(block);
      return size ? uint32_t(size) : g_size(manager, block);
    }


    void makeJump(uint8_t* code, uintptr_t from, uintptr_t to) {
      int32_t displacement = int32_t(to - (from + JumpLength));
      code[0] = 0xE9;
      std::memcpy(code + 1, &displacement, sizeof(displacement));
    }


    /**
     * \brief Copies of the functions' frame setup, followed by a jump to the
     * rest of each function, so that the originals still run
     */
    uintptr_t makeTrampolines(const Function* functions, size_t count) {
      void* memory = VirtualAlloc(nullptr, TrampolineStride * count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

      if (!memory)
        return 0;

      uintptr_t base = reinterpret_cast<uintptr_t>(memory);

      for (size_t i = 0; i < count; i++) {
        uint8_t*  code = reinterpret_cast<uint8_t*>(base + i * TrampolineStride);
        uintptr_t rest = functions[i].address + VrGame::HeapFunctionFrameLength;

        std::memcpy(code, functions[i].start, VrGame::HeapFunctionFrameLength);
        makeJump(code + VrGame::HeapFunctionFrameLength, base + i * TrampolineStride + VrGame::HeapFunctionFrameLength, rest);
      }

      DWORD oldProtect = 0;

      if (!VirtualProtect(memory, TrampolineStride * count, PAGE_EXECUTE_READ, &oldProtect)) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return 0;
      }

      FlushInstructionCache(GetCurrentProcess(), memory, TrampolineStride * count);
      return base;
    }


    /**
     * \brief Writes the jump over a function's first 8 bytes in one store
     *
     * Another thread may run the function meanwhile: it sees either the
     * whole old start or the whole jump. The function is 16-byte aligned,
     * so the 8 bytes are one aligned word.
     */
    bool redirectEntry(const Function& function) {
      uint8_t newCode[EntryLength];
      std::memcpy(newCode, function.start, EntryLength);
      makeJump(newCode, function.address, reinterpret_cast<uintptr_t>(function.replacement));

      LONG64 oldValue;
      LONG64 newValue;
      std::memcpy(&oldValue, function.start, EntryLength);
      std::memcpy(&newValue, newCode, EntryLength);

      void* code = reinterpret_cast<void*>(function.address);
      DWORD oldProtect = 0;

      if (!VirtualProtect(code, EntryLength, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool written = InterlockedCompareExchange64(static_cast<volatile LONG64*>(code), newValue, oldValue) == oldValue;

      VirtualProtect(code, EntryLength, oldProtect, &oldProtect);
      FlushInstructionCache(GetCurrentProcess(), code, EntryLength);
      return written;
    }


    bool stillRedirected(const Function& function) {
      uint8_t jump[JumpLength];
      makeJump(jump, function.address, reinterpret_cast<uintptr_t>(function.replacement));
      return VrGameMemory::matches(function.address, jump, JumpLength);
    }


    // Size and Deallocate first: until Allocate jumps, they only pass every
    // block on, so a failure part way leaves the game as it was
    const Function g_functions[] = {
      { "Size",       VrGame::HeapBlockSize,     VrGame::HeapBlockSizeStart,     reinterpret_cast<const void*>(&sizeHook)     },
      { "Deallocate", VrGame::HeapFreeBlock,     VrGame::HeapFreeBlockStart,     reinterpret_cast<const void*>(&freeHook)     },
      { "Allocate",   VrGame::HeapAllocateBlock, VrGame::HeapAllocateBlockStart, reinterpret_cast<const void*>(&allocateHook) },
    };

    constexpr size_t FunctionCount = sizeof(g_functions) / sizeof(g_functions[0]);

  }


  bool VrGameHeap::install() {
    if (g_heap)
      return true;

    for (const Function& function : g_functions) {
      if ((function.address & (EntryLength - 1))
       || !VrGameMemory::matches(function.address, function.start, EntryLength)) {
        Logger::info(str::format("VR: MemoryManager::", function.name, " is not the game's original"
          " (another heap replacer or plugin), the game keeps its heap"));
        return false;
      }
    }

    uintptr_t trampolines = makeTrampolines(g_functions, FunctionCount);

    if (!trampolines) {
      Logger::warn("VR: No memory for the heap's trampolines, the game keeps its heap");
      return false;
    }

    g_size     = reinterpret_cast<SizeFn>    (trampolines + 0 * TrampolineStride);
    g_free     = reinterpret_cast<FreeFn>    (trampolines + 1 * TrampolineStride);
    g_allocate = reinterpret_cast<AllocateFn>(trampolines + 2 * TrampolineStride);

    g_pages = new GamePages();
    g_heap  = new VrBlockHeap(*g_pages);

    for (const Function& function : g_functions) {
      if (!redirectEntry(function)) {
        // The functions redirected so far pass every block on
        Logger::err(str::format("VR: MemoryManager::", function.name, " could not be redirected,"
          " the game keeps its heap"));
        return false;
      }
    }

    Logger::info(str::format("VR: The game's blocks of up to ", VrBlockHeap::MaxBlockSize,
      " bytes come from the VR heap (d3d9.vrGameHeap)"));
    return true;
  }


  void VrGameHeap::logStatsIfDue() {
    static auto s_next = std::chrono::steady_clock::now() + StatsPeriod;

    if (!g_heap)
      return;

    auto now = std::chrono::steady_clock::now();

    if (now < s_next)
      return;

    s_next = now + StatsPeriod;

    VrBlockHeapStats stats = g_heap->stats();

    Logger::info(str::format("VR: Game heap: ", stats.bytes >> 20, " MiB in ", stats.blocks, " blocks, ",
      stats.slabs, " slabs (", (uint64_t(stats.slabs) * VrBlockHeap::SlabSize) >> 20, " MiB, ",
      stats.emptySlabs, " empty), ", stats.refused, " frees refused"));

    if (!g_overwrittenLogged) {
      for (const Function& function : g_functions) {
        if (!stillRedirected(function)) {
          Logger::err(str::format("VR: MemoryManager::", function.name, " was rewritten by another module"
            " after the VR heap was installed"));
          g_overwrittenLogged = true;
        }
      }
    }
  }

}
