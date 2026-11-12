#pragma once

#include <array>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Steps of one frame cycle whose duration is measured
   *
   * A cycle runs from the moment WaitGetPoses returns to the moment it
   * returns for the next frame, so the steps after Interval add up to it.
   */
  enum class VrFrameStep : uint32_t {
    /// Whole cycle
    Interval,
    /// Eye rendering: the game's draw calls for both eyes
    Render,
    /// Getting the eye rendering to the queue: DXVK's flush, the wait for
    /// its worker thread and the drain of its submission queue
    Sync,
    /// Moving the eye images to the layout the runtime reads
    Transition,
    /// Handing the eye images to the runtime
    Submit,
    /// Telling the runtime the frame is complete
    HandOff,
    /// From the hand-off to the next wait: the game logic of the next frame
    After,
    /// Submitting panels, their sync included, before the eye images
    Panels,
    /// Blocked until the runtime wants the next frame
    Wait,
  };

  constexpr uint32_t VrFrameStepCount = uint32_t(VrFrameStep::Wait) + 1;

  /**
   * \brief Durations of one frame cycle, in milliseconds, indexed by VrFrameStep
   */
  using VrFrameCycle = std::array<double, VrFrameStepCount>;

  /**
   * \brief Sum and largest value of one step over a window
   */
  struct VrFrameStepStats {
    double sum = 0.0;
    double max = 0.0;
  };

  /**
   * \brief Frame timing over a window of rendered frames
   *
   * Collects the durations of consecutive frame cycles until the window is
   * full. A frame is late when its cycle took at least half a display period
   * longer than one period, so the runtime showed the previous image again
   * or reprojected it.
   */
  class VrFrameStats {

  public:

    /**
     * \param [in] windowFrames Frames in a window, at least one
     * \param [in] periodMs Display period, in milliseconds
     */
    VrFrameStats(uint32_t windowFrames, double periodMs);

    /**
     * \brief Adds a frame cycle
     * \returns \c true if the window is full. It stays full until reset.
     */
    bool add(const VrFrameCycle& cycle);

    /**
     * \brief Empties the window
     */
    void reset();

    uint32_t frames() const {
      return m_frames;
    }

    uint32_t lateFrames() const {
      return m_late;
    }

    uint32_t busyFrames() const {
      return m_busy;
    }

    /**
     * \brief Time covered by the window's cycles, in seconds
     */
    double seconds() const {
      return m_steps[uint32_t(VrFrameStep::Interval)].sum / 1000.0;
    }

    const VrFrameStepStats& step(VrFrameStep step) const {
      return m_steps[uint32_t(step)];
    }

    /**
     * \brief Average duration of a step over the window, in milliseconds
     */
    double average(VrFrameStep step) const;

  private:

    uint32_t m_windowFrames;
    double   m_periodMs;
    uint32_t m_frames = 0;
    uint32_t m_late   = 0;
    uint32_t m_busy   = 0;

    std::array<VrFrameStepStats, VrFrameStepCount> m_steps = { };

  };

}
