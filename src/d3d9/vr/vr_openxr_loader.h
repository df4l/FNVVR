#pragma once

#include <string>

#include <openxr/openxr.h>

struct HINSTANCE__;

namespace dxvk {

  /**
   * \brief The OpenXR loader, loaded at run time
   *
   * The 32-bit \c openxr_loader.dll ships in the \c fnvvr folder next to
   * d3d9.dll, and is loaded from there by its full path, so the build does
   * not link against it and nothing is loaded unless OpenXR is used. Every
   * other OpenXR function is reached through xrGetInstanceProcAddr.
   */
  class VrOpenXrLoader {

  public:

    VrOpenXrLoader();

    ~VrOpenXrLoader();

    VrOpenXrLoader(const VrOpenXrLoader&) = delete;
    VrOpenXrLoader& operator = (const VrOpenXrLoader&) = delete;

    /**
     * \brief Whether the loader was found and exports its entry point
     */
    bool isLoaded() const {
      return m_getInstanceProcAddr != nullptr;
    }

    /**
     * \brief Full path of the loader DLL, whether or not it was loaded
     */
    const std::wstring& path() const {
      return m_path;
    }

    /**
     * \brief Resolves an OpenXR function
     *
     * \param [in] instance Instance, or \c XR_NULL_HANDLE for the functions
     *    that are called before an instance exists
     * \param [in] name Function name
     * \returns The function, or \c nullptr if it is not available
     */
    template<typename Fn>
    Fn get(XrInstance instance, const char* name) const {
      PFN_xrVoidFunction function = nullptr;

      if (!m_getInstanceProcAddr || XR_FAILED(m_getInstanceProcAddr(instance, name, &function)))
        return nullptr;

      return reinterpret_cast<Fn>(function);
    }

  private:

    HINSTANCE__*                m_library             = nullptr;
    std::wstring                m_path;
    PFN_xrGetInstanceProcAddr   m_getInstanceProcAddr = nullptr;

  };

}
