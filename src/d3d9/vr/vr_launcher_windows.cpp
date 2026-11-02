#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

#include "../../util/log/log.h"
#include "../../util/thread.h"
#include "../../util/util_string.h"

#include "vr_launcher_windows.h"

namespace dxvk {

  namespace {

    /// How long a window has to answer, in milliseconds
    constexpr UINT AnswerTimeoutMs = 100;

    /// Alpha of an opaque pixel of the bitmap
    constexpr uint32_t OpaqueAlpha = 0xff000000u;


    bool ask(HWND window, UINT message, WPARAM wparam, LPARAM lparam, DWORD_PTR* result = nullptr) {
      DWORD_PTR ignored = 0;

      return SendMessageTimeoutW(window, message, wparam, lparam,
        SMTO_ABORTIFHUNG, AnswerTimeoutMs, result ? result : &ignored) != 0;
    }


    BOOL CALLBACK collectWindow(HWND window, LPARAM list) {
      if (IsWindowVisible(window) && !IsIconic(window))
        reinterpret_cast<std::vector<HWND>*>(list)->push_back(window);

      return TRUE;
    }


    /**
     * \brief The launcher's shown top level windows, top most first
     */
    std::vector<HWND> shownWindows(DWORD thread) {
      std::vector<HWND> windows;
      EnumThreadWindows(thread, collectWindow, reinterpret_cast<LPARAM>(&windows));
      return windows;
    }


    /**
     * \brief The shown children of a window, top most first
     */
    std::vector<HWND> shownChildren(HWND window) {
      std::vector<HWND> children;

      for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (IsWindowVisible(child))
          children.push_back(child);
      }

      return children;
    }


    bool isGroupBox(HWND window) {
      WCHAR name[16] = { };
      GetClassNameW(window, name, 16);

      return !_wcsicmp(name, L"Button")
          && (GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX;
    }


    /**
     * \brief The largest window without an owner: the launcher's main dialog
     */
    HWND mainWindow(const std::vector<HWND>& windows) {
      HWND result = nullptr;
      LONG largest = 0;

      for (HWND window : windows) {
        RECT rect = { };

        if (GetWindow(window, GW_OWNER) || !GetWindowRect(window, &rect))
          continue;

        LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);

        if (area > largest) {
          largest = area;
          result  = window;
        }
      }

      return result;
    }


    bool contains(const RECT& rect, const POINT& point) {
      return point.x >= rect.left && point.x < rect.right
          && point.y >= rect.top  && point.y < rect.bottom;
    }


    LPARAM pointParam(LONG x, LONG y) {
      return MAKELPARAM(short(x), short(y));
    }


    /**
     * \brief The deepest child under a point that takes the mouse
     *
     * Static texts and group boxes let the mouse through to what is below,
     * which is how the launcher's main dialog gets the clicks on its
     * buttons, which are pictures.
     */
    HWND childAt(HWND window, const POINT& point) {
      for (HWND child : shownChildren(window)) {
        RECT rect = { };

        if (!IsWindowEnabled(child) || !GetWindowRect(child, &rect) || !contains(rect, point))
          continue;

        DWORD_PTR hit = HTNOWHERE;

        if (!ask(child, WM_NCHITTEST, 0, pointParam(point.x, point.y), &hit) || LRESULT(hit) == HTTRANSPARENT)
          continue;

        HWND deeper = childAt(child, point);
        return deeper ? deeper : child;
      }

      return nullptr;
    }


    BOOL CALLBACK takeFirstWindow(HWND window, LPARAM result) {
      *reinterpret_cast<HWND*>(result) = window;
      return FALSE;
    }


    // A hook procedure takes no argument of ours, so this has to be global.
    // The work is only reachable under the lock, so that a message delivered
    // after its sender gave up finds nothing to do.
    HHOOK                       g_threadCallHook    = nullptr;
    UINT                        g_threadCallMessage = 0;
    dxvk::mutex                 g_threadCallLock;
    const std::function<void()>* g_threadCallWork   = nullptr;

    LRESULT CALLBACK threadCallHook(int code, WPARAM wparam, LPARAM lparam) {
      if (code == HC_ACTION && reinterpret_cast<const CWPSTRUCT*>(lparam)->message == g_threadCallMessage) {
        std::lock_guard<dxvk::mutex> lock(g_threadCallLock);

        if (g_threadCallWork) {
          (*g_threadCallWork)();
          g_threadCallWork = nullptr;
        }
      }

      return CallNextHookEx(nullptr, code, wparam, lparam);
    }


    std::string describe(HWND window) {
      WCHAR name[64] = { };
      GetClassNameW(window, name, 64);
      return str::format(str::fromws(name), " ", GetDlgCtrlID(window));
    }

  }


  void VrLauncherThreadCall::install() {
    g_threadCallMessage = RegisterWindowMessageW(L"FNVVR.LauncherThreadCall");

    if (g_threadCallMessage)
      g_threadCallHook = SetWindowsHookExW(WH_CALLWNDPROC, threadCallHook, nullptr, GetCurrentThreadId());

    if (!g_threadCallHook)
      Logger::warn("VR: Launcher: The hook on its thread could not be set, its panel will be slow");
  }


  VrLauncherThreadCall::VrLauncherThreadCall(DWORD thread)
  : m_thread(thread) { }


  bool VrLauncherThreadCall::run(const std::function<void()>& work) {
    HWND window = nullptr;
    EnumThreadWindows(m_thread, takeFirstWindow, reinterpret_cast<LPARAM>(&window));

    if (!g_threadCallHook || !window) {
      work();
      return true;
    }

    { std::lock_guard<dxvk::mutex> lock(g_threadCallLock);
      g_threadCallWork = &work;
    }

    ask(window, g_threadCallMessage, 0, 0);

    std::lock_guard<dxvk::mutex> lock(g_threadCallLock);
    bool done = !g_threadCallWork;
    g_threadCallWork = nullptr;
    return done;
  }


  VrLauncherCapture::Surface::Surface() {
    BITMAPINFO info = { };
    info.bmiHeader.biSize        = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth       = VrLauncherCanvasWidth;
    info.bmiHeader.biHeight      = -VrLauncherCanvasHeight;
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    dc     = CreateCompatibleDC(nullptr);
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    pixels = reinterpret_cast<uint32_t*>(bits);

    if (dc && bitmap)
      oldBitmap = SelectObject(dc, bitmap);
    else
      Logger::err("VR: Launcher: The bitmap for its image could not be created");
  }


  VrLauncherCapture::Surface::~Surface() {
    if (oldBitmap)
      SelectObject(dc, oldBitmap);

    if (bitmap)
      DeleteObject(bitmap);

    if (dc)
      DeleteDC(dc);
  }


  VrLauncherCapture::VrLauncherCapture(DWORD thread)
  : m_thread(thread) {
    size_t count = size_t(VrLauncherCanvasWidth) * size_t(VrLauncherCanvasHeight);
    m_previous.resize(count);
    m_rgba.resize(count * sizeof(uint32_t));
  }


  bool VrLauncherCapture::update() {
    if (!m_canvasSurface.pixels || !m_windowSurface.pixels)
      return false;

    std::vector<HWND> windows = shownWindows(m_thread);
    HWND main = mainWindow(windows);

    m_hasWindows = main != nullptr;

    if (!main)
      return false;

    RECT rect = { };
    GetWindowRect(main, &rect);
    m_canvas = vrLauncherCanvas(VrScreenRect { rect.left, rect.top, rect.right, rect.bottom });

    size_t count = m_previous.size();
    GdiFlush();
    std::memset(m_canvasSurface.pixels, 0, count * sizeof(uint32_t));

    // From the bottom up, as the desktop stacks them
    for (auto window = windows.rbegin(); window != windows.rend(); window++) {
      RECT canvas = { m_canvas.left, m_canvas.top, m_canvas.right, m_canvas.bottom };

      if (!paintWindow(*window, canvas))
        return false;

      makeOpaque(*window);
    }

    GdiFlush();

    if (!std::memcmp(m_canvasSurface.pixels, m_previous.data(), count * sizeof(uint32_t)))
      return false;

    std::memcpy(m_previous.data(), m_canvasSurface.pixels, count * sizeof(uint32_t));

    for (size_t i = 0; i < count; i++) {
      uint32_t bgra = m_canvasSurface.pixels[i];
      m_rgba[i * 4 + 0] = uint8_t(bgra >> 16);
      m_rgba[i * 4 + 1] = uint8_t(bgra >> 8);
      m_rgba[i * 4 + 2] = uint8_t(bgra);
      m_rgba[i * 4 + 3] = uint8_t(bgra >> 24);
    }

    return true;
  }


  bool VrLauncherCapture::paintWindow(HWND window, const RECT& clip) {
    RECT frame = { };
    GetWindowRect(window, &frame);

    // What Wine does not draw of a frame stays in the dialogs' colour
    HDC dc = m_windowSurface.dc;
    RECT area = { 0, 0, frame.right - frame.left, frame.bottom - frame.top };
    FillRect(dc, &area, GetSysColorBrush(COLOR_BTNFACE));

    if (!ask(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_NONCLIENT))
      return false;

    copyToCanvas(frame, clip);

    POINT origin = { };
    RECT  client = { };
    ClientToScreen(window, &origin);
    GetClientRect(window, &client);

    // Wine's controls clip themselves as if the bitmap started at their own
    // corner, so that is where they draw
    int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, 0, client.right, client.bottom);

    WPARAM wparam = reinterpret_cast<WPARAM>(dc);
    bool answered = ask(window, WM_ERASEBKGND, wparam, 0)
                 && ask(window, WM_PRINTCLIENT, wparam, PRF_CLIENT);

    RestoreDC(dc, saved);

    if (!answered)
      return false;

    RECT clientRect = { origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
    copyToCanvas(clientRect, clip);

    RECT childClip = { };

    if (!IntersectRect(&childClip, &clientRect, &clip))
      return true;

    // Stacked from the bottom up too. A group box frames its neighbours,
    // whatever its place in the stack, and is copied here with no holes, so
    // it goes first.
    std::vector<HWND> children = shownChildren(window);
    std::reverse(children.begin(), children.end());
    std::stable_partition(children.begin(), children.end(), isGroupBox);

    for (HWND child : children) {
      if (!paintWindow(child, childClip))
        return false;
    }

    return true;
  }


  void VrLauncherCapture::copyToCanvas(const RECT& rect, const RECT& clip) {
    RECT visible = { };

    if (!IntersectRect(&visible, &rect, &clip))
      return;

    BitBlt(m_canvasSurface.dc, visible.left - m_canvas.left, visible.top - m_canvas.top,
      visible.right - visible.left, visible.bottom - visible.top,
      m_windowSurface.dc, visible.left - rect.left, visible.top - rect.top, SRCCOPY);
  }


  void VrLauncherCapture::makeOpaque(HWND window) {
    GdiFlush();

    RECT rect = { };
    GetWindowRect(window, &rect);

    // GDI does not write the alpha channel. Everything under the window is
    // the window, whatever was drawn.
    LONG left   = std::max(rect.left,   LONG(m_canvas.left))   - m_canvas.left;
    LONG top    = std::max(rect.top,    LONG(m_canvas.top))    - m_canvas.top;
    LONG right  = std::min(rect.right,  LONG(m_canvas.right))  - m_canvas.left;
    LONG bottom = std::min(rect.bottom, LONG(m_canvas.bottom)) - m_canvas.top;

    for (LONG y = top; y < bottom; y++) {
      uint32_t* row = m_canvasSurface.pixels + size_t(y) * size_t(VrLauncherCanvasWidth);

      for (LONG x = left; x < right; x++)
        row[x] |= OpaqueAlpha;
    }
  }


  VrLauncherMouseRouter::VrLauncherMouseRouter(DWORD thread)
  : m_thread(thread) { }


  HWND VrLauncherMouseRouter::targetAt(const POINT& point) const {
    for (HWND window : shownWindows(m_thread)) {
      RECT rect = { };

      if (!GetWindowRect(window, &rect) || !contains(rect, point))
        continue;

      // A dialog waiting for another one takes no input, like with the mouse
      if (!IsWindowEnabled(window))
        return nullptr;

      HWND child = childAt(window, point);
      HWND target = child ? child : window;

      // Title bars and scroll bars are left alone: they enter loops that
      // read the real mouse. The dialogs have buttons, the lists have a wheel.
      DWORD_PTR hit = HTNOWHERE;

      if (!ask(target, WM_NCHITTEST, 0, pointParam(point.x, point.y), &hit) || LRESULT(hit) != HTCLIENT)
        return nullptr;

      return target;
    }

    return nullptr;
  }


  void VrLauncherMouseRouter::send(const VrLauncherMouseEvent& event) {
    using Kind = VrLauncherMouseEvent::Kind;

    POINT point = { event.point.x, event.point.y };

    GUITHREADINFO info = { };
    info.cbSize = sizeof(info);
    GetGUIThreadInfo(m_thread, &info);

    HWND target = info.hwndCapture ? info.hwndCapture : targetAt(point);

    // Windows learn that the cursor left them from a move outside of them
    if (m_hover && (event.kind == Kind::Leave || target != m_hover))
      PostMessageW(m_hover, WM_MOUSEMOVE, 0, pointParam(-1, -1));

    if (event.kind == Kind::Leave) {
      m_hover = nullptr;
      return;
    }

    m_hover = target;

    if (!target)
      return;

    WPARAM buttons = event.held ? MK_LBUTTON : 0;

    if (event.kind == Kind::Wheel) {
      PostMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(buttons, short(event.wheel)), pointParam(point.x, point.y));
      return;
    }

    POINT client = point;
    ScreenToClient(target, &client);
    LPARAM position = pointParam(client.x, client.y);

    switch (event.kind) {
      case Kind::Move:
        PostMessageW(target, WM_MOUSEMOVE, buttons, position);
        break;

      case Kind::ButtonDown:
        Logger::info(str::format("VR: Launcher: Click at ", point.x, ",", point.y, " on ", describe(target)));
        PostMessageW(target, WM_LBUTTONDOWN, buttons, position);
        break;

      case Kind::ButtonUp:
        PostMessageW(target, WM_LBUTTONUP, buttons, position);
        break;

      default:
        break;
    }
  }

}
