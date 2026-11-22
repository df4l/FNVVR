#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <utility>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_performance_levers.h"

namespace dxvk {

  namespace {

    using SaveAllFn = bool (__fastcall*)(void* collection, void* unused, const char* path);

    VrPerformanceLevers* g_levers = nullptr;

    constexpr size_t SaveAllSites = std::size(VrGame::SettingsSaveAllCallSites);

    // The function each redirected call reached: the game's or another mod's
    // hook (johnnyguitar takes one of the console's two calls)
    std::array<uintptr_t, SaveAllSites> g_previousSaveAll = { };

    template<size_t Site>
    bool __fastcall saveSettingsHook(void* collection, void* unused, const char* path) {
      VrPerformanceLevers* levers = g_levers;

      if (levers)
        levers->lendSettings();

      bool saved = reinterpret_cast<SaveAllFn>(g_previousSaveAll[Site])(collection, nullptr, path);

      if (levers)
        levers->retakeSettings();

      return saved;
    }

    template<size_t... Sites>
    constexpr std::array<const void*, SaveAllSites> saveSettingsHooks(std::index_sequence<Sites...>) {
      return { reinterpret_cast<const void*>(&saveSettingsHook<Sites>)... };
    }

    constexpr char WaterReflectionsName[]  = "bUseWaterReflections:Water";
    constexpr char WaterDepthName[]        = "bUseWaterDepth:Water";
    constexpr char WaterRefractionsName[]  = "bUseWaterRefractions:Water";
    constexpr char GrassStartFadeName[]    = "fGrassStartFadeDistance:Grass";
    constexpr char GrassMinStartFadeName[] = "fGrassMinStartFadeDistance:Grass";

    // Above the 0.5 under which the game stops updating grass
    constexpr float GrassFloorMargin = 1.0f;

    constexpr float SpecularOffStart = 0.0f;
    constexpr float SpecularOffEnd   = 1.0f;

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


  VrPerformanceLevers::~VrPerformanceLevers() {
    // The redirected calls stay in place and fall through to the game's function
    if (g_levers == this)
      g_levers = nullptr;
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

    m_specularStart = Value();
    m_specularEnd   = Value();

    if (specular) {
      m_specularStart.address = gameFloat(VrGame::SpecularLodStartFade);
      m_specularEnd.address   = gameFloat(VrGame::SpecularLodEnd);
    }

    bool grass = settingNamed(VrGame::SettingGrassStartFade, GrassStartFadeName)
              && settingNamed(VrGame::SettingGrassMinStartFade, GrassMinStartFadeName);

    if (grass) {
      Value value;
      value.address = gameFloat(VrGame::SettingGrassStartFade + VrGame::SettingValue);
      value.floor   = *gameFloat(VrGame::SettingGrassMinStartFade + VrGame::SettingValue) + GrassFloorMargin;
      value.setting = true;
      m_distanceValues.push_back(value);
    }

    bool water = settingNamed(VrGame::SettingUseWaterReflections, WaterReflectionsName);
    bool depth = settingNamed(VrGame::SettingUseWaterDepth, WaterDepthName)
              && settingNamed(VrGame::SettingUseWaterRefractions, WaterRefractionsName);

    m_waterReflections  = Flag();
    m_waterDepth        = Flag();
    m_waterRefractions  = Flag();

    if (water)
      m_waterReflections.address = reinterpret_cast<uint8_t*>(VrGame::SettingUseWaterReflections + VrGame::SettingValue);

    if (depth) {
      m_waterDepth.address       = reinterpret_cast<uint8_t*>(VrGame::SettingUseWaterDepth + VrGame::SettingValue);
      m_waterRefractions.address = reinterpret_cast<uint8_t*>(VrGame::SettingUseWaterRefractions + VrGame::SettingValue);
    }

    // A game setting the game could save lowered is not a lever
    if ((grass || water || depth) && !hookSettingsSaves()) {
      if (grass)
        m_distanceValues.pop_back();

      grass = false;
      water = false;
      depth = false;
      m_waterReflections = Flag();
      m_waterDepth       = Flag();
      m_waterRefractions = Flag();
    }

    Logger::info(str::format("VR: Performance mode levers: object fades ", fades ? "found" : "not found",
      ", light fades ", lights ? "found" : "not found", ", shadow fades ", shadows ? "found" : "not found",
      ", specular fades ", specular ? "found" : "not found", ", grass ", grass ? "found" : "not found",
      ", water reflections ", water ? "found" : "not found",
      ", water depth maps and refractions ", depth ? "found" : "not found"));

    return !m_distanceValues.empty() || !m_gpuValues.empty() || specular || water;
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

    for (Value* value : { &m_specularStart, &m_specularEnd }) {
      if (!value->address)
        continue;

      // Specular highlights fade out from the start to the end distance:
      // one game unit leaves none (BSShaderPPLightingProperty::GetSpecularLODFade)
      if (stereoReplay(levels.distance))
        hold(*value, value == &m_specularStart ? SpecularOffStart : SpecularOffEnd);
      else if (levels.gpu)
        scale(*value, gpuScale(levels.gpu));
      else
        give(*value);
    }

    if (waterReflectionsOff(levels.distance))
      clear(m_waterReflections);
    else
      give(m_waterReflections);

    // Depth maps are drawn in the left eye's screen space before the
    // replayed commands; the refraction copy of the screen shows the left
    // eye's world under the water in some replayed frames
    for (Flag* flag : { &m_waterDepth, &m_waterRefractions }) {
      if (stereoReplay(levels.distance))
        clear(*flag);
      else
        give(*flag);
    }
  }


  void VrPerformanceLevers::restore() {
    for (Value& value : m_distanceValues)
      give(value);

    for (Value& value : m_gpuValues)
      give(value);

    if (m_specularStart.address)
      give(m_specularStart);

    if (m_specularEnd.address)
      give(m_specularEnd);

    give(m_waterReflections);
    give(m_waterDepth);
    give(m_waterRefractions);
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


  void VrPerformanceLevers::hold(Value& value, float target) {
    float current = *value.address;

    // The settings menu wrote the player's new choice
    if (!value.held || current != value.written)
      value.base = current;

    value.written = target;
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


  bool VrPerformanceLevers::hookSettingsSaves() {
    if (m_hooked)
      return true;

    if (g_levers)
      return false;

    // Each call keeps the target it had, so a mod's hook on one of them stays
    const auto hooks = saveSettingsHooks(std::make_index_sequence<SaveAllSites>());
    bool chainable = true;

    for (uintptr_t site : VrGame::SettingsSaveAllCallSites)
      chainable = chainable && VrGameMemory::canChainCall(site, VrGame::SettingsSaveAll);

    for (size_t i = 0; chainable && i < SaveAllSites; i++) {
      chainable = VrGameMemory::chainCall(VrGame::SettingsSaveAllCallSites[i],
        VrGame::SettingsSaveAll, hooks[i], &g_previousSaveAll[i]);
    }

    if (!chainable) {
      Logger::warn("VR: The game's INI saves were not found, the performance mode leaves water reflections, depth maps, refractions and grass alone");
      return false;
    }

    g_levers = this;
    m_hooked = true;
    return true;
  }


  void VrPerformanceLevers::lendSettings() {
    for (Value& value : m_distanceValues) {
      if (value.setting && value.held && *value.address == value.written) {
        *value.address = value.base;
        value.lent = true;
      }
    }

    for (Flag* flag : { &m_waterReflections, &m_waterDepth, &m_waterRefractions }) {
      if (flag->address && flag->held && *flag->address == 0) {
        *flag->address = flag->base;
        flag->lent = true;
      }
    }
  }


  void VrPerformanceLevers::retakeSettings() {
    for (Value& value : m_distanceValues) {
      if (value.lent && *value.address == value.base)
        *value.address = value.written;

      value.lent = false;
    }

    for (Flag* flag : { &m_waterReflections, &m_waterDepth, &m_waterRefractions }) {
      if (flag->lent && *flag->address == flag->base)
        *flag->address = 0;

      flag->lent = false;
    }
  }

}
