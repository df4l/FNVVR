#include <cmath>
#include <sstream>

#include "vr_performance_memory.h"

namespace dxvk {

  std::string VrPerformanceMemory::key(const std::string& model, double periodMs) {
    // '=' and line breaks would break the file's lines
    std::string name;

    for (char c : model)
      name += (c == '=' || c == '\n' || c == '\r') ? ' ' : c;

    long rate = periodMs > 0.0 ? std::lround(1000.0 / periodMs) : 0;
    return name + "@" + std::to_string(rate);
  }


  uint32_t VrPerformanceMemory::mostHeld(const double* ms, size_t count) {
    uint32_t best = 0;

    for (size_t i = 1; i < count; i++) {
      if (ms[i] > ms[best])
        best = uint32_t(i);
    }

    return best;
  }


  void VrPerformanceMemory::parse(const std::string& text) {
    m_entries.clear();

    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      size_t equals = line.rfind('=');

      if (line.empty() || line[0] == '#' || equals == std::string::npos || !equals)
        continue;

      std::istringstream values(line.substr(equals + 1));
      long distance = -1, gpu = -1;

      if (!(values >> distance >> gpu) || distance < 0 || gpu < 0)
        continue;

      store(line.substr(0, equals), VrPerformanceLevels { uint32_t(distance), uint32_t(gpu) });
    }
  }


  std::string VrPerformanceMemory::format() const {
    std::string text = "# Levels the performance mode settled at, per headset@rate: distance gpu. Written by the mod.\n";

    for (const auto& entry : m_entries) {
      text += entry.first + "=" + std::to_string(entry.second.distance)
            + " " + std::to_string(entry.second.gpu) + "\n";
    }

    return text;
  }


  bool VrPerformanceMemory::find(const std::string& key, VrPerformanceLevels& levels) const {
    for (const auto& entry : m_entries) {
      if (entry.first == key) {
        levels = entry.second;
        return true;
      }
    }

    return false;
  }


  bool VrPerformanceMemory::store(const std::string& key, const VrPerformanceLevels& levels) {
    for (auto& entry : m_entries) {
      if (entry.first == key) {
        if (entry.second == levels)
          return false;

        entry.second = levels;
        return true;
      }
    }

    m_entries.emplace_back(key, levels);
    return true;
  }

}
