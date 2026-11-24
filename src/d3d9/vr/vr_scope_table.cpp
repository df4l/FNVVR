#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include "vr_scope_table.h"

namespace dxvk {

  namespace {

    struct ShippedScope {
      const char* model;
      float x, y, z;
      float halfWidth, halfHeight;
      bool  rectangular;
    };

    // The opening's rear plane, or just in front of the eyepiece's glass where
    // it bulges towards the eye past that plane (anti-materiel, marksman,
    // varmint, .44, trail carbine, Lincoln's rifle). Discs have the radius of
    // the opening's rim.
    // A model with a size of 0 has nothing to look through: the K9000's lens
    // faces forward from the barrel.
    constexpr ShippedScope Scopes[] = {
      { "weapons\\2handrifle\\sniperrifle.nif",                              -2.692f, 11.843f,   0.009f, 1.475f, 1.475f, false },
      { "weapons\\2handrifle\\sniperrifleunique.nif",                        -2.692f, 11.843f,   0.009f, 1.475f, 1.475f, false },
      { "weapons\\2handrifle\\sniperriflesuppressor.nif",                    -2.692f, 11.843f,   0.009f, 1.475f, 1.475f, false },
      { "weapons\\2handrifle\\huntingrifle.nif",                              0.964f, 10.044f,   0.003f, 1.316f, 1.316f, false },
      { "weapons\\2handrifle\\huntingriflescope.nif",                         0.964f, 10.044f,   0.003f, 1.316f, 1.316f, false },
      { "nvdlc05\\weapons\\2handrifle\\nvdlc051stpersonweapuniquehuntingrifle.nif", 0.964f, 10.044f, 0.003f, 1.316f, 1.316f, false },
      { "weapons\\2handrifle\\laserrifle.nif",                                0.792f, 14.918f,  -0.154f, 2.177f, 1.068f, true  },
      { "weapons\\2handrifle\\laserrifleunique.nif",                          0.792f, 14.918f,  -0.154f, 2.177f, 1.068f, true  },
      { "weapons\\2handrifle\\varmintrifle.nif",                             -0.870f, 10.132f,   0.001f, 1.504f, 1.504f, false },
      { "weapons\\2handrifle\\varmintrifleunique.nif",                       -0.870f, 10.132f,   0.001f, 1.504f, 1.504f, false },
      { "weapons\\1handpistol\\huntingrevolver.nif",                          0.776f, 10.352f,  -0.024f, 1.128f, 1.128f, false },
      { "nvdlc05\\weapons\\1handpistol\\nvdlc05weapnvhuntingrevolvercustom.nif", 0.776f, 10.352f, -0.024f, 1.128f, 1.128f, false },
      { "weapons\\1handpistol\\44magnumrevolvernoscope.nif",                 -0.760f,  9.283f,   0.003f, 1.016f, 1.016f, false },
      { "weapons\\2handrifle\\antimaterialrifle.nif",                         0.170f, 10.347f,  -0.011f, 1.098f, 1.098f, false },
      { "weapons\\2handrifle\\trailcarbine.nif",                              2.270f,  8.957f,  -0.011f, 0.870f, 0.870f, false },
      { "weapons\\1handpistol\\9mm.nif",                                     -3.428f,  8.996f,  -0.001f, 1.067f, 1.067f, false },
      { "weapons\\2handrifle\\cowboyrepeaterunique.nif",                      4.300f,  8.119f,  -1.750f, 0.915f, 0.915f, false },
      { "weapons\\2handautomatic\\marksman.nif",                              1.060f,  9.982f,  -0.002f, 0.719f, 0.719f, false },
      { "weapons\\2handautomatic\\marksmanaa.nif",                            1.060f,  9.984f,  -0.004f, 0.738f, 0.738f, false },
      { "weapons\\2handrifle\\gaussrifle.nif",                                3.226f, 13.439f,  -0.386f, 1.260f, 1.260f, false },
      { "weapons\\2handrifle\\gaussrifleunique.nif",                          3.380f, 13.432f,  -0.392f, 1.295f, 1.295f, false },
      { "nvdlc05\\weapons\\2handautomatic\\nvdlc05bozarmachinegun.nif",      -4.016f, 12.913f,  -0.005f, 1.210f, 1.210f, false },
      { "nvdlc05\\weapons\\1handpistol\\plasmadefender05.nif",               -3.304f, 10.391f,  -0.029f, 0.984f, 0.984f, false },
      { "nvdlc01\\weapons\\2handrifle\\nvdlc01weapholorifle.nif",             8.528f, 11.378f,   0.217f, 0.804f, 0.804f, false },
      { "nvdlc04\\weapons\\2handlauncher\\nvdlc04redglare.nif",              -1.580f,  1.900f, -19.855f, 0.850f, 0.560f, true  },
      { "nvdlc03\\weapons\\2handhandle\\nvdlc03cyberdoggun.nif",              0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false },
      { "nvdlc03\\weapons\\2handhandle\\nvdlc03fidogun.nif",                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false },
    };

    constexpr char BinocularsModel[] = "weapons\\1handmelee\\binoculars.nif";
    constexpr char CameraModel[]     = "weapons\\1handpistol\\camera.nif";

    // Measured on the model's rings of vertices, findings/optics.md §13, in the
    // frame of the node holding them: along the view, left, up
    VrScopePlacement binocularsPlacement() {
      VrScopePlacement placement;
      placement.kind          = VrOpticKind::Binoculars;
      placement.eyepiece      = { -5.26f, -2.70f, 1.78f };
      placement.otherEyepiece = { -5.26f,  3.18f, 1.78f };
      placement.objective     = { 11.63f,  0.24f, 0.88f };
      placement.halfWidth     = 1.13f;
      placement.halfHeight    = 1.13f;
      return placement;
    }

    // The back of the body, just behind the model's rearmost vertices (x -5.31),
    // and the taking lens, the lower of the two on the front
    VrScopePlacement cameraPlacement() {
      VrScopePlacement placement;
      placement.kind        = VrOpticKind::Screen;
      placement.eyepiece    = { -5.42f, 1.6f, -7.6f };
      placement.objective   = { 1.6f, 0.15f, -7.35f };
      placement.halfWidth   = 3.0f;
      placement.halfHeight  = 2.25f;
      placement.rectangular = true;
      return placement;
    }

    std::string normalised(std::string path) {
      std::transform(path.begin(), path.end(), path.begin(), [] (unsigned char c) {
        return c == '/' ? '\\' : char(std::tolower(c));
      });

      return path;
    }

    std::string trimmed(const std::string& text) {
      constexpr char Blanks[] = " \t\r\n";
      size_t first = text.find_first_not_of(Blanks);

      if (first == std::string::npos)
        return std::string();

      return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
    }

  }


  size_t VrScopeTable::parse(const std::string& text) {
    size_t count = 0;
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
      line = trimmed(line);

      size_t separator = line.find('=');

      if (line.empty() || line[0] == ';' || separator == std::string::npos)
        continue;

      std::string model = normalised(trimmed(line.substr(0, separator)));
      std::string value = trimmed(line.substr(separator + 1));

      if (model.empty())
        continue;

      VrScopePlacement placement;

      if (normalised(value) != "none") {
        std::istringstream numbers(value);
        numbers.imbue(std::locale::classic());

        if (!(numbers >> placement.eyepiece.x >> placement.eyepiece.y >> placement.eyepiece.z >> placement.halfWidth))
          continue;

        if (numbers >> placement.halfHeight)
          placement.rectangular = true;
        else
          placement.halfHeight = placement.halfWidth;

        if (!placement.hasLens())
          continue;
      }

      m_corrections[model] = placement;
      count += 1;
    }

    return count;
  }


  bool VrScopeTable::find(const char* model, VrScopePlacement& placement) const {
    if (!model)
      return false;

    std::string key = normalised(model);
    auto correction = m_corrections.find(key);

    if (correction != m_corrections.end()) {
      placement = correction->second;
      return true;
    }

    if (key == BinocularsModel) {
      placement = binocularsPlacement();
      return true;
    }

    if (key == CameraModel) {
      placement = cameraPlacement();
      return true;
    }

    for (const ShippedScope& scope : Scopes) {
      if (key == scope.model) {
        placement.eyepiece    = { scope.x, scope.y, scope.z };
        placement.halfWidth   = scope.halfWidth;
        placement.halfHeight  = scope.halfHeight;
        placement.rectangular = scope.rectangular;
        return true;
      }
    }

    return false;
  }


  VrScopePlacement VrScopeTable::guess(const VrVector3& muzzle) {
    constexpr float AboveBarrel = 4.5f;
    constexpr float Radius      = 1.2f;

    VrScopePlacement placement;
    placement.eyepiece   = { 0.0f, muzzle.y + AboveBarrel, muzzle.z };
    placement.halfWidth  = Radius;
    placement.halfHeight = Radius;
    return placement;
  }

}
