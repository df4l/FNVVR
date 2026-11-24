#include "test.h"

#include "../vr_gpu_profile.h"

using namespace dxvk;

TEST_CASE(gpuTimestampMsUsesTheFrequency) {
  CHECK_NEAR(vrGpuTimestampMs(1000, 3000, 1000000), 2.0, 1e-9);
  CHECK(vrGpuTimestampMs(3000, 1000, 1000000) < 0.0);
  CHECK(vrGpuTimestampMs(1000, 3000, 0) < 0.0);
}

TEST_CASE(gpuSectionKeepsEyeAndPass) {
  VrGpuSection section = VrGpuSection::pass(1, VrRenderPass::EyeCopy);
  CHECK(!section.isPart());
  CHECK(section.eye() == 1);
  CHECK(section.pass() == VrRenderPass::EyeCopy);

  VrGpuSection part = VrGpuSection::part(VrGpuFramePart::Panels);
  CHECK(part.isPart());
  CHECK(part.part() == VrGpuFramePart::Panels);

  VrGpuSection first = VrGpuSection::pass(0, VrRenderPass::Eye);
  CHECK(!first.isPart());
  CHECK(first.eye() == 0);
  CHECK(first.pass() == VrRenderPass::Eye);
}

TEST_CASE(gpuProfileSplitsTheFrame) {
  VrGpuProfile profile(2);

  profile.add(VrGpuSection::part(VrGpuFramePart::Frame), 8.0);
  profile.add(VrGpuSection::part(VrGpuFramePart::Scope), 1.0);
  profile.add(VrGpuSection::part(VrGpuFramePart::Panels), 0.5);
  profile.add(VrGpuSection::pass(0, VrRenderPass::Eye), 3.0);
  profile.add(VrGpuSection::pass(0, VrRenderPass::Scene), 2.0);
  profile.add(VrGpuSection::pass(1, VrRenderPass::Eye), 3.0);
  profile.add(VrGpuSection::pass(1, VrRenderPass::Interface), 0.25);
  profile.add(VrGpuSection::pass(1, VrRenderPass::Interface), 0.25);
  CHECK(!profile.endFrame());

  profile.add(VrGpuSection::part(VrGpuFramePart::Frame), 6.0);
  profile.add(VrGpuSection::pass(0, VrRenderPass::Eye), 3.0);
  profile.add(VrGpuSection::pass(1, VrRenderPass::Eye), 3.0);
  CHECK(profile.endFrame());

  CHECK(profile.frames() == 2);
  CHECK_NEAR(profile.averageMs(VrGpuFramePart::Frame), 7.0, 1e-9);
  CHECK_NEAR(profile.maxMs(VrGpuFramePart::Frame), 8.0, 1e-9);
  CHECK_NEAR(profile.averageMs(VrGpuFramePart::Scope), 0.5, 1e-9);
  CHECK_NEAR(profile.otherMs(), 7.0 - 0.5 - 0.25 - 6.0, 1e-9);
  CHECK_NEAR(profile.eyes().averageMs(1, VrRenderPass::Interface), 0.25, 1e-9);
  CHECK_NEAR(profile.eyes().maxMs(1, VrRenderPass::Interface), 0.5, 1e-9);
  CHECK_NEAR(profile.eyes().otherMs(0), 2.0, 1e-9);
}

TEST_CASE(gpuProfileStopsAtAFullWindow) {
  VrGpuProfile profile(1);

  profile.add(VrGpuSection::part(VrGpuFramePart::Frame), 5.0);
  CHECK(profile.endFrame());

  profile.add(VrGpuSection::part(VrGpuFramePart::Frame), 50.0);
  CHECK(profile.endFrame());
  CHECK_NEAR(profile.maxMs(VrGpuFramePart::Frame), 5.0, 1e-9);

  profile.reset();
  CHECK(profile.frames() == 0);
  CHECK(profile.describe(3).find("Frames not timed: 3") != std::string::npos);
}
