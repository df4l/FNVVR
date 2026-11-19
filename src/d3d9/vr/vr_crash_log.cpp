#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_crash_log.h"

namespace dxvk {

  namespace {

    /// Exceptions logged at most: the last one before the game closes is
    /// the crash, earlier ones may be caught by the game. A crash logger
    /// raises its own while it reads the crash, with its module on the stack.
    constexpr uint32_t MaxLogged = 8;

    /// Stack words searched for return addresses, and return addresses
    /// logged at most
    constexpr uint32_t StackWords   = 1024;
    constexpr uint32_t MaxReturns   = 32;

    /// Return addresses logged from the far end of the stack, where the
    /// thread started, when the near end does not reach it
    constexpr uint32_t MaxOuterReturns = 48;

    /// Exceptions of any kind logged at most for a watched thread
    constexpr uint32_t MaxWatchedLogged = 8;

    /// STATUS_HEAP_CORRUPTION, raised by the heap on a corrupted block
    constexpr DWORD HeapCorruption = 0xC0000374;

    std::atomic<uint32_t> g_logged = { 0u };
    std::atomic<bool>     g_installed = { false };

    std::atomic<uint32_t> g_watchedThread = { 0u };
    std::atomic<uint32_t> g_watchedLogged = { 0u };

    bool isFatal(DWORD code) {
      switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_IN_PAGE_ERROR:
        case HeapCorruption:
          return true;

        default:
          return false;
      }
    }

    /**
     * \brief Exceptions raised only to talk to a debugger
     */
    bool isDebuggerMessage(DWORD code) {
      switch (code) {
        case 0x406D1388u:   // MS_VC_EXCEPTION, naming a thread
        case DBG_PRINTEXCEPTION_C:
        case 0x4001000Au:   // DBG_PRINTEXCEPTION_WIDE_C
          return true;

        default:
          return false;
      }
    }

    std::string hex(uintptr_t value) {
      static const char digits[] = "0123456789ABCDEF";
      std::string text = "0x00000000";

      for (uint32_t i = 0; i < 8; i++)
        text[9 - i] = digits[(value >> (4 * i)) & 0xF];

      return text;
    }

    /**
     * \brief An address as module+offset, or an empty string outside any
     *    module
     */
    std::string moduleOffset(uintptr_t address) {
      HMODULE module = nullptr;

      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(address), &module) || !module)
        return std::string();

      WCHAR path[MAX_PATH] = { };
      DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
      std::wstring name(path, length < MAX_PATH ? length : 0);
      size_t slash = name.find_last_of(L"\\/");

      if (slash != std::wstring::npos)
        name = name.substr(slash + 1);

      return str::format(str::fromws(name.c_str()), "+", hex(address - reinterpret_cast<uintptr_t>(module)));
    }

    bool isCode(uintptr_t address) {
      MEMORY_BASIC_INFORMATION info = { };

      if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;

      return info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    }

    /**
     * \brief Number of readable bytes from an address, up to a limit
     */
    size_t readableBytes(uintptr_t address, size_t limit) {
      size_t total = 0;

      while (total < limit) {
        MEMORY_BASIC_INFORMATION info = { };

        if (!VirtualQuery(reinterpret_cast<LPCVOID>(address + total), &info, sizeof(info))
         || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
          break;

        uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        total = end - address;
      }

      return total < limit ? total : limit;
    }

    std::string returnAddresses(uintptr_t stack) {
      std::string list;
      size_t words = readableBytes(stack, StackWords * sizeof(uintptr_t)) / sizeof(uintptr_t);
      auto values = reinterpret_cast<const uintptr_t*>(stack);
      uint32_t found = 0;

      for (size_t i = 0; i < words && found < MaxReturns; i++) {
        uintptr_t value = values[i];

        if (!isCode(value))
          continue;

        std::string where = moduleOffset(value);

        if (where.empty())
          continue;

        if (!list.empty())
          list += ", ";

        list += where;
        found++;
      }

      return list;
    }

    /**
     * \brief Return addresses nearest the far end of the stack, where the
     *    thread started, or an empty string when the near end's list
     *    already reaches it
     */
    std::string outerReturnAddresses(uintptr_t stack) {
      auto tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
      auto base = reinterpret_cast<uintptr_t>(tib->StackBase);

      if (base <= stack)
        return std::string();

      size_t words = readableBytes(stack, base - stack) / sizeof(uintptr_t);
      auto values = reinterpret_cast<const uintptr_t*>(stack);
      std::vector<std::string> found;

      for (size_t i = 0; i < words; i++) {
        if (!isCode(values[i]))
          continue;

        std::string where = moduleOffset(values[i]);

        if (!where.empty())
          found.push_back(std::move(where));
      }

      if (found.size() <= MaxReturns)
        return std::string();

      size_t first = found.size() > MaxReturns + MaxOuterReturns ? found.size() - MaxOuterReturns : MaxReturns;
      std::string list = str::format(found.size(), " found, from the ", first + 1, "th: ");

      for (size_t i = first; i < found.size(); i++)
        list += (i == first ? "" : ", ") + found[i];

      return list;
    }

    /**
     * \brief An address with its module and offset, when it has one
     */
    std::string location(uintptr_t address) {
      std::string where = moduleOffset(address);
      return where.empty() ? hex(address) : str::format(hex(address), " (", where, ")");
    }

    LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
      const EXCEPTION_RECORD* record = info->ExceptionRecord;

      bool watched = g_watchedThread.load() == GetCurrentThreadId()
        && !isDebuggerMessage(record->ExceptionCode);

      if (watched && !isFatal(record->ExceptionCode)) {
        if (g_watchedLogged.fetch_add(1u) < MaxWatchedLogged) {
          Logger::err(str::format("VR: Exception ", hex(record->ExceptionCode), " raised at ",
            location(reinterpret_cast<uintptr_t>(record->ExceptionAddress)), " on the watched thread ",
            GetCurrentThreadId(), ", flags ", hex(record->ExceptionFlags)));
          Logger::err(str::format("VR: Return addresses on the stack: ", returnAddresses(info->ContextRecord->Esp)));
        }

        return EXCEPTION_CONTINUE_SEARCH;
      }

      if (!isFatal(record->ExceptionCode) || g_logged.fetch_add(1u) >= MaxLogged)
        return EXCEPTION_CONTINUE_SEARCH;

      auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
      std::string access;

      if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        ULONG_PTR kind = record->ExceptionInformation[0];
        access = str::format(", ", kind == 0 ? "reading " : kind == 1 ? "writing " : "executing ",
          hex(record->ExceptionInformation[1]));
      }

      Logger::err(str::format("VR: Exception ", hex(record->ExceptionCode), " at ", location(address), access,
        ", thread ", GetCurrentThreadId()));

      // d3d9.dll is only built for 32-bit x86
      const CONTEXT* c = info->ContextRecord;
      Logger::err(str::format("VR: Registers: eax ", hex(c->Eax), " ebx ", hex(c->Ebx), " ecx ", hex(c->Ecx),
        " edx ", hex(c->Edx), " esi ", hex(c->Esi), " edi ", hex(c->Edi), " ebp ", hex(c->Ebp), " esp ", hex(c->Esp)));

      // Too little stack is left to search it after an overflow
      if (record->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        Logger::err(str::format("VR: Return addresses on the stack: ", returnAddresses(c->Esp)));

        std::string outer = outerReturnAddresses(c->Esp);

        if (!outer.empty())
          Logger::err(str::format("VR: Return addresses towards the thread's start, ", outer));
      }

      return EXCEPTION_CONTINUE_SEARCH;
    }

  }


  void VrCrashLog::install() {
    if (g_installed.exchange(true))
      return;

    if (AddVectoredExceptionHandler(0, &handler))
      Logger::info("VR: Fatal exceptions are written to this log");
  }



  void VrCrashLog::watchThread(uint32_t threadId) {
    g_watchedThread = threadId;
  }


  std::string VrCrashLog::describeThread(uint32_t threadId) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);

    if (!thread)
      return "not readable";

    CONTEXT context = { };
    context.ContextFlags = CONTEXT_CONTROL;
    bool read = false;

    // Only the registers are read while the thread is suspended: it may
    // hold a lock that reading its stack would take
    if (SuspendThread(thread) != DWORD(-1)) {
      read = GetThreadContext(thread, &context);
      ResumeThread(thread);
    }

    CloseHandle(thread);

    if (!read)
      return "not readable";

    return str::format("at ", location(context.Eip), ", return addresses on the stack: ", returnAddresses(context.Esp));
  }

}
