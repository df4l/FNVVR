#pragma once

#include <memory>
#include <string>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Creates the backend with the given name
   *
   * Known names are "emulator" and "openvr". The name is case insensitive.
   *
   * \param [in] name Backend name
   * \returns The backend, or \c nullptr if the name is unknown
   *    or the backend is not available in this build
   */
  std::unique_ptr<IVRBackend> vrCreateBackend(const std::string& name);

}
