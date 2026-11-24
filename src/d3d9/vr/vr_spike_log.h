#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "vr_frame_stats.h"
#include "vr_gpu_profile.h"
#include "vr_render_profile.h"

namespace dxvk {

  /**
   * \brief Logs the breakdown of each frame that missed its display period
   *
   * The averages of the other summaries hide the frames that make the
   * headset stutter. A frame is a spike when its cycle was late (one and a
   * half periods or more), when the game thread was busy for longer than a
   * period, or when its GPU time exceeded a period. Its line gives the
   * cycle's steps, the CPU and GPU time of each pass of each eye, and the
   * wall clock when the cycle ended, to find the frame in a perf recording
   * (\c tools/profile-cpu/spikes.py).
   *
   * What is known of a frame arrives at three moments: its CPU passes when
   * both eyes are drawn, its cycle at the next wait, its GPU times a few
   * frames later. The line is written once all of them are there.
   */
  class VrSpikeLog {

  public:

    using LineFn = std::function<void (const std::string& line)>;

    /// Told the id of each frame whose line is written
    using SpikeFn = std::function<void (uint64_t frameId)>;

    /// Frames over which the number of lines is limited
    static constexpr uint32_t WindowFrames = 1200;

    /// Lines written per window at most; the others are counted
    static constexpr uint32_t MaxLines = 30;

    /**
     * \param [in] periodMs Display period
     * \param [in] gpuTimed GPU times will be given for most frames
     * \param [in] sink Receives each line
     */
    VrSpikeLog(double periodMs, bool gpuTimed, LineFn sink);

    /**
     * \brief Starts a frame with the CPU time of its passes
     */
    void setCpuPasses(uint64_t frameId, const VrRenderProfile::EyePasses& passes);

    /**
     * \brief Gives the steps of a frame's cycle
     *
     * \param [in] endSeconds Wall clock at the end of the cycle
     */
    void setCycle(uint64_t frameId, const VrFrameCycle& cycle, double endSeconds);

    void setGpu(uint64_t frameId, const VrGpuFrameTimes& times);

    void setSpikeCallback(SpikeFn callback) {
      m_spikeCallback = std::move(callback);
    }

  private:

    static constexpr uint32_t RecordCount = 64;

    struct Record {
      uint64_t                   frameId  = 0;
      bool                       used     = false;
      bool                       hasCycle = false;
      bool                       hasGpu   = false;
      VrRenderProfile::EyePasses cpu      = { };
      VrFrameCycle               cycle    = { };
      double                     endSeconds = 0.0;
      VrGpuFrameTimes            gpu;
    };

    double   m_periodMs;
    bool     m_gpuTimed;
    LineFn   m_sink;
    SpikeFn  m_spikeCallback;

    std::array<Record, RecordCount> m_records;

    uint32_t m_frames     = 0;
    uint32_t m_lines      = 0;
    uint32_t m_suppressed = 0;

    Record* find(uint64_t frameId);

    void tryFinish(Record& record);

    void finish(Record& record);

    std::string describe(const Record& record, const std::string& reasons) const;

  };

}
