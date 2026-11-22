#include "test.h"

#include "../vr_performance_governor.h"

using namespace dxvk;

namespace {

  // 72 Hz, the Steam Frame's rate
  constexpr double Period = 1000.0 / 72.0;

  VrPerformanceSample frame(double cpuMs, double gpuMs) {
    VrPerformanceSample sample;
    sample.periodMs = Period;
    sample.cpuMs    = cpuMs;
    sample.gpuMs    = gpuMs;
    return sample;
  }

  // Feeds the same frame for a time, in milliseconds of display
  void run(VrPerformanceGovernor& governor, const VrPerformanceSample& sample, double ms) {
    for (double t = 0.0; t < ms; t += sample.periodMs)
      governor.update(sample);
  }

  const VrPerformanceSample Light   = frame(6.0, 6.0);
  const VrPerformanceSample CpuHeavy = frame(20.0, 18.0);
  const VrPerformanceSample GpuHeavy = frame(8.0, 18.0);

}


TEST_CASE(performance_governor_keeps_the_settings_while_frames_fit) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, Light, 10000.0);
  CHECK(governor.levels().distance == 0);
  CHECK(governor.levels().gpu == 0);

  // Close to the budget is neither over nor calm
  run(governor, frame(12.5, 12.5), 10000.0);
  CHECK(governor.levels().distance == 0);
  CHECK(governor.levels().gpu == 0);
}


TEST_CASE(performance_governor_lowers_distances_for_a_late_cpu) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, CpuHeavy, 260.0);
  CHECK(governor.levels().distance == 1);

  // The GPU waited for the CPU: its long frames do not count
  CHECK(governor.levels().gpu == 0);

  run(governor, CpuHeavy, 5000.0);
  CHECK(governor.levels().distance == 4);
  CHECK(governor.levels().gpu == 0);
}


TEST_CASE(performance_governor_steps_down_on_a_quarter_of_late_frames) {
  VrPerformanceGovernor governor(4, 3);

  // One late frame in five: not enough
  for (uint32_t i = 0; i < 200; i++)
    governor.update(i % 5 == 0 ? CpuHeavy : Light);

  CHECK(governor.levels().distance == 0);

  // One in three
  for (uint32_t i = 0; i < 20; i++)
    governor.update(i % 3 == 0 ? CpuHeavy : Light);

  CHECK(governor.levels().distance >= 1);
}


TEST_CASE(performance_governor_uses_gpu_levers_before_distances) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, GpuHeavy, 260.0);
  CHECK(governor.levels().gpu == 1);
  CHECK(governor.levels().distance == 0);

  run(governor, GpuHeavy, 520.0);
  CHECK(governor.levels().gpu == 3);
  CHECK(governor.levels().distance == 0);

  // Every GPU lever is down: distances help the GPU too
  run(governor, GpuHeavy, 260.0);
  CHECK(governor.levels().distance == 1);
}


TEST_CASE(performance_governor_follows_the_runtime_reprojection_reason) {
  VrPerformanceGovernor governor(4, 3);

  VrPerformanceSample sample = Light;
  sample.gpuMs = -1.0;
  sample.gpuReprojected = true;
  run(governor, sample, 260.0);
  CHECK(governor.levels().gpu == 1);
  CHECK(governor.levels().distance == 0);

  sample.gpuReprojected = false;
  sample.cpuReprojected = true;
  run(governor, sample, 260.0);
  CHECK(governor.levels().distance == 1);
}


TEST_CASE(performance_governor_raises_a_level_after_two_calm_seconds) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, CpuHeavy, 260.0);
  CHECK(governor.levels().distance == 1);

  run(governor, Light, 1900.0);
  CHECK(governor.levels().distance == 1);

  run(governor, Light, 200.0);
  CHECK(governor.levels().distance == 0);
}


TEST_CASE(performance_governor_raises_a_level_despite_a_few_slow_frames) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, CpuHeavy, 260.0);
  CHECK(governor.levels().distance == 1);

  // Mostly light frames with a slower one now and then, as in play
  const VrPerformanceSample Slower = frame(12.5, 6.0);

  for (uint32_t i = 0; i < 3 * 72; i++)
    governor.update(i % 20 == 0 ? Slower : Light);

  CHECK(governor.levels().distance == 0);
}


TEST_CASE(performance_governor_waits_longer_after_a_relapse) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, CpuHeavy, 260.0);
  run(governor, Light, 2100.0);
  CHECK(governor.levels().distance == 0);

  // Late again right after the step up: the next one needs four seconds
  run(governor, CpuHeavy, 260.0);
  CHECK(governor.levels().distance == 1);

  run(governor, Light, 3000.0);
  CHECK(governor.levels().distance == 1);

  // Steps are taken at the end of a quarter (19 frames at 72 Hz)
  run(governor, Light, 1400.0);
  CHECK(governor.levels().distance == 0);
}


TEST_CASE(performance_governor_reset_goes_back_to_the_settings) {
  VrPerformanceGovernor governor(4, 3);
  run(governor, CpuHeavy, 1000.0);
  run(governor, GpuHeavy, 1000.0);
  CHECK(governor.levels().distance > 0);

  governor.reset();
  CHECK(governor.levels().distance == 0);
  CHECK(governor.levels().gpu == 0);
}
