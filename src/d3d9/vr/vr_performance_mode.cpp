#include <algorithm>
#include <iomanip>
#include <sstream>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_frame_stats.h"
#include "vr_options_file.h"
#include "vr_performance_mode.h"

namespace dxvk {

  namespace {

    constexpr double LogWindowMs = 60000.0;

    constexpr wchar_t MemoryFile[] = L"performance.ini";

    std::string fixed(double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    }

    uint32_t percent(double part, double whole) {
      return whole > 0.0 ? uint32_t(100.0 * part / whole + 0.5) : 0u;
    }

    std::string distanceLabel(uint32_t level) {
      return str::format(VrPerformanceLevers::stereoReplay(level) ? "replay " : "",
        "x", fixed(VrPerformanceLevers::distanceScale(level)));
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

      if (m_available && !m_levers.canStereoReplay())
        Logger::warn("VR: The performance mode cannot turn water reflections, depth maps and refractions and specular highlights off, it never draws the right eye from the left eye's commands");
    }

    if (enabled == m_enabled)
      return;

    m_enabled = enabled;
    m_levers.restore();
    m_governor.reset();
    m_applied = false;
    m_window  = Window();
    m_memoryKey.clear();

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

      if (m_memoryKey.empty()) {
        recall(sample.periodMs);
        changed = true;
      }

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

      bool stepped = m_governor.update(sample);
      account(sample, hasGpu, stepped);
      changed |= stepped;
    }

    if (changed || !m_applied) {
      m_levers.apply(appliedLevels());
      m_applied = true;
    }
  }


  float VrPerformanceMode::renderScale() const {
    if (!m_enabled || !m_applied)
      return 1.0f;

    return VrPerformanceLevers::renderScale(m_governor.levels().gpu);
  }


  bool VrPerformanceMode::stereoReplay() const {
    return m_enabled && m_applied
        && m_levers.canStereoReplay()
        && VrPerformanceLevers::stereoReplay(appliedLevels().distance);
  }


  void VrPerformanceMode::forceStereoReplay(bool forced) {
    if (forced == m_replayForced)
      return;

    m_replayForced = forced;
    m_applied = false;
  }


  VrPerformanceLevels VrPerformanceMode::appliedLevels() const {
    VrPerformanceLevels levels = m_governor.levels();

    if (m_replayForced)
      levels.distance = std::max(levels.distance, VrPerformanceLevers::StereoReplayDistanceLevel);

    return levels;
  }


  void VrPerformanceMode::recall(double periodMs) {
    m_memoryKey = VrPerformanceMemory::key(m_backend.headsetModel(), periodMs);

    std::string text;

    if (VrOptionsFile::readText(MemoryFile, text))
      m_memory.parse(text);

    VrPerformanceLevels levels;

    if (!m_memory.find(m_memoryKey, levels)) {
      Logger::info(str::format("VR: Performance mode on ", m_memoryKey, " for the first time, it starts from the player's settings"));
      return;
    }

    m_governor.start(levels);
    levels = m_governor.levels();

    Logger::info(str::format("VR: Performance mode on ", m_memoryKey, " starts where it settled last time: distances ",
      distanceLabel(levels.distance), ", shadows and specular x",
      fixed(VrPerformanceLevers::gpuScale(levels.gpu)), ", world resolution ",
      percent(VrPerformanceLevers::renderScale(levels.gpu), 1.0), " %"));
  }


  void VrPerformanceMode::remember() {
    const Window& w = m_window;

    if (m_memoryKey.empty())
      return;

    VrPerformanceLevels settled;
    settled.distance = VrPerformanceMemory::mostHeld(w.distanceMs.data(), w.distanceMs.size());
    settled.gpu      = VrPerformanceMemory::mostHeld(w.gpuLevelMs.data(), w.gpuLevelMs.size());

    if (m_memory.store(m_memoryKey, settled))
      VrOptionsFile::writeText(MemoryFile, m_memory.format());
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
      distances << (i ? ", " : "") << distanceLabel(i)
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
      " level changes; time at each distance scale (replay: right eye drawn from the left eye's commands, "
      "water reflections, depth maps and refractions and specular off): ", distances.str(),
      "; at each shadow and specular scale / world resolution: ", gpuLevels.str(),
      "; now distances ", distanceLabel(appliedLevels().distance),
      m_replayForced ? " (d3d9.vrStereoReplayTest forces the replay, the times above are the governor's levels)" : "",
      ", shadows and specular x", fixed(VrPerformanceLevers::gpuScale(levels.gpu)),
      ", world resolution ", percent(VrPerformanceLevers::renderScale(levels.gpu), 1.0), " %"));

    remember();
    m_window = Window();
  }

}
