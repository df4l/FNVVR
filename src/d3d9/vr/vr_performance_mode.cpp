#include <algorithm>
#include <iomanip>
#include <sstream>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_frame_stats.h"
#include "vr_performance_mode.h"

namespace dxvk {

  namespace {

    constexpr double LogWindowMs = 60000.0;

    std::string fixed(double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    }

    uint32_t percent(double part, double whole) {
      return whole > 0.0 ? uint32_t(100.0 * part / whole + 0.5) : 0u;
    }

  }


  VrPerformanceMode::VrPerformanceMode(IVRBackend& backend)
  : m_backend(backend),
    m_governor(VrPerformanceLevers::DistanceSteps, VrPerformanceLevers::GpuSteps) { }


  VrPerformanceMode::~VrPerformanceMode() {
    m_levers.restore();
  }


  void VrPerformanceMode::setEnabled(bool enabled) {
    if (enabled && !m_initialized) {
      m_initialized = true;
      m_available   = m_levers.initialize();
    }

    if (enabled == m_enabled)
      return;

    m_enabled = enabled;
    m_levers.restore();
    m_governor.reset();
    m_applied = false;
    m_window  = Window();

    if (enabled && !m_available)
      Logger::warn("VR: The performance mode has nothing to lower, the executable is not the supported version");
    else
      Logger::info(str::format("VR: Performance mode ", enabled ? "on" : "off"));
  }


  void VrPerformanceMode::update(bool inWorld, const VrFrameTiming& timing) {
    if (!m_enabled || !m_available)
      return;

    // The settings menu shows and saves the player's own values
    if (!inWorld) {
      if (m_applied)
        m_levers.restore();

      m_applied = false;
      return;
    }

    bool changed = false;

    if (timing.hasPreviousCycle && timing.predictedPeriod > 0) {
      VrPerformanceSample sample;
      sample.periodMs = double(timing.predictedPeriod) * 1e-6;
      sample.cpuMs    = timing.previousCycle[uint32_t(VrFrameStep::Interval)]
                      - timing.previousCycle[uint32_t(VrFrameStep::Wait)];

      // Each of the runtime's frames counts once
      VrFrameLoad load;
      bool hasGpu = m_backend.readFrameLoad(load)
        && (!m_hasFrameIndex || load.frameIndex != m_frameIndex);

      if (hasGpu) {
        m_hasFrameIndex = true;
        m_frameIndex    = load.frameIndex;
        sample.gpuMs          = load.gpuMs;
        sample.cpuReprojected = load.cpuReprojected;
        sample.gpuReprojected = load.gpuReprojected;
      }

      changed = m_governor.update(sample);
      account(sample, hasGpu, changed);
    }

    if (changed || !m_applied) {
      m_levers.apply(m_governor.levels());
      m_applied = true;
    }
  }


  float VrPerformanceMode::renderScale() const {
    if (!m_enabled || !m_applied)
      return 1.0f;

    return VrPerformanceLevers::renderScale(m_governor.levels().gpu);
  }


  void VrPerformanceMode::account(const VrPerformanceSample& sample, bool hasGpu, bool changed) {
    const VrPerformanceLevels& levels = m_governor.levels();
    Window& w = m_window;

    w.frames   += 1;
    w.ms       += sample.periodMs;
    w.periodMs  = sample.periodMs;
    w.cpuMs    += sample.cpuMs;
    w.cpuOver  += sample.cpuMs > sample.periodMs ? 1 : 0;
    w.changes  += changed ? 1 : 0;
    w.cpuReprojected += sample.cpuReprojected ? 1 : 0;
    w.gpuReprojected += sample.gpuReprojected ? 1 : 0;

    if (hasGpu && sample.gpuMs >= 0.0) {
      w.gpuFrames += 1;
      w.gpuMs     += sample.gpuMs;
      w.gpuOver   += sample.gpuMs > sample.periodMs ? 1 : 0;
    }

    w.distanceMs[std::min<size_t>(levels.distance, w.distanceMs.size() - 1)] += sample.periodMs;
    w.gpuLevelMs[std::min<size_t>(levels.gpu, w.gpuLevelMs.size() - 1)] += sample.periodMs;

    if (w.ms >= LogWindowMs)
      logWindow();
  }


  void VrPerformanceMode::logWindow() {
    const Window& w = m_window;
    const VrPerformanceLevels& levels = m_governor.levels();

    std::ostringstream distances;
    for (uint32_t i = 0; i < w.distanceMs.size(); i++) {
      distances << (i ? ", " : "") << fixed(VrPerformanceLevers::distanceScale(i))
        << " " << percent(w.distanceMs[i], w.ms) << " %";
    }

    std::ostringstream gpuLevels;
    for (uint32_t i = 0; i < w.gpuLevelMs.size(); i++) {
      gpuLevels << (i ? ", " : "") << fixed(VrPerformanceLevers::gpuScale(i))
        << "/" << percent(VrPerformanceLevers::renderScale(i), 1.0) << " % " << percent(w.gpuLevelMs[i], w.ms) << " %";
    }

    std::string gpu = w.gpuFrames
      ? str::format(fixed(w.gpuMs / double(w.gpuFrames)), " ms, over the period ", percent(w.gpuOver, w.gpuFrames), " %")
      : std::string("unknown");

    Logger::info(str::format("VR: Performance mode over ", fixed(w.ms / 1000.0), " s in the world, ", w.frames,
      " frames, period ", fixed(w.periodMs), " ms: game thread ", fixed(w.cpuMs / double(w.frames)),
      " ms, over the period ", percent(w.cpuOver, w.frames), " %; GPU ", gpu,
      "; reprojected for the CPU ", percent(w.cpuReprojected, w.frames),
      " %, for the GPU ", percent(w.gpuReprojected, w.frames), " %; ", w.changes,
      " level changes; time at each distance scale: ", distances.str(),
      " (water reflections off from x", fixed(VrPerformanceLevers::distanceScale(VrPerformanceLevers::WaterOffDistanceLevel)),
      "); at each shadow and specular scale / world resolution: ", gpuLevels.str(),
      "; now distances x", fixed(VrPerformanceLevers::distanceScale(levels.distance)),
      ", shadows and specular x", fixed(VrPerformanceLevers::gpuScale(levels.gpu)),
      ", world resolution ", percent(VrPerformanceLevers::renderScale(levels.gpu), 1.0), " %"));

    m_window = Window();
  }

}
