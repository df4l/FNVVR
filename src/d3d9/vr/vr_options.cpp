#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "vr_options.h"

namespace dxvk {

  namespace {

    constexpr char SmoothTurnName[]      = "smoothturn";
    constexpr char SmoothTurnSpeedName[] = "smoothturnspeed";
    constexpr char PipBoyScaleName[]     = "pipboyscale";
    constexpr char ManualReloadName[]    = "manualreload";
    constexpr char PhysicalSneakName[]   = "physicalsneak";
    constexpr char RoomscaleName[]       = "roomscale";
    constexpr char ScopeLensName[]       = "scopelens";

    std::string trim(const std::string& text) {
      size_t begin = 0;
      size_t end   = text.size();

      while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        begin++;

      while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        end--;

      return text.substr(begin, end - begin);
    }


    std::string lower(std::string text) {
      std::transform(text.begin(), text.end(), text.begin(),
        [] (unsigned char c) { return char(std::tolower(c)); });
      return text;
    }


    bool parseBool(const std::string& text, bool& value) {
      if (text.empty())
        return false;

      char* end = nullptr;
      long number = std::strtol(text.c_str(), &end, 10);

      if (*end)
        return false;

      value = number != 0;
      return true;
    }


    // Reads a positive number, brought into [low, high]
    bool parseFloat(const std::string& text, float low, float high, float& value) {
      if (text.empty())
        return false;

      char* end = nullptr;
      double number = std::strtod(text.c_str(), &end);

      if (*end || !(number > 0.0))
        return false;

      value = std::min(std::max(float(number), low), high);
      return true;
    }


    // Position of a value on evenly spaced steps from min, the closest one
    uint32_t closestStep(float value, float min, float step, uint32_t count) {
      float position = (value - min) / step + 0.5f;

      if (!(position > 0.0f))
        return 0;

      return std::min(uint32_t(position), count - 1);
    }


    std::string formatFloat(float value) {
      char text[32];
      std::snprintf(text, sizeof(text), "%g", value);
      return text;
    }

  }


  uint32_t vrSmoothTurnSpeedToStep(float speed) {
    return closestStep(speed, VrSmoothTurnSpeedMin, VrSmoothTurnSpeedStep, VrSmoothTurnSpeedSteps);
  }


  float vrSmoothTurnStepToSpeed(uint32_t step) {
    return VrSmoothTurnSpeedMin + float(std::min(step, VrSmoothTurnSpeedSteps - 1)) * VrSmoothTurnSpeedStep;
  }


  uint32_t vrPipBoyScaleToStep(float scale) {
    return closestStep(scale, VrPipBoyScaleMin, VrPipBoyScaleStep, VrPipBoyScaleSteps);
  }


  float vrPipBoyStepToScale(uint32_t step) {
    return VrPipBoyScaleMin + float(std::min(step, VrPipBoyScaleSteps - 1)) * VrPipBoyScaleStep;
  }


  void vrParseOptions(const std::string& text, VrOptions& options) {
    size_t position = 0;

    while (position <= text.size()) {
      size_t lineEnd = text.find('\n', position);

      if (lineEnd == std::string::npos)
        lineEnd = text.size();

      std::string line = trim(text.substr(position, lineEnd - position));
      position = lineEnd + 1;

      if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
        continue;

      size_t separator = line.find('=');

      if (separator == std::string::npos)
        continue;

      std::string name  = lower(trim(line.substr(0, separator)));
      std::string value = trim(line.substr(separator + 1));

      if (name == SmoothTurnName)
        parseBool(value, options.smoothTurn);
      else if (name == SmoothTurnSpeedName)
        parseFloat(value, VrSmoothTurnSpeedMin, vrSmoothTurnStepToSpeed(VrSmoothTurnSpeedSteps - 1), options.smoothTurnSpeed);
      else if (name == PipBoyScaleName)
        parseFloat(value, VrPipBoyScaleMin, vrPipBoyStepToScale(VrPipBoyScaleSteps - 1), options.pipBoyScale);
      else if (name == ManualReloadName)
        parseBool(value, options.manualReload);
      else if (name == PhysicalSneakName)
        parseBool(value, options.physicalSneak);
      else if (name == ScopeLensName)
        parseBool(value, options.scopeLens);
      else if (name == RoomscaleName)
        parseBool(value, options.roomscale);
    }
  }


  std::string vrFormatOptions(const VrOptions& options) {
    std::string text = "; Written by the VR options page in the game's Settings menu.\n[VR]\n";
    text += "SmoothTurn=";
    text += options.smoothTurn ? "1" : "0";
    text += "\n";
    text += "SmoothTurnSpeed=" + formatFloat(options.smoothTurnSpeed) + "\n";
    text += "PipBoyScale=" + formatFloat(options.pipBoyScale) + "\n";
    text += "ManualReload=";
    text += options.manualReload ? "1" : "0";
    text += "\n";
    text += "PhysicalSneak=";
    text += options.physicalSneak ? "1" : "0";
    text += "\n";
    text += "ScopeLens=";
    text += options.scopeLens ? "1" : "0";
    text += "\n";
    text += "Roomscale=";
    text += options.roomscale ? "1" : "0";
    text += "\n";
    return text;
  }

}
