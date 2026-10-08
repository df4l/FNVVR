#include <algorithm>
#include <cctype>

#include "vr_backend_factory.h"
#include "vr_emulator_backend.h"
#include "vr_openvr_backend.h"

namespace dxvk {

  std::unique_ptr<IVRBackend> vrCreateBackend(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
      [] (unsigned char c) { return char(std::tolower(c)); });

    if (lower == "emulator")
      return std::make_unique<VrEmulatorBackend>();

    if (lower == "openvr")
      return std::make_unique<VrOpenVrBackend>();

    return nullptr;
  }

}
