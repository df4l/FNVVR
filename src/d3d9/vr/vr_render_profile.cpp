#include <algorithm>

#include "vr_render_profile.h"

namespace dxvk {

  VrRenderProfile::VrRenderProfile(uint32_t windowFrames)
  : m_windowFrames(std::max(windowFrames, 1u)) { }


  void VrRenderProfile::add(uint32_t eye, VrRenderPass pass, double ms) {
    if (eye >= EyeCount || m_frames >= m_windowFrames)
      return;

    m_current[eye][uint32_t(pass)] += ms;
  }


  bool VrRenderProfile::endFrame() {
    if (m_frames >= m_windowFrames)
      return true;

    for (uint32_t eye = 0; eye < EyeCount; eye++) {
      for (uint32_t pass = 0; pass < VrRenderPassCount; pass++) {
        double ms = m_current[eye][pass];
        PassStats& stats = m_stats[eye][pass];

        if (ms > 0.0) {
          stats.sum += ms;
          stats.max  = std::max(stats.max, ms);
          stats.frames += 1;
        }
      }
    }

    m_last    = m_current;
    m_current = { };
    m_frames += 1;
    return m_frames >= m_windowFrames;
  }


  void VrRenderProfile::reset() {
    *this = VrRenderProfile(m_windowFrames);
  }


  double VrRenderProfile::averageMs(uint32_t eye, VrRenderPass pass) const {
    return m_frames ? stats(eye, pass).sum / double(m_frames) : 0.0;
  }


  double VrRenderProfile::maxMs(uint32_t eye, VrRenderPass pass) const {
    return stats(eye, pass).max;
  }


  uint32_t VrRenderProfile::passFrames(uint32_t eye, VrRenderPass pass) const {
    return stats(eye, pass).frames;
  }


  double VrRenderProfile::otherMs(uint32_t eye) const {
    double other = averageMs(eye, VrRenderPass::Eye);

    for (uint32_t pass = uint32_t(VrRenderPass::Eye) + 1; pass < VrRenderPassCount; pass++)
      other -= averageMs(eye, VrRenderPass(pass));

    return std::max(other, 0.0);
  }

}
