#include <windows.h>

#include "../../util/log/log.h"

#include "vr_game_window.h"

namespace dxvk {

  namespace {

    constexpr DWORD UnpacedFrameMs = 11;

  }


  bool VrGameWindow::hide(IDirect3DSwapChain9* swapchain) {
    D3DPRESENT_PARAMETERS params = { };

    if (FAILED(swapchain->GetPresentParameters(&params)) || !params.hDeviceWindow)
      return false;

    // The game draws into a child window when it is not in full screen
    HWND window = GetAncestor(params.hDeviceWindow, GA_ROOT);

    if (!window)
      return false;

    if (IsWindowVisible(window)) {
      Logger::info("VR: The game's window is hidden from the desktop");
      ShowWindow(window, SW_HIDE);
    }

    return !IsWindowVisible(window);
  }


  void VrGameWindow::waitUnpacedFrame() {
    Sleep(UnpacedFrameMs);
  }

}
