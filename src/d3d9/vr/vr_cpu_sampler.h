#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

#include "../../util/thread.h"

#include "vr_cpu_samples.h"

namespace dxvk {

  /**
   * \brief Samples where the game thread runs, for testers without a profiler
   *
   * A thread of ours wakes every two milliseconds, suspends the game
   * thread, reads its instruction pointer and copies the top of its stack,
   * then resumes it (\c d3d9.vrCpuSamples). The copy gives the nearest
   * return addresses into the game, found by the call instruction before
   * them, since the game does not keep frame pointers. Each sample carries
   * the frame and the phase the renderer set.
   *
   * Nothing between the suspension and the resumption takes a lock or
   * allocates: the game thread may hold the heap's or the loader's lock.
   *
   * Every minute the samples are counted per distinct sample into
   * \c fnvvr\\cpu_samples.txt and summarised in the log per module and
   * phase. The samples of a frame the spike log reports are written one by
   * one. \c tools/profile-cpu/samples.py names the game's addresses.
   */
  class VrCpuSampler {

  public:

    /// Time between samples. A suspension costs the game thread about
    /// 60 us under Wine on a PC, so about 3 % of its time.
    static constexpr uint32_t IntervalMs = 2;

    /// Samples kept to write those of a spike frame (16 s)
    static constexpr uint32_t RingSize = 8192;

    /// Length of a window
    static constexpr double WindowSeconds = 60.0;

    /**
     * \brief Starts sampling a thread
     * \returns \c nullptr if the thread or the file cannot be opened
     */
    static std::unique_ptr<VrCpuSampler> start(DWORD threadId);

    ~VrCpuSampler();

    VrCpuSampler(const VrCpuSampler&) = delete;
    VrCpuSampler& operator = (const VrCpuSampler&) = delete;

    /**
     * \brief Sets the frame the next samples belong to
     */
    void setFrame(uint64_t frameId) {
      m_frame.store(uint32_t(frameId), std::memory_order_relaxed);
    }

    /**
     * \brief Sets the phase of the next samples
     * \returns The previous phase
     */
    uint8_t setPhase(uint8_t phase) {
      return m_phase.exchange(phase, std::memory_order_relaxed);
    }

    /**
     * \brief Asks for the samples of a frame to be written one by one
     */
    void markSpike(uint64_t frameId);

  private:

    struct RingSample {
      uint32_t    frame = 0;
      VrCpuSample sample;
    };

    static constexpr uint32_t StackCopyBytes = 4096;

    HANDLE      m_thread     = nullptr;
    HANDLE      m_stopEvent  = nullptr;
    std::FILE*  m_file       = nullptr;
    dxvk::thread m_worker;

    std::atomic<uint32_t> m_frame = { 0 };
    std::atomic<uint8_t>  m_phase = { uint8_t(VrCpuPhase::Game) };

    std::mutex            m_spikeMutex;
    std::vector<uint32_t> m_spikes;

    // Worker thread only
    uint32_t m_textBegin  = 0;
    uint32_t m_textEnd    = 0;
    uint32_t m_stackLow   = 0;
    uint32_t m_stackTop   = 0;
    std::vector<uint8_t>    m_stackCopy;
    std::vector<RingSample> m_ring;
    uint32_t m_ringNext   = 0;
    VrCpuModules     m_modules;
    VrCpuSampleTable m_window;
    uint32_t m_windowIndex = 0;
    uint32_t m_failed      = 0;
    uint64_t m_suspendedTicks = 0;
    LARGE_INTEGER m_frequency = { };
    LARGE_INTEGER m_started   = { };
    LARGE_INTEGER m_windowStart = { };

    VrCpuSampler(HANDLE thread, HANDLE stopEvent, std::FILE* file);

    void run();

    void sample();

    void findCallers(uint32_t bytes, VrCpuSample& sample) const;

    void findStack(uint32_t esp);

    void readModules();

    void writeSpikes();

    void writeWindow();

    double secondsSince(const LARGE_INTEGER& start) const;

  };


  /**
   * \brief Sets a phase for a scope and puts the previous one back
   */
  class VrCpuPhaseScope {

  public:

    VrCpuPhaseScope(VrCpuSampler* sampler, uint8_t phase)
    : m_sampler(sampler), m_previous(sampler ? sampler->setPhase(phase) : 0) { }

    ~VrCpuPhaseScope() {
      if (m_sampler)
        m_sampler->setPhase(m_previous);
    }

    VrCpuPhaseScope(const VrCpuPhaseScope&) = delete;
    VrCpuPhaseScope& operator = (const VrCpuPhaseScope&) = delete;

  private:

    VrCpuSampler* m_sampler;
    uint8_t       m_previous;

  };

}
