#include "vr_cpu_samples.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace dxvk {

  namespace {

    const char* passName(VrRenderPass pass) {
      switch (pass) {
        case VrRenderPass::Eye:         return "rest";
        case VrRenderPass::Shadows:     return "shadows";
        case VrRenderPass::Water:       return "water";
        case VrRenderPass::Scene:       return "scene";
        case VrRenderPass::FirstPerson: return "first person";
        case VrRenderPass::ImageSpace:  return "image space";
        case VrRenderPass::Interface:   return "interface";
        case VrRenderPass::EyeCopy:     return "eye copy";
      }
      return "?";
    }

    /// ModR/M byte of a call through memory or a register: reg field 2
    bool isCallModRm(uint8_t modrm, uint8_t mod) {
      return (modrm >> 6) == mod && ((modrm >> 3) & 7) == 2;
    }

    std::string percent(uint32_t count, uint32_t total) {
      char text[16];
      std::snprintf(text, sizeof(text), "%.1f %%", total ? 100.0 * double(count) / double(total) : 0.0);
      return text;
    }

  }


  std::string vrCpuPhaseName(uint8_t phase) {
    switch (VrCpuPhase(phase)) {
      case VrCpuPhase::Game:  return "game";
      case VrCpuPhase::Wait:  return "wait";
      case VrCpuPhase::Frame: return "frame";
    }

    if (phase < VrCpuPhaseEyeBase)
      return "?";

    uint32_t index = phase - VrCpuPhaseEyeBase;
    uint32_t eye   = index / VrRenderPassCount;

    if (eye >= VrRenderProfile::EyeCount)
      return "?";

    return std::string(eye ? "right " : "left ") + passName(VrRenderPass(index % VrRenderPassCount));
  }


  bool vrFollowsCall(const uint8_t* before) {
    // call rel32
    if (before[2] == 0xE8)
      return true;

    // call reg (mod 3) or call [reg] (mod 0, without SIB or disp32)
    if (before[5] == 0xFF) {
      uint8_t modrm = before[6];
      uint8_t rm    = modrm & 7;

      if (isCallModRm(modrm, 3) || (isCallModRm(modrm, 0) && rm != 4 && rm != 5))
        return true;
    }

    // call [reg+disp8] or call [reg+reg*s]
    if (before[4] == 0xFF) {
      uint8_t modrm = before[5];
      uint8_t rm    = modrm & 7;

      if ((isCallModRm(modrm, 1) && rm != 4) || (isCallModRm(modrm, 0) && rm == 4))
        return true;
    }

    // call [sib+disp8]
    if (before[3] == 0xFF && isCallModRm(before[4], 1) && (before[4] & 7) == 4)
      return true;

    // call [reg+disp32] or call [abs32]
    if (before[1] == 0xFF) {
      uint8_t modrm = before[2];
      uint8_t rm    = modrm & 7;

      if ((isCallModRm(modrm, 2) && rm != 4) || (isCallModRm(modrm, 0) && rm == 5))
        return true;
    }

    // call [sib+disp32]
    if (before[0] == 0xFF && isCallModRm(before[1], 2) && (before[1] & 7) == 4)
      return true;

    return false;
  }


  void VrCpuModules::set(std::vector<Module> modules) {
    std::sort(modules.begin(), modules.end(),
      [] (const Module& a, const Module& b) { return a.base < b.base; });
    m_modules = std::move(modules);
  }


  uint32_t VrCpuModules::find(uint32_t address) const {
    auto next = std::upper_bound(m_modules.begin(), m_modules.end(), address,
      [] (uint32_t value, const Module& module) { return value < module.base; });

    if (next == m_modules.begin())
      return Unknown;

    const Module& module = *(next - 1);
    return address < module.end ? uint32_t(next - 1 - m_modules.begin()) : Unknown;
  }


  size_t VrCpuSampleHash::operator () (const VrCpuSample& sample) const {
    size_t hash = sample.eip;
    hash = hash * 0x9E3779B1u + sample.callers[0];
    hash = hash * 0x9E3779B1u + sample.callers[1];
    hash = hash * 0x9E3779B1u + sample.phase;
    return hash;
  }


  void VrCpuSampleTable::add(const VrCpuSample& sample) {
    m_counts[sample] += 1;
    m_total += 1;
  }


  void VrCpuSampleTable::reset() {
    m_counts.clear();
    m_total = 0;
  }


  std::string VrCpuSampleTable::describeShares(const VrCpuModules& modules, uint32_t maxModules) const {
    std::map<std::string, uint32_t> perModule;
    std::map<uint8_t, uint32_t>     perPhase;

    for (const auto& entry : m_counts) {
      uint32_t index = modules.find(entry.first.eip);
      perModule[index == VrCpuModules::Unknown ? std::string("other") : modules.modules()[index].name] += entry.second;
      perPhase[entry.first.phase] += entry.second;
    }

    std::vector<std::pair<std::string, uint32_t>> sorted(perModule.begin(), perModule.end());
    std::stable_sort(sorted.begin(), sorted.end(),
      [] (const auto& a, const auto& b) { return a.second > b.second; });

    std::string text = "modules: ";
    uint32_t rest = 0;

    for (size_t i = 0; i < sorted.size(); i++) {
      if (i < maxModules)
        text += (i ? ", " : "") + sorted[i].first + " " + percent(sorted[i].second, m_total);
      else
        rest += sorted[i].second;
    }

    if (rest)
      text += ", the others " + percent(rest, m_total);

    text += "; phases: ";
    bool first = true;

    for (const auto& phase : perPhase) {
      text += (first ? "" : ", ") + vrCpuPhaseName(phase.first) + " " + percent(phase.second, m_total);
      first = false;
    }

    return text;
  }

}
