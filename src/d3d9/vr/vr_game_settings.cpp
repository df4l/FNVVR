#include <cstdint>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_settings.h"

namespace dxvk {

  namespace {

    enum class SettingType {
      Bool,
      Int,
      Float,
    };

    struct ForcedSetting {
      uintptr_t   setting;
      const char* name;
      SettingType type;
      int32_t     intValue;
      float       floatValue;
    };

    constexpr ForcedSetting Values[] = {
      { VrGame::SettingForceHighDetailReflections, "bForceHighDetailReflections:Water",                SettingType::Bool,  0,     0.0f   },
      { VrGame::SettingMultiThreadAudio,           "bMultiThreadAudio:Audio",                          SettingType::Bool,  1,     0.0f   },
      { VrGame::SettingUseAudioDebugInformation,   "bUseAudioDebugInformation:Audio",                  SettingType::Bool,  0,     0.0f   },
      { VrGame::SettingAudioCacheSize,             "iAudioCacheSize:Audio",                            SettingType::Int,   16384, 0.0f   },
      { VrGame::SettingMaxSizeForCachedSound,      "iMaxSizeForCachedSound:Audio",                     SettingType::Int,   2048,  0.0f   },
      { VrGame::SettingSelectivePurgeOnFastTravel, "bSelectivePurgeUnusedOnFastTravel:BackgroundLoad", SettingType::Bool,  1,     0.0f   },
      { VrGame::SettingBackgroundLoadLipFiles,     "bBackgroundLoadLipFiles:BackgroundLoad",           SettingType::Bool,  1,     0.0f   },
      { VrGame::SettingPreemptivelyUnloadCells,    "bPreemptivelyUnloadCells:General",                 SettingType::Bool,  1,     0.0f   },
      { VrGame::Setting30GrassVS,                  "b30GrassVS:Grass",                                 SettingType::Bool,  1,     0.0f   },
      { VrGame::SettingDoHighDynamicRange,         "bDoHighDynamicRange:BlurShaderHDR",                SettingType::Bool,  1,     0.0f   },
      { VrGame::SettingUseBlurShader,              "bUseBlurShader:BlurShader",                        SettingType::Bool,  0,     0.0f   },
      { VrGame::SettingLightEffectFadeDuration,    "fLightEffectFadeDuration:Pipboy",                  SettingType::Float, 0,     400.0f },
      { VrGame::SettingDrawShadows,                "bDrawShadows:Display",                             SettingType::Bool,  0,     0.0f   },
      { VrGame::SettingActorShadowCountInt,        "iActorShadowCountInt:Display",                     SettingType::Int,   0,     0.0f   },
      { VrGame::SettingActorShadowCountExt,        "iActorShadowCountExt:Display",                     SettingType::Int,   0,     0.0f   },
    };


    bool hasName(const ForcedSetting& value) {
      if (!VrGameMemory::readable(value.setting, VrGame::SettingName + sizeof(uintptr_t)))
        return false;

      uintptr_t name = *reinterpret_cast<const uintptr_t*>(value.setting + VrGame::SettingName);
      size_t size = std::strlen(value.name) + 1;

      return VrGameMemory::readable(name, size)
          && !std::memcmp(reinterpret_cast<const void*>(name), value.name, size);
    }


    void write(const ForcedSetting& value) {
      uintptr_t address = value.setting + VrGame::SettingValue;

      switch (value.type) {
        case SettingType::Bool: {
          auto current = reinterpret_cast<uint8_t*>(address);
          Logger::info(str::format("VR: Game setting ", value.name, ": ", uint32_t(*current), " -> ", value.intValue));
          *current = uint8_t(value.intValue);
        } break;

        case SettingType::Int: {
          auto current = reinterpret_cast<int32_t*>(address);
          Logger::info(str::format("VR: Game setting ", value.name, ": ", *current, " -> ", value.intValue));
          *current = value.intValue;
        } break;

        case SettingType::Float: {
          auto current = reinterpret_cast<float*>(address);
          Logger::info(str::format("VR: Game setting ", value.name, ": ", *current, " -> ", value.floatValue));
          *current = value.floatValue;
        } break;
      }
    }

  }


  bool VrGameSettings::fixAudioThreadExit() {
    constexpr size_t Size = sizeof(VrGame::AudioShutdownClearInitializedCode);

    const uint8_t nops[Size] = { 0x90, 0x90, 0x90, 0x90 };

    return VrGameMemory::replaceCode(VrGame::AudioShutdownClearInitialized,
      VrGame::AudioShutdownClearInitializedCode, nops, Size);
  }


  void VrGameSettings::keepActive() {
    const ForcedSetting value = { VrGame::SettingAlwaysActive, "bAlwaysActive:General", SettingType::Bool, 1, 0.0f };

    if (hasName(value))
      write(value);
    else
      Logger::warn(str::format("VR: Game setting ", value.name, " was not found, it keeps its value"));
  }


  void VrGameSettings::apply() {
    bool exitFixed = fixAudioThreadExit();

    for (const auto& value : Values) {
      if (value.setting == VrGame::SettingMultiThreadAudio && !exitFixed) {
        Logger::warn(str::format("VR: Game setting ", value.name, " keeps its value: the game could not exit with it"));
        continue;
      }

      if (hasName(value))
        write(value);
      else
        Logger::warn(str::format("VR: Game setting ", value.name, " was not found, it keeps its value"));
    }
  }

}
