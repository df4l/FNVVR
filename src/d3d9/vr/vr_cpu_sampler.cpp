#include "vr_cpu_sampler.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_module_path.h"

namespace dxvk {

  namespace {

    std::FILE* openSamplesFile() {
      std::wstring directory = vrModuleDirectory();

      if (directory.empty())
        return nullptr;

      std::wstring folder = directory + L"fnvvr\\";
      CreateDirectoryW(folder.c_str(), nullptr);
      return _wfopen((folder + L"cpu_samples.txt").c_str(), L"w");
    }


    /// Executable sections of a loaded module, as one range
    bool codeRange(HMODULE module, uint32_t& begin, uint32_t& end) {
      auto base = reinterpret_cast<const uint8_t*>(module);
      auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

      if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

      auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
      auto section = IMAGE_FIRST_SECTION(nt);

      begin = ~0u;
      end   = 0;

      for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
          continue;

        uint32_t start = uint32_t(reinterpret_cast<uintptr_t>(base)) + section->VirtualAddress;
        begin = std::min(begin, start);
        end   = std::max(end, start + std::max<uint32_t>(section->Misc.VirtualSize, section->SizeOfRawData));
      }

      return begin < end;
    }

  }


  std::unique_ptr<VrCpuSampler> VrCpuSampler::start(DWORD threadId) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
      FALSE, threadId);

    if (!thread) {
      Logger::warn(str::format("VR: CPU samples are off: the game thread cannot be opened (", GetLastError(), ")"));
      return nullptr;
    }

    std::FILE* file = openSamplesFile();

    if (!file) {
      Logger::warn("VR: CPU samples are off: fnvvr\\cpu_samples.txt cannot be written");
      CloseHandle(thread);
      return nullptr;
    }

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return std::unique_ptr<VrCpuSampler>(new VrCpuSampler(thread, stopEvent, file));
  }


  VrCpuSampler::VrCpuSampler(HANDLE thread, HANDLE stopEvent, std::FILE* file)
  : m_thread(thread), m_stopEvent(stopEvent), m_file(file),
    m_stackCopy(StackCopyBytes), m_ring(RingSize) {
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_started);
    m_windowStart = m_started;

    codeRange(GetModuleHandleW(nullptr), m_textBegin, m_textEnd);
    readModules();

    std::fprintf(m_file, "fnvvr-cpu-samples 1\n");
    std::fprintf(m_file, "exe-code %08x %08x\n", m_textBegin, m_textEnd);
    std::fflush(m_file);

    Logger::info(str::format("VR: Sampling the game thread every ", IntervalMs, " ms into fnvvr\\cpu_samples.txt"));

    m_worker = dxvk::thread([this] { run(); });
    SetThreadPriority(m_worker.native_handle(), THREAD_PRIORITY_HIGHEST);
  }


  VrCpuSampler::~VrCpuSampler() {
    SetEvent(m_stopEvent);
    m_worker.join();

    writeWindow();
    std::fclose(m_file);
    CloseHandle(m_stopEvent);
    CloseHandle(m_thread);
  }


  void VrCpuSampler::markSpike(uint64_t frameId) {
    std::lock_guard<std::mutex> lock(m_spikeMutex);
    m_spikes.push_back(uint32_t(frameId));
  }


  void VrCpuSampler::run() {
    env::setThreadName("vr-cpu-sampler");

    LARGE_INTEGER lastSpikes = m_started;

    while (WaitForSingleObject(m_stopEvent, IntervalMs) == WAIT_TIMEOUT) {
      sample();

      if (secondsSince(lastSpikes) >= 0.1) {
        QueryPerformanceCounter(&lastSpikes);
        writeSpikes();
      }

      if (secondsSince(m_windowStart) >= WindowSeconds)
        writeWindow();
    }
  }


  void VrCpuSampler::sample() {
    LARGE_INTEGER before;
    QueryPerformanceCounter(&before);

    if (SuspendThread(m_thread) == DWORD(-1)) {
      m_failed += 1;
      return;
    }

    // From here to ResumeThread: no lock, no allocation, no logging
    CONTEXT context = { };
    context.ContextFlags = CONTEXT_CONTROL;
    bool read = GetThreadContext(m_thread, &context);

    uint32_t frame = m_frame.load(std::memory_order_relaxed);
    uint8_t  phase = m_phase.load(std::memory_order_relaxed);
    uint32_t esp   = read ? uint32_t(context.Esp) : 0;
    uint32_t bytes = 0;

    if (esp >= m_stackLow && esp < m_stackTop) {
      bytes = std::min(StackCopyBytes, m_stackTop - esp) & ~3u;
      std::memcpy(m_stackCopy.data(), reinterpret_cast<const void*>(uintptr_t(esp)), bytes);
    }

    ResumeThread(m_thread);

    LARGE_INTEGER after;
    QueryPerformanceCounter(&after);
    m_suspendedTicks += uint64_t(after.QuadPart - before.QuadPart);

    if (!read) {
      m_failed += 1;
      return;
    }

    // The stack grew past the range known, or this is the first sample
    if (!bytes)
      findStack(esp);

    RingSample& entry = m_ring[m_ringNext];
    m_ringNext = (m_ringNext + 1) % RingSize;

    entry.frame = frame;
    entry.sample = VrCpuSample();
    entry.sample.eip   = uint32_t(context.Eip);
    entry.sample.phase = phase;
    findCallers(bytes, entry.sample);

    m_window.add(entry.sample);
  }


  void VrCpuSampler::findCallers(uint32_t bytes, VrCpuSample& sample) const {
    uint32_t found = 0;

    for (uint32_t offset = 0; offset + 4 <= bytes && found < sample.callers.size(); offset += 4) {
      uint32_t value;
      std::memcpy(&value, m_stackCopy.data() + offset, 4);

      if (value < m_textBegin + 7 || value >= m_textEnd)
        continue;

      if (vrFollowsCall(reinterpret_cast<const uint8_t*>(uintptr_t(value - 7))))
        sample.callers[found++] = value;
    }
  }


  void VrCpuSampler::findStack(uint32_t esp) {
    MEMORY_BASIC_INFORMATION info = { };

    if (!VirtualQuery(reinterpret_cast<const void*>(uintptr_t(esp)), &info, sizeof(info))
     || info.State != MEM_COMMIT) {
      m_stackLow = m_stackTop = 0;
      return;
    }

    m_stackLow = uint32_t(reinterpret_cast<uintptr_t>(info.BaseAddress));
    m_stackTop = m_stackLow + uint32_t(info.RegionSize);
  }


  void VrCpuSampler::readModules() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

    if (snapshot == INVALID_HANDLE_VALUE)
      return;

    std::vector<VrCpuModules::Module> modules;
    MODULEENTRY32 entry = { };
    entry.dwSize = sizeof(entry);

    for (BOOL more = Module32First(snapshot, &entry); more; more = Module32Next(snapshot, &entry)) {
      VrCpuModules::Module module;
      module.base = uint32_t(reinterpret_cast<uintptr_t>(entry.modBaseAddr));
      module.end  = module.base + entry.modBaseSize;
      module.name = entry.szModule;
      modules.push_back(std::move(module));
    }

    CloseHandle(snapshot);
    m_modules.set(std::move(modules));
  }


  void VrCpuSampler::writeSpikes() {
    std::vector<uint32_t> spikes;

    { std::lock_guard<std::mutex> lock(m_spikeMutex);
      spikes.swap(m_spikes);
    }

    for (uint32_t frame : spikes) {
      std::fprintf(m_file, "spike %u\n", frame);

      for (uint32_t i = 0; i < RingSize; i++) {
        const RingSample& entry = m_ring[(m_ringNext + i) % RingSize];

        if (entry.frame == frame && entry.sample.eip)
          std::fprintf(m_file, "f %u %08x %08x %08x\n", entry.sample.phase, entry.sample.eip,
            entry.sample.callers[0], entry.sample.callers[1]);
      }
    }

    if (!spikes.empty())
      std::fflush(m_file);
  }


  void VrCpuSampler::writeWindow() {
    double seconds = secondsSince(m_windowStart);
    QueryPerformanceCounter(&m_windowStart);

    if (!m_window.total())
      return;

    // Plugins may have loaded since the last window
    readModules();

    for (const auto& module : m_modules.modules())
      std::fprintf(m_file, "module %08x %08x %s\n", module.base, module.end, module.name.c_str());

    std::fprintf(m_file, "window %u %.1f %.1f %u %u\n", m_windowIndex, secondsSince(m_started) - seconds,
      seconds, m_window.total(), m_failed);

    for (const auto& entry : m_window.counts()) {
      std::fprintf(m_file, "s %u %u %08x %08x %08x\n", entry.second, entry.first.phase, entry.first.eip,
        entry.first.callers[0], entry.first.callers[1]);
    }

    std::fflush(m_file);

    uint32_t attempts = m_window.total() + m_failed;
    double suspendedUs = attempts
      ? 1e6 * double(m_suspendedTicks) / double(m_frequency.QuadPart) / double(attempts)
      : 0.0;

    Logger::info(str::format("VR: CPU samples over ", seconds, " s: ", m_window.total(), " of the game thread (",
      uint32_t(double(m_window.total()) / std::max(seconds, 1e-3)), " per second, ", m_failed, " failed, suspended ",
      suspendedUs, " us each); ", m_window.describeShares(m_modules, 6)));

    m_window.reset();
    m_windowIndex += 1;
    m_failed = 0;
    m_suspendedTicks = 0;
  }


  double VrCpuSampler::secondsSince(const LARGE_INTEGER& start) const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - start.QuadPart) / double(m_frequency.QuadPart);
  }

}
