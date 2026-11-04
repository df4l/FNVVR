#include <chrono>
#include <cmath>

#include "../../util/log/log.h"

#include "shaders/vr_tube_screen_ps.h"

#include "vr_tube_screen.h"

namespace dxvk {

  namespace {

    // 4:3, as the tube. Mipmaps keep the scanlines from shimmering from afar.
    constexpr UINT PictureWidth  = 512;
    constexpr UINT PictureHeight = 384;

    // Long enough for the shader's slow sines, short enough for a float's precision
    constexpr double TimeWrap = 3600.0;

    // The recording light's blinking
    constexpr double BlinkPeriod = 1.4;
    constexpr double BlinkOn     = 0.8;

  }


  VrTubeScreen::VrTubeScreen(IDirect3DDevice9* device, VrPainter& painter)
  : m_device(device), m_painter(painter) { }


  bool VrTubeScreen::prepare() {
    if (m_failed)
      return false;

    if (m_shader != nullptr)
      return true;

    bool made = SUCCEEDED(m_device->CreatePixelShader(reinterpret_cast<const DWORD*>(VrTubeScreenPixelShader), &m_shader));

    for (Com<IDirect3DTexture9>& picture : m_pictures) {
      made = made && SUCCEEDED(m_device->CreateTexture(PictureWidth, PictureHeight, 0,
        D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &picture, nullptr));

      Com<IDirect3DSurface9> surface;

      // The phosphor starts dark
      made = made && SUCCEEDED(picture->GetSurfaceLevel(0, &surface))
        && SUCCEEDED(m_device->ColorFill(surface.ptr(), nullptr, D3DCOLOR_ARGB(0, 0, 0, 0)));
    }

    if (!made) {
      Logger::err("VR: Failed to make the tube screen's shader or pictures, the screen stays dark");
      m_shader = nullptr;
      m_failed = true;
      return false;
    }

    Logger::info("VR: The camera's tube screen is ready");
    return true;
  }


  bool VrTubeScreen::update(IDirect3DTexture9* view, float shutterFlash) {
    if (view)
      m_view = view;

    if (m_view == nullptr || !prepare())
      return false;

    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();

    double seconds = std::chrono::duration<double>(Clock::now() - start).count();

    // A number that differs from picture to picture, for the static
    m_counter = (m_counter + 1) & 0xFFFF;
    float random = float(std::fmod(double(m_counter) * 0.61803398875, 1.0));

    const float constants[4] = {
      float(std::fmod(seconds, TimeWrap)),
      random,
      shutterFlash,
      std::fmod(seconds, BlinkPeriod) < BlinkOn ? 1.0f : 0.0f,
    };

    uint32_t next = 1 - m_latest;
    Com<IDirect3DSurface9> target;

    if (FAILED(m_pictures[next]->GetSurfaceLevel(0, &target))
     || !m_painter.shade(target.ptr(), m_shader.ptr(), m_view.ptr(), m_pictures[m_latest].ptr(), constants))
      return false;

    m_pictures[next]->GenerateMipSubLevels();
    m_latest = next;
    return true;
  }

}
