#include <algorithm>
#include <array>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_performance_levers.h"

namespace dxvk {

  namespace {

    constexpr std::array<float, VrPerformanceLevers::DistanceSteps + 1> DistanceScales = { 1.0f, 0.85f, 0.72f, 0.6f, 0.5f };
    constexpr std::array<float, VrPerformanceLevers::GpuSteps + 1>      GpuScales      = { 1.0f, 0.7f, 0.5f, 0.35f };

    // fstp dword ptr [address]
    bool storesTo(uintptr_t site, uintptr_t address) {
      uint8_t code[6] = { 0xD9, 0x1D,
        uint8_t(address), uint8_t(address >> 8), uint8_t(address >> 16), uint8_t(address >> 24) };
      return VrGameMemory::matches(site, code, sizeof(code));
    }

    float* gameFloat(uintptr_t address) {
      return reinterpret_cast<float*>(address);
    }

  }


  bool VrPerformanceLevers::initialize() {
    m_distanceValues.clear();
    m_gpuValues.clear();

    bool fades = VrGameMemory::matches(VrGame::SetFadeOutMultiplier,
        VrGame::SetFadeOutMultiplierCode, sizeof(VrGame::SetFadeOutMultiplierCode))
      && VrGameMemory::matches(VrGame::GetFadeOutMultiplier,
        VrGame::GetFadeOutMultiplierCode, sizeof(VrGame::GetFadeOutMultiplierCode));

    if (fades) {
      float* multipliers = gameFloat(VrGame::FadeOutMultipliers);

      m_distanceValues.push_back({ multipliers + VrGame::FadeOutMultiplierItems });
      m_distanceValues.push_back({ multipliers + VrGame::FadeOutMultiplierActors });

      for (uint32_t index : VrGame::FadeOutMultiplierObjects)
        m_distanceValues.push_back({ multipliers + index });
    }

    bool lights = storesTo(VrGame::LightLodStartFadeStore, VrGame::LightLodStartFade)
      && storesTo(VrGame::LightLodEndStore, VrGame::LightLodEnd);

    if (lights) {
      m_distanceValues.push_back({ gameFloat(VrGame::LightLodStartFade) });
      m_distanceValues.push_back({ gameFloat(VrGame::LightLodEnd) });
    }

    bool shadows = storesTo(VrGame::ShadowLodStartFadeStore, VrGame::ShadowLodStartFade)
      && storesTo(VrGame::ShadowLodEndStore, VrGame::ShadowLodEnd);

    if (shadows) {
      m_gpuValues.push_back({ gameFloat(VrGame::ShadowLodStartFade) });
      m_gpuValues.push_back({ gameFloat(VrGame::ShadowLodEnd) });
    }

    bool specular = storesTo(VrGame::SpecularLodStartFadeStore, VrGame::SpecularLodStartFade)
      && storesTo(VrGame::SpecularLodEndStore, VrGame::SpecularLodEnd);

    if (specular) {
      m_gpuValues.push_back({ gameFloat(VrGame::SpecularLodStartFade) });
      m_gpuValues.push_back({ gameFloat(VrGame::SpecularLodEnd) });
    }

    Logger::info(str::format("VR: Performance mode levers: object fades ", fades ? "found" : "not found",
      ", light fades ", lights ? "found" : "not found", ", shadow fades ", shadows ? "found" : "not found",
      ", specular fades ", specular ? "found" : "not found"));

    return !m_distanceValues.empty() || !m_gpuValues.empty();
  }


  void VrPerformanceLevers::apply(const VrPerformanceLevels& levels) {
    for (Value& value : m_distanceValues) {
      if (levels.distance)
        scale(value, distanceScale(levels.distance));
      else
        give(value);
    }

    for (Value& value : m_gpuValues) {
      if (levels.gpu)
        scale(value, gpuScale(levels.gpu));
      else
        give(value);
    }
  }


  void VrPerformanceLevers::restore() {
    for (Value& value : m_distanceValues)
      give(value);

    for (Value& value : m_gpuValues)
      give(value);
  }


  float VrPerformanceLevers::distanceScale(uint32_t level) {
    return DistanceScales[std::min<size_t>(level, DistanceScales.size() - 1)];
  }


  float VrPerformanceLevers::gpuScale(uint32_t level) {
    return GpuScales[std::min<size_t>(level, GpuScales.size() - 1)];
  }


  void VrPerformanceLevers::scale(Value& value, float factor) {
    float current = *value.address;

    // The settings menu wrote the player's new choice
    if (!value.held || current != value.written)
      value.base = current;

    value.written = value.base * factor;
    value.held    = true;
    *value.address = value.written;
  }


  void VrPerformanceLevers::give(Value& value) {
    if (!value.held)
      return;

    if (*value.address == value.written)
      *value.address = value.base;

    value.held = false;
  }

}
