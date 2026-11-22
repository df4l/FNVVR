#include <algorithm>
#include <limits>

#include "vr_performance_governor.h"

namespace dxvk {

  namespace {

    constexpr double OverShare      = 0.95;
    constexpr double CalmShare      = 0.75;
    constexpr double WindowMs       = 250.0;
    constexpr double CalmMs         = 2000.0;
    constexpr double MaxCalmMs      = 16000.0;
    constexpr double RelapseMs      = 5000.0;
    /// Share of a window's frames over budget that lowers a level, as a ratio
    constexpr uint32_t OverPerWindow = 4;

  }


  VrPerformanceGovernor::Ladder::Ladder(uint32_t steps)
  : m_steps(steps) {
    reset();
  }


  void VrPerformanceGovernor::Ladder::reset() {
    m_level        = 0;
    m_windowFrames = 0;
    m_overFrames   = 0;
    m_windowMs     = 0.0;
    m_calmMs       = 0.0;
    m_sinceDownMs  = std::numeric_limits<double>::infinity();
    m_sinceUpMs    = std::numeric_limits<double>::infinity();
    m_calmNeededMs = CalmMs;
  }


  void VrPerformanceGovernor::Ladder::start(uint32_t level) {
    reset();
    m_level = std::min(level, m_steps);
  }


  bool VrPerformanceGovernor::Ladder::step(bool over, bool calm, double periodMs) {
    m_windowFrames += 1;
    m_overFrames   += over ? 1 : 0;
    m_windowMs     += periodMs;
    m_sinceDownMs  += periodMs;
    m_sinceUpMs    += periodMs;
    m_calmMs        = calm ? m_calmMs + periodMs : 0.0;

    // A long time without trouble forgets earlier relapses
    if (m_sinceDownMs >= MaxCalmMs)
      m_calmNeededMs = CalmMs;

    if (m_windowMs >= WindowMs) {
      bool overloaded = m_overFrames * OverPerWindow >= m_windowFrames;

      m_windowFrames = 0;
      m_overFrames   = 0;
      m_windowMs     = 0.0;

      if (overloaded && m_level < m_steps) {
        if (m_sinceUpMs < RelapseMs)
          m_calmNeededMs = std::min(m_calmNeededMs * 2.0, MaxCalmMs);

        m_level      += 1;
        m_sinceDownMs = 0.0;
        m_calmMs      = 0.0;
        return true;
      }
    }

    if (m_level > 0 && m_calmMs >= m_calmNeededMs && m_sinceDownMs >= m_calmNeededMs) {
      m_level    -= 1;
      m_sinceUpMs = 0.0;
      m_calmMs    = 0.0;
      return true;
    }

    return false;
  }


  VrPerformanceGovernor::VrPerformanceGovernor(uint32_t distanceSteps, uint32_t gpuSteps)
  : m_distance(distanceSteps), m_gpu(gpuSteps) { }


  bool VrPerformanceGovernor::update(const VrPerformanceSample& sample) {
    if (sample.periodMs <= 0.0)
      return false;

    double over = sample.periodMs * OverShare;
    double calm = sample.periodMs * CalmShare;

    bool cpuOver = sample.cpuMs > over || sample.cpuReprojected;
    bool cpuCalm = sample.cpuMs < calm && !sample.cpuReprojected;

    bool gpuKnown = sample.gpuMs >= 0.0;
    bool gpuOver  = (gpuKnown && sample.gpuMs > over && !cpuOver) || sample.gpuReprojected;
    bool gpuCalm  = (!gpuKnown || sample.gpuMs < calm) && !sample.gpuReprojected;

    bool changed = m_gpu.step(gpuOver, gpuCalm, sample.periodMs);

    bool gpuHasSteps = !m_gpu.exhausted();
    changed |= m_distance.step(cpuOver || (gpuOver && !gpuHasSteps),
      cpuCalm && (gpuCalm || gpuHasSteps), sample.periodMs);

    m_levels.distance = m_distance.level();
    m_levels.gpu      = m_gpu.level();
    return changed;
  }


  void VrPerformanceGovernor::reset() {
    m_distance.reset();
    m_gpu.reset();
    m_levels = VrPerformanceLevels();
  }


  void VrPerformanceGovernor::start(const VrPerformanceLevels& levels) {
    m_distance.start(levels.distance);
    m_gpu.start(levels.gpu);
    m_levels.distance = m_distance.level();
    m_levels.gpu      = m_gpu.level();
  }

}
