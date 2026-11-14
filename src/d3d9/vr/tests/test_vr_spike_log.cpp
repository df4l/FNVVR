#include <string>
#include <vector>

#include "test.h"

#include "../vr_spike_log.h"

using namespace dxvk;

namespace {

  VrFrameCycle cycle(double interval, double wait) {
    VrFrameCycle result = { };
    result[uint32_t(VrFrameStep::Interval)] = interval;
    result[uint32_t(VrFrameStep::Wait)]     = wait;
    return result;
  }

  VrGpuFrameTimes gpuFrame(double ms) {
    VrGpuFrameTimes times;
    times.parts[uint32_t(VrGpuFramePart::Frame)] = ms;
    times.eyes[1][uint32_t(VrRenderPass::Scene)] = ms / 2.0;
    return times;
  }

  bool contains(const std::string& line, const char* text) {
    return line.find(text) != std::string::npos;
  }

}

TEST_CASE(spikeLogSkipsFramesOnTime) {
  std::vector<std::string> lines;
  VrSpikeLog log(8.33, false, [&] (const std::string& line) { lines.push_back(line); });

  log.setCpuPasses(1, { });
  log.setCycle(1, cycle(8.33, 4.0), 100.0);
  CHECK(lines.empty());
}

TEST_CASE(spikeLogWritesALateFrameOnceItsGpuTimesArrive) {
  std::vector<std::string> lines;
  VrSpikeLog log(8.33, true, [&] (const std::string& line) { lines.push_back(line); });

  VrRenderProfile::EyePasses passes = { };
  passes[0][uint32_t(VrRenderPass::Scene)] = 3.5;

  log.setCpuPasses(7, passes);
  log.setCycle(7, cycle(16.7, 2.0), 1763154060.25);
  CHECK(lines.empty());

  log.setGpu(7, gpuFrame(9.0));
  CHECK(lines.size() == 1);
  CHECK(contains(lines[0], "frame 7 (late, game thread busy, GPU)"));
  CHECK(contains(lines[0], "1763154060.250000 s"));
  CHECK(contains(lines[0], "scene 3.50 | 0.00"));
  CHECK(contains(lines[0], "scene 0.00 | 4.50"));
}

TEST_CASE(spikeLogWritesAGpuOnlySpike) {
  std::vector<std::string> lines;
  VrSpikeLog log(8.33, true, [&] (const std::string& line) { lines.push_back(line); });

  log.setCpuPasses(3, { });
  log.setGpu(3, gpuFrame(8.5));
  log.setCycle(3, cycle(8.33, 3.0), 1.0);
  CHECK(lines.size() == 1);
  CHECK(contains(lines[0], "(GPU)"));
}

TEST_CASE(spikeLogGivesUpOnMissingGpuTimes) {
  std::vector<std::string> lines;
  VrSpikeLog log(8.33, true, [&] (const std::string& line) { lines.push_back(line); });

  log.setCpuPasses(1, { });
  log.setCycle(1, cycle(20.0, 0.5), 1.0);
  CHECK(lines.empty());

  // The record is reused by a later frame
  log.setCpuPasses(65, { });
  CHECK(lines.size() == 1);
  CHECK(contains(lines[0], "GPU not timed"));
}

TEST_CASE(spikeLogLimitsLinesPerWindow) {
  std::vector<std::string> lines;
  VrSpikeLog log(8.33, false, [&] (const std::string& line) { lines.push_back(line); });

  for (uint64_t id = 0; id < VrSpikeLog::WindowFrames; id++) {
    log.setCpuPasses(id, { });
    log.setCycle(id, cycle(id < 40 ? 20.0 : 8.33, 0.5), 1.0);
  }

  CHECK(lines.size() == VrSpikeLog::MaxLines + 1);
  CHECK(contains(lines.back(), "VR: Spikes: 10 more"));
}
