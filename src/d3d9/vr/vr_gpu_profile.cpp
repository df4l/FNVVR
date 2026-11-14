#include <algorithm>
#include <iomanip>
#include <sstream>

#include "vr_gpu_profile.h"

namespace dxvk {

  namespace {

    std::string fixed(double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    }

  }


  double vrGpuTimestampMs(uint64_t begin, uint64_t end, uint64_t frequency) {
    if (!frequency || end < begin)
      return -1.0;

    return double(end - begin) * 1000.0 / double(frequency);
  }


  VrGpuProfile::VrGpuProfile(uint32_t windowFrames)
  : m_windowFrames(std::max(windowFrames, 1u)), m_eyes(m_windowFrames) { }


  void VrGpuProfile::add(VrGpuSection section, double ms) {
    if (ms <= 0.0 || m_eyes.frames() >= m_windowFrames)
      return;

    if (section.isPart())
      m_current[uint32_t(section.part())] += ms;
    else
      m_eyes.add(section.eye(), section.pass(), ms);
  }


  bool VrGpuProfile::endFrame() {
    if (m_eyes.frames() >= m_windowFrames)
      return true;

    for (uint32_t i = 0; i < VrGpuFramePartCount; i++) {
      m_parts[i].sum += m_current[i];
      m_parts[i].max  = std::max(m_parts[i].max, m_current[i]);
    }

    m_current = { };
    return m_eyes.endFrame();
  }


  void VrGpuProfile::reset() {
    *this = VrGpuProfile(m_windowFrames);
  }


  double VrGpuProfile::averageMs(VrGpuFramePart part) const {
    uint32_t count = frames();
    return count ? m_parts[uint32_t(part)].sum / double(count) : 0.0;
  }


  double VrGpuProfile::maxMs(VrGpuFramePart part) const {
    return m_parts[uint32_t(part)].max;
  }


  double VrGpuProfile::otherMs() const {
    double other = averageMs(VrGpuFramePart::Frame)
      - averageMs(VrGpuFramePart::Scope)
      - averageMs(VrGpuFramePart::Panels);

    for (uint32_t eye = 0; eye < VrRenderProfile::EyeCount; eye++)
      other -= m_eyes.averageMs(eye, VrRenderPass::Eye);

    return std::max(other, 0.0);
  }


  std::string VrGpuProfile::describe(uint32_t untimed) const {
    auto eyes = [this] (VrRenderPass pass) {
      return fixed(m_eyes.averageMs(0, pass)) + "/" + fixed(m_eyes.maxMs(0, pass)) + " | "
           + fixed(m_eyes.averageMs(1, pass)) + "/" + fixed(m_eyes.maxMs(1, pass));
    };

    auto part = [this] (VrGpuFramePart part) {
      return fixed(averageMs(part)) + "/" + fixed(maxMs(part));
    };

    std::ostringstream line;
    line << "VR: GPU time over " << frames() << " frames, average/longest ms."
         << " Whole frame " << part(VrGpuFramePart::Frame)
         << ", scope " << part(VrGpuFramePart::Scope)
         << ", panels " << part(VrGpuFramePart::Panels)
         << ", outside of these " << fixed(otherMs())
         << ". Left | right eye: whole eye " << eyes(VrRenderPass::Eye)
         << ", shadow maps " << eyes(VrRenderPass::Shadows)
         << ", water " << eyes(VrRenderPass::Water)
         << ", scene " << eyes(VrRenderPass::Scene)
         << ", first person " << eyes(VrRenderPass::FirstPerson)
         << ", image space " << eyes(VrRenderPass::ImageSpace)
         << ", interface " << eyes(VrRenderPass::Interface)
         << ", eye copy " << eyes(VrRenderPass::EyeCopy)
         << ", rest of the eye " << fixed(m_eyes.otherMs(0)) << " | " << fixed(m_eyes.otherMs(1))
         << ". Frames not timed: " << untimed;
    return line.str();
  }

}
