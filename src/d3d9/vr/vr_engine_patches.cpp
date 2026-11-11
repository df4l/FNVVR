#include <cstring>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_engine_patch_table.h"
#include "vr_engine_patches.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    bool groupChosen(const std::string& groups, const char* name) {
      if (groups == "all")
        return true;

      size_t start = 0;

      while (start <= groups.size()) {
        size_t end = groups.find(',', start);

        if (end == std::string::npos)
          end = groups.size();

        size_t a = groups.find_first_not_of(' ', start);
        size_t b = groups.find_last_not_of(' ', end ? end - 1 : 0);

        if (a < end && b != std::string::npos && b >= a
         && groups.compare(a, b - a + 1, name) == 0)
          return true;

        start = end + 1;
      }

      return false;
    }


    bool unitMatches(const engine_patches::Unit& unit) {
      for (uint32_t i = 0; i < unit.rangeCount; i++) {
        const engine_patches::Range& range = engine_patches::Ranges[unit.firstRange + i];

        if (!VrGameMemory::matches(range.address, &engine_patches::Bytes[range.offset], range.size))
          return false;
      }

      return true;
    }


    bool applyUnit(const engine_patches::Unit& unit) {
      for (uint32_t i = 0; i < unit.rangeCount; i++) {
        const engine_patches::Range& range = engine_patches::Ranges[unit.firstRange + i];
        const uint8_t* original    = &engine_patches::Bytes[range.offset];
        const uint8_t* replacement = original + range.size;

        if (!VrGameMemory::replaceCode(range.address, original, replacement, range.size))
          return false;
      }

      return true;
    }

  }


  void VrEnginePatches::install(const std::string& groups) {
    using namespace engine_patches;

    uint32_t applied[GroupCount] = { };
    uint32_t found[GroupCount]   = { };
    uint32_t total[GroupCount]   = { };
    bool     chosen[GroupCount]  = { };

    for (uint32_t g = 0; g < GroupCount; g++)
      chosen[g] = groupChosen(groups, GroupNames[g]);

    // Every unit is checked before anything is written, so a unit whose
    // ranges overlap another one's cannot be judged on a half-written state
    bool matched[std::size(Units)] = { };

    for (size_t i = 0; i < std::size(Units); i++) {
      uint32_t g = uint32_t(Units[i].group);
      total[g]++;

      if (chosen[g])
        matched[i] = unitMatches(Units[i]);
    }

    for (size_t i = 0; i < std::size(Units); i++) {
      uint32_t g = uint32_t(Units[i].group);

      if (!matched[i]) {
        if (chosen[g])
          found[g]++;
        continue;
      }

      if (applyUnit(Units[i]))
        applied[g]++;
      else
        Logger::warn(str::format("VR: Engine patch at 0x", std::hex, Units[i].address, " was written only in part"));
    }

    uint32_t appliedAll = 0;
    uint32_t totalAll   = 0;

    for (uint32_t g = 0; g < GroupCount; g++) {
      appliedAll += applied[g];
      totalAll   += total[g];

      if (!chosen[g])
        Logger::info(str::format("VR: Engine patches '", GroupNames[g], "' turned off"));
      else if (found[g])
        Logger::info(str::format("VR: Engine patches '", GroupNames[g], "': ", applied[g], " of ", total[g],
          " applied, ", found[g], " left as found (already rewritten by a plugin or not the original executable)"));
    }

    Logger::info(str::format("VR: ", appliedAll, " of ", totalAll, " engine patches applied"));
  }

}
