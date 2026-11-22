#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief What one frame cost, as the performance mode sees it
   */
  struct VrPerformanceSample {
    /// Display period, in milliseconds
    double periodMs = 0.0;
    /// Time the game thread was busy for the frame (frame minus the wait
    /// for the runtime), in milliseconds
    double cpuMs    = 0.0;
    /// GPU time of the frame as the runtime measured it, in milliseconds,
    /// negative if unknown
    double gpuMs    = -1.0;
    /// The runtime reprojected the frame because the CPU was late
    bool   cpuReprojected = false;
    /// The runtime reprojected the frame because the GPU was late
    bool   gpuReprojected = false;
  };

  /**
   * \brief How far the performance mode lowers its levers
   *
   * 0 is the player's own settings. Distance levels lower view distances,
   * which saves both CPU and GPU time; GPU levels lower what costs the GPU
   * only.
   */
  struct VrPerformanceLevels {
    uint32_t distance = 0;
    uint32_t gpu      = 0;

    bool operator == (const VrPerformanceLevels& other) const {
      return distance == other.distance && gpu == other.gpu;
    }

    bool operator != (const VrPerformanceLevels& other) const {
      return !(*this == other);
    }
  };

  /**
   * \brief Chooses the performance mode's levels from measured frames
   *
   * A frame is over budget when it took more than 95 % of the display
   * period, or the runtime reprojected it for that resource; calm below 75 %.
   * Every quarter of a second, a level goes down one step if a quarter of
   * the frames was over budget. A level goes back up one step after two
   * seconds of calm quarters, a quarter being calm when at least nine frames
   * in ten were calm and it did not lower a level. When a resource is over budget again within five
   * seconds of a step up, the calm needed doubles (up to 16 seconds), so the
   * image does not keep switching between two levels; it goes back to two
   * seconds after 16 seconds without a step down.
   *
   * The GPU time a runtime measures also grows while the GPU waits for a
   * late CPU, so a frame over the CPU's budget never counts against the GPU.
   * Distances save GPU time too: once the GPU levels are all used, a GPU
   * over budget lowers distances.
   */
  class VrPerformanceGovernor {

  public:

    VrPerformanceGovernor(uint32_t distanceSteps, uint32_t gpuSteps);

    /**
     * \brief Takes one frame into account
     * \returns \c true if the levels changed
     */
    bool update(const VrPerformanceSample& sample);

    /**
     * \brief Goes back to the player's own settings at once
     */
    void reset();

    /**
     * \brief Starts from given levels, as after a reset
     *
     * Levels past the last step are clamped.
     */
    void start(const VrPerformanceLevels& levels);

    const VrPerformanceLevels& levels() const {
      return m_levels;
    }

  private:

    class Ladder {

    public:

      explicit Ladder(uint32_t steps);

      bool step(bool over, bool calm, double periodMs);

      void reset();

      void start(uint32_t level);

      uint32_t level() const {
        return m_level;
      }

      bool exhausted() const {
        return m_level == m_steps;
      }

    private:

      uint32_t m_steps;
      uint32_t m_level        = 0;
      uint32_t m_windowFrames = 0;
      uint32_t m_overFrames   = 0;
      uint32_t m_uncalmFrames = 0;
      double   m_windowMs     = 0.0;
      double   m_calmMs       = 0.0;
      double   m_sinceDownMs  = 0.0;
      double   m_sinceUpMs    = 0.0;
      double   m_calmNeededMs = 0.0;

    };

    Ladder              m_distance;
    Ladder              m_gpu;
    VrPerformanceLevels m_levels;

  };

}
