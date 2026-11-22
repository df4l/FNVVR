#include <algorithm>
#include <array>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_performance_levers.h"

namespace dxvk {

  namespace {

    constexpr char WaterReflectionsName[]  = "bUseWaterReflections:Water";
    constexpr char GrassStartFadeName[]    = "fGrassStartFadeDistance:Grass";
    constexpr char GrassMinStartFadeName[] = "fGrassMinStartFadeDistance:Grass";

    // Above the 0.5 under which the game stops updating grass
    constexpr float GrassFloorMargin = 1.0f;

    // fstp dword ptr [address]
    bool storesTo(uintptr_t site, uintptr_t address) {
      uint8_t code[6] = { 0xD9, 0x1D,
        uint8_t(address), uint8_t(address >> 8), uint8_t(address >> 16), uint8_t(address >> 24) };
      return VrGameMemory::matches(site, code, sizeof(code));
    }

    float* gameFloat(uintptr_t address) {
      return reinterpret_cast<float*>(address);
    }

    bool settingNamed(uintptr_t setting, const char* name) {
      if (!VrGameMemory::readable(setting, VrGame::SettingName + sizeof(uintptr_t)))
        return false;

      uintptr_t text = *reinterpret_cast<const uintptr_t*>(setting + VrGame::SettingName);
      size_t size = std::strlen(name) + 1;

      return VrGameMemory::readable(text, size)
          && !std::memcmp(reinterpret_cast<const void*>(text), name, size);
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

    bool grass = settingNamed(VrGame::SettingGrassStartFade, GrassStartFadeName)
              && settingNamed(VrGame::SettingGrassMinStartFade, GrassMinStartFadeName);

    if (grass) {
      Value value;
      value.address = gameFloat(VrGame::SettingGrassStartFade + VrGame::SettingValue);
      value.floor   = *gameFloat(VrGame::SettingGrassMinStartFade + VrGame::SettingValue) + GrassFloorMargin;
      m_distanceValues.push_back(value);
    }

    bool water = settingNamed(VrGame::SettingUseWaterReflections, WaterReflectionsName);

    m_waterReflections = Flag();

    if (water)
      m_waterReflections.address = reinterpret_cast<uint8_t*>(VrGame::SettingUseWaterReflections + VrGame::SettingValue);

    Logger::info(str::format("VR: Performance mode levers: object fades ", fades ? "found" : "not found",
      ", light fades ", lights ? "found" : "not found", ", shadow fades ", shadows ? "found" : "not found",
      ", specular fades ", specular ? "found" : "not found", ", grass ", grass ? "found" : "not found",
      ", water reflections ", water ? "found" : "not found"));

    return !m_distanceValues.empty() || !m_gpuValues.empty() || water;
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

    if (waterReflectionsOff(levels.distance))
      clear(m_waterReflections);
    else
      give(m_waterReflections);
  }


  void VrPerformanceLevers::restore() {
    for (Value& value : m_distanceValues)
      give(value);

    for (Value& value : m_gpuValues)
      give(value);

    give(m_waterReflections);
  }


  void VrPerformanceLevers::scale(Value& value, float factor) {
    float current = *value.address;

    // The settings menu wrote the player's new choice
    if (!value.held || current != value.written)
      value.base = current;

    value.written = std::max(value.base * factor, std::min(value.floor, value.base));
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


  void VrPerformanceLevers::clear(Flag& flag) {
    if (!flag.address)
      return;

    uint8_t current = *flag.address;

    // The settings menu wrote the player's new choice
    if (!flag.held || current != 0)
      flag.base = current;

    flag.held = true;
    *flag.address = 0;
  }


  void VrPerformanceLevers::give(Flag& flag) {
    if (!flag.address || !flag.held)
      return;

    if (*flag.address == 0)
      *flag.address = flag.base;

    flag.held = false;
  }

}
