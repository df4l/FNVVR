#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_module_path.h"
#include "vr_openxr_loader.h"

namespace dxvk {

  VrOpenXrLoader::VrOpenXrLoader() {
    std::wstring directory = vrModuleDirectory();

    if (directory.empty()) {
      Logger::err("VR: Cannot determine the folder of d3d9.dll to load the OpenXR loader");
      return;
    }

    m_path = directory + L"fnvvr\\openxr_loader.dll";
    m_library = LoadLibraryW(m_path.c_str());

    if (!m_library) {
      Logger::err(str::format("VR: Failed to load ", str::fromws(m_path.c_str()),
        ", error ", GetLastError()));
      return;
    }

    m_getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
      GetProcAddress(m_library, "xrGetInstanceProcAddr"));

    if (!m_getInstanceProcAddr)
      Logger::err(str::format("VR: ", str::fromws(m_path.c_str()), " does not export xrGetInstanceProcAddr"));
  }


  VrOpenXrLoader::~VrOpenXrLoader() {
    if (m_library)
      FreeLibrary(m_library);
  }

}
