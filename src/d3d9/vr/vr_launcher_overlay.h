#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "vr_backend.h"
#include "vr_launcher_input.h"
#include "vr_menu_pointer.h"

namespace dxvk {

  /**
   * \brief Shows the game's launcher in the headset, driven by the lasers
   *
   * The launcher loads this DLL to list the adapters, and draws its windows
   * with GDI, without a D3D9 device. So a thread of this DLL keeps a VR
   * session without eyes: it shows pictures of the windows on a panel, and
   * posts the lasers to them as mouse messages. No code of the launcher is
   * changed, which makes this independent of the launcher's version.
   *
   * The session is the same kind as the game's, a scene application, which
   * also takes over from the desktop game theatre SteamVR shows for the
   * launcher otherwise. If VR cannot start, the launcher simply stays on
   * the desktop, where it can always be used with the mouse too. See
   * findings/launcher.md.
   */
  class VrLauncherOverlay {

  public:

    /**
     * \brief Whether this process is the game's launcher
     */
    static bool isLauncherProcess();

    /**
     * \param [in] backend Name of the VR backend
     * \param [in] distance Distance of the panel from the head, in metres
     * \param [in] menuPanelWidth Width of the game's menu panel, in metres
     */
    VrLauncherOverlay(const std::string& backend, float distance, float menuPanelWidth);

    /**
     * \brief Starts showing the launcher, on the first call
     *
     * Must be called on the launcher's thread that owns its windows, once
     * Vulkan's instance exists: the runtime is not safe to start during
     * that time. The launcher unloads the DLL when it is done with the
     * adapters, so the DLL keeps itself loaded from here on.
     */
    void start();

  private:

    std::string m_backendName;
    float       m_distance;
    float       m_menuPanelWidth;
    DWORD       m_windowThread = 0;
    bool        m_started      = false;

    static DWORD WINAPI threadMain(void* overlay);

    void run();

  };

}
