#include <algorithm>
#include <cctype>

#include "vr_backend_factory.h"
#include "vr_emulator_backend.h"

namespace dxvk {

  std::unique_ptr<IVRBackend> vrCreateBackend(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
      [] (unsigned char c) { return char(std::tolower(c)); });

    if (lower == "emulator")
      return std::make_unique<VrEmulatorBackend>();

    return nullptr;
  }

}
