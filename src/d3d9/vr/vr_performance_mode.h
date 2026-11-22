#pragma once

#include <array>

#include "vr_backend.h"
#include "vr_performance_governor.h"
#include "vr_performance_levers.h"

namespace dxvk {

  /**
   * \brief The performance mode of the Settings > VR page
   *
   * Off by default. While the player is in the world, each frame's game
   * thread time and the runtime's GPU time and reprojections go to a
   * VrPerformanceGovernor, whose levels VrPerformanceLevers applies. In
   * menus and loading screens the player's own values are back, so the
   * game's settings menu shows and saves them; the levels come back with
   * the world. Every 60 seconds one log line sums up what the mode saw and
   * did, so a tester only has to send the log (findings/performance.md §14).
   */
  class VrPerformanceMode {

  public:

    explicit VrPerformanceMode(IVRBackend& backend);

    ~VrPerformanceMode();

    VrPerformanceMode             (const VrPerformanceMode&) = delete;
    VrPerformanceMode& operator = (const VrPerformanceMode&) = delete;

    void setEnabled(bool enabled);

    /**
     * \brief Takes a frame into account
     *
     * Called once per frame, after the wait for the runtime.
     *
     * \param [in] inWorld The player is in the world, not in a menu
     * \param [in] timing What the wait returned
     */
    void update(bool inWorld, const VrFrameTiming& timing);

    /**
     * \brief Resolution the eyes' world is drawn at this frame
     *
     * A share of each side: 1 while the mode is off or in menus.
     */
    float renderScale() const;

  private:

    struct Window {
      uint32_t frames     = 0;
      double   ms         = 0.0;
      double   periodMs   = 0.0;
      double   cpuMs      = 0.0;
      uint32_t cpuOver    = 0;
      uint32_t gpuFrames  = 0;
      double   gpuMs      = 0.0;
      uint32_t gpuOver    = 0;
      uint32_t cpuReprojected = 0;
      uint32_t gpuReprojected = 0;
      uint32_t changes    = 0;
      std::array<double, VrPerformanceLevers::DistanceSteps + 1> distanceMs = { };
      std::array<double, VrPerformanceLevers::GpuSteps + 1>      gpuLevelMs = { };
    };

    IVRBackend&           m_backend;
    VrPerformanceGovernor m_governor;
    VrPerformanceLevers   m_levers;

    bool      m_initialized   = false;
    bool      m_available     = false;
    bool      m_enabled       = false;
    bool      m_applied       = false;
    bool      m_hasFrameIndex = false;
    uint32_t  m_frameIndex    = 0;
    Window    m_window;

    void account(const VrPerformanceSample& sample, bool hasGpu, bool changed);

    void logWindow();

  };

}
