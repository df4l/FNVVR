#include <algorithm>

#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_preview_window.h"

namespace dxvk {

  namespace {

    constexpr const wchar_t* WindowClassName = L"DxvkVrPreviewWindow";
    constexpr const wchar_t* WindowTitle     = L"VR preview (left eye | right eye)";

    // Width of the window's client area, the height follows the eye aspect ratio
    constexpr uint32_t PreviewWidth = 1000;

    LRESULT CALLBACK previewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
      // The swapchain needs the window, so closing it only hides it
      if (message == WM_CLOSE) {
        ShowWindow(window, SW_HIDE);
        return 0;
      }

      return DefWindowProcW(window, message, wParam, lParam);
    }

    bool registerWindowClass() {
      WNDCLASSW windowClass = { };
      windowClass.lpfnWndProc   = previewWindowProc;
      windowClass.hInstance     = GetModuleHandleW(nullptr);
      windowClass.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
      windowClass.lpszClassName = WindowClassName;

      return RegisterClassW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

  }


  VrPreviewWindow::VrPreviewWindow(IDirect3DDevice9* device, HWND window)
  : m_device(device), m_window(window) { }


  VrPreviewWindow::~VrPreviewWindow() {
    m_swapChain = nullptr;
    DestroyWindow(m_window);
  }


  std::unique_ptr<VrPreviewWindow> VrPreviewWindow::create(
          IDirect3DDevice9*     device,
    const VrExtent&             eyeExtent) {
    if (!eyeExtent.width || !eyeExtent.height || !registerWindowClass())
      return nullptr;

    uint32_t width  = PreviewWidth;
    uint32_t height = std::max(1u, uint32_t(uint64_t(width) * eyeExtent.height / (2u * eyeExtent.width)));

    RECT rect = { 0, 0, LONG(width), LONG(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    LONG windowWidth  = rect.right - rect.left;
    LONG windowHeight = rect.bottom - rect.top;

    // Right edge of the screen, so that the game window keeps its menus visible
    LONG left = std::max(0l, LONG(GetSystemMetrics(SM_CXSCREEN)) - windowWidth);

    // The game pauses when it loses the focus, so the preview must never take it
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE, WindowClassName, WindowTitle,
      WS_OVERLAPPEDWINDOW, left, 0, windowWidth, windowHeight,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!window)
      return nullptr;

    ShowWindow(window, SW_SHOWNA);

    std::unique_ptr<VrPreviewWindow> preview(new VrPreviewWindow(device, window));
    preview->m_width  = width;
    preview->m_height = height;

    D3DPRESENT_PARAMETERS params = { };
    params.BackBufferWidth        = width;
    params.BackBufferHeight       = height;
    params.BackBufferFormat       = D3DFMT_X8R8G8B8;
    params.BackBufferCount        = 1;
    params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow          = window;
    params.Windowed               = TRUE;
    params.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;

    if (FAILED(device->CreateAdditionalSwapChain(&params, &preview->m_swapChain))) {
      Logger::err("VR: Failed to create the preview swapchain");
      return nullptr;
    }

    Logger::info(str::format("VR: Preview window ", width, "x", height));
    return preview;
  }


  void VrPreviewWindow::present(IDirect3DTexture9* eyes[VrEyeCount]) {
    Com<IDirect3DSurface9> backBuffer;

    if (FAILED(m_swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
      return;

    const LONG half = LONG(m_width / 2);

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      Com<IDirect3DSurface9> source;

      if (FAILED(eyes[i]->GetSurfaceLevel(0, &source)))
        return;

      RECT target = { LONG(i) * half, 0, LONG(i + 1) * half, LONG(m_height) };
      m_device->StretchRect(source.ptr(), nullptr, backBuffer.ptr(), &target, D3DTEXF_LINEAR);
    }

    m_swapChain->Present(nullptr, nullptr, nullptr, nullptr, 0);
  }

}
