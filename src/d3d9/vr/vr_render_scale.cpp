#include <algorithm>

#include "../../util/com/com_pointer.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "shaders/vr_upscale_sharpen_ps.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_painter.h"
#include "vr_render_scale.h"

namespace dxvk {

  namespace {

    using LookupFn      = void  (__fastcall*)(void* manager, void* unused, void* renderer, uint32_t type,
      uint32_t* width, uint32_t* height, uint32_t* format, uint32_t* entry, uint32_t* flags, uint32_t* samples);
    using WorldTargetFn = void* (__fastcall*)(void* main, void* unused, void* renderer, void* texture,
      uint32_t disableImageSpace, uint32_t multisampled);
    using BorrowFn      = void* (__fastcall*)(void* manager, void* unused, void* renderer,
      uint32_t type, uint32_t a, uint32_t b, uint32_t c);
    using ReturnFn      = void  (__fastcall*)(void* manager, void* unused, void* texture);
    using PointerFn     = void* (__cdecl*)();
    using StartFn       = void  (__cdecl*)(uint32_t clear, void* group);
    using IsOutsideFn   = bool  (__cdecl*)();

    VrRenderScale* g_renderScale = nullptr;

    // The functions the redirected calls reached: the game's, or another
    // mod's hook
    uintptr_t g_lookup      = VrGame::LookupTextureParameters;
    uintptr_t g_worldTarget = VrGame::InitRenderTexture;

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
    }

    void* gamePointer(uintptr_t address) {
      return *reinterpret_cast<void**>(address);
    }

    void* imageSpaceManager() {
      return game<PointerFn>(VrGame::ImageSpaceManagerSingleton)();
    }

    void* borrowTarget() {
      void* manager  = gamePointer(VrGame::TextureManager);
      void* renderer = gamePointer(VrGame::Renderer);

      if (!manager || !renderer)
        return nullptr;

      return game<BorrowFn>(VrGame::BorrowRenderedTexture)(manager, nullptr, renderer,
        VrGame::ImageSpaceTextureType, 0, 0, 0);
    }

    void returnTarget(void* texture) {
      void* manager = gamePointer(VrGame::TextureManager);

      if (manager && texture)
        game<ReturnFn>(VrGame::ReturnRenderedTexture)(manager, nullptr, texture);
    }

    // BSRenderedTexture -> NiRenderedTexture -> its renderer data -> the device's texture
    bool renderedTexture(void* rendered, Com<IDirect3DTexture9>& d3dTexture) {
      void* texture = rendered ? field<void*>(rendered, VrGame::RenderedTextureFirst) : nullptr;
      void* data    = texture ? field<void*>(texture, VrGame::TextureRendererData) : nullptr;
      auto  base    = data ? field<IDirect3DBaseTexture9*>(data, VrGame::TextureDataD3DTexture) : nullptr;

      return base && base->GetType() == D3DRTYPE_TEXTURE
          && SUCCEEDED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&d3dTexture)));
    }

  }


  VrRenderScale::VrRenderScale(IDirect3DDevice9* device, VrPainter& painter)
  : m_device(device), m_painter(painter) { }


  VrRenderScale::~VrRenderScale() {
    // The redirected calls stay in place and fall through to the game's functions
    endWorld();
    releaseTargets();
    g_renderScale = nullptr;
  }


  std::unique_ptr<VrRenderScale> VrRenderScale::install(IDirect3DDevice9* device, VrPainter& painter) {
    if (g_renderScale)
      return nullptr;

    bool found = VrGameMemory::matches(VrGame::BorrowRenderedTexture,
        VrGame::BorrowRenderedTexturePrologue, sizeof(VrGame::BorrowRenderedTexturePrologue))
      && VrGameMemory::matches(VrGame::ReturnRenderedTexture,
        VrGame::ReturnRenderedTexturePrologue, sizeof(VrGame::ReturnRenderedTexturePrologue))
      && VrGameMemory::matches(VrGame::ImageSpaceSwapTargetStore,
        VrGame::ImageSpaceSwapTargetStoreCode, sizeof(VrGame::ImageSpaceSwapTargetStoreCode))
      && VrGameMemory::matches(VrGame::RenderedTextureStart,
        VrGame::RenderedTextureStartCode, sizeof(VrGame::RenderedTextureStartCode))
      && VrGameMemory::matches(VrGame::RenderedTextureIsOutsideFrame,
        VrGame::RenderedTextureIsOutsideFrameCode, sizeof(VrGame::RenderedTextureIsOutsideFrameCode))
      && VrGameMemory::canChainCall(VrGame::TextureLookupCallSite, VrGame::LookupTextureParameters)
      && VrGameMemory::canChainCall(VrGame::WorldTargetCallSite, VrGame::InitRenderTexture);

    if (!found
     || !VrGameMemory::chainCall(VrGame::TextureLookupCallSite, VrGame::LookupTextureParameters,
          reinterpret_cast<const void*>(&VrRenderScale::lookupHook), &g_lookup)
     || !VrGameMemory::chainCall(VrGame::WorldTargetCallSite, VrGame::InitRenderTexture,
          reinterpret_cast<const void*>(&VrRenderScale::worldTargetHook), &g_worldTarget)) {
      Logger::warn("VR: The world's render targets were not found, the performance mode keeps the full resolution");
      return nullptr;
    }

    std::unique_ptr<VrRenderScale> scale(new VrRenderScale(device, painter));
    g_renderScale = scale.get();
    return scale;
  }


  void VrRenderScale::setScale(float scale) {
    m_scale = std::clamp(scale, VrMinRenderScale, 1.0f);
  }


  void VrRenderScale::beginEye(VrExtent screen) {
    m_screen = screen;
    m_scaled = vrScaledWorldExtent(screen, m_scale);
    m_inEye  = true;
  }


  void VrRenderScale::endEye() {
    endWorld();
    m_inEye = false;
  }


  uintptr_t VrRenderScale::imageSpaceDestination(uintptr_t destination) {
    if (!scaling() || !m_finalTarget)
      return destination;

    // Another destination than the screen keeps the game's own size
    if (destination) {
      if (!m_loggedFailure) {
        Logger::warn("VR: The world's image space pass draws into another target than the screen, it is not stretched");
        m_loggedFailure = true;
      }

      return destination;
    }

    m_upscale = true;
    return reinterpret_cast<uintptr_t>(m_finalTarget);
  }


  void VrRenderScale::finishImageSpace() {
    if (!m_upscale)
      return;

    m_upscale = false;

    // Drawing into a texture, the last effect ended the renderer's
    // offscreen frame, and the interface would start the screen's with
    // every buffer cleared, over the stretched world. It starts here
    // without a clear, as when the effect draws into the screen.
    if (game<IsOutsideFn>(VrGame::RenderedTextureIsOutsideFrame)())
      game<StartFn>(VrGame::RenderedTextureStart)(VrGame::ClearNone, nullptr);

    Com<IDirect3DTexture9> source;
    Com<IDirect3DSurface9> screen;

    bool stretched = renderedTexture(m_finalTarget, source)
      && SUCCEEDED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &screen))
      && stretch(source.ptr(), screen.ptr());

    if (!stretched && !m_loggedFailure) {
      Logger::err("VR: Failed to stretch the scaled world into the game's screen");
      m_loggedFailure = true;
    }
  }


  bool VrRenderScale::stretch(IDirect3DTexture9* source, IDirect3DSurface9* screen) {
    if (!m_sharpenTried) {
      m_sharpenTried = true;

      if (FAILED(m_device->CreatePixelShader(reinterpret_cast<const DWORD*>(VrUpscaleSharpenPixelShader), &m_sharpen))) {
        Logger::warn("VR: Failed to make the sharpening shader, the scaled world is only stretched");
        m_sharpen = nullptr;
      }
    }

    D3DSURFACE_DESC desc = { };

    if (m_sharpen != nullptr && SUCCEEDED(source->GetLevelDesc(0, &desc))) {
      float scale = float(m_scaled.width) / float(m_screen.width);

      const float constants[4] = {
        1.0f / float(desc.Width),
        1.0f / float(desc.Height),
        vrSharpenWeight(scale),
        0.0f,
      };

      if (m_painter.shade(screen, m_sharpen.ptr(), source, nullptr, constants))
        return true;
    }

    Com<IDirect3DSurface9> surface;

    return SUCCEEDED(source->GetSurfaceLevel(0, &surface))
        && SUCCEEDED(m_device->StretchRect(surface.ptr(), nullptr, screen, nullptr, D3DTEXF_LINEAR));
  }


  void VrRenderScale::beginWorld() {
    m_inWorld = true;

    if (!scaling()) {
      releaseTargets();
      return;
    }

    // Borrowed while the world is drawn, so they get the scaled size
    if (m_targetSize.width != m_scaled.width || m_targetSize.height != m_scaled.height) {
      releaseTargets();
      m_swapTarget  = borrowTarget();
      m_finalTarget = borrowTarget();
      m_targetSize  = m_scaled;

      if (!m_loggedScale) {
        Logger::info(str::format("VR: The eyes' world is drawn at ", m_scaled.width, "x", m_scaled.height,
          " for a screen of ", m_screen.width, "x", m_screen.height, ", then stretched and sharpened to it"));
        m_loggedScale = true;
      }
    }

    void* manager = imageSpaceManager();

    if (manager && m_swapTarget) {
      m_gameSwapTarget = field<void*>(manager, VrGame::ImageSpaceSwapTarget);
      field<void*>(manager, VrGame::ImageSpaceSwapTarget) = m_swapTarget;
      m_swapReplaced = true;
    }
  }


  void VrRenderScale::endWorld() {
    if (m_swapReplaced) {
      void* manager = imageSpaceManager();

      if (manager)
        field<void*>(manager, VrGame::ImageSpaceSwapTarget) = m_gameSwapTarget;

      m_swapReplaced = false;
    }

    m_inWorld = false;
    m_upscale = false;
  }


  void VrRenderScale::releaseTargets() {
    returnTarget(m_swapTarget);
    returnTarget(m_finalTarget);
    m_swapTarget  = nullptr;
    m_finalTarget = nullptr;
    m_targetSize  = VrExtent();
  }


  void __fastcall VrRenderScale::lookupHook(void* manager, void* unused, void* renderer, uint32_t type,
      uint32_t* width, uint32_t* height, uint32_t* format, uint32_t* entry, uint32_t* flags, uint32_t* samples) {
    game<LookupFn>(g_lookup)(manager, nullptr, renderer, type, width, height, format, entry, flags, samples);

    if (g_renderScale && g_renderScale->scaling() && width && height)
      vrScaleScreenTexture(g_renderScale->m_screen, g_renderScale->m_scaled, *width, *height);
  }


  void* __fastcall VrRenderScale::worldTargetHook(void* main, void* unused, void* renderer, void* texture,
      uint32_t disableImageSpace, uint32_t multisampled) {
    // The texture is DrawWorldStandard's destination: the eyes' world goes
    // to the screen, a menu's frozen background into a texture of its own
    if (g_renderScale && g_renderScale->m_inEye && !g_renderScale->m_inWorld && !texture)
      g_renderScale->beginWorld();

    return game<WorldTargetFn>(g_worldTarget)(main, nullptr, renderer, texture, disableImageSpace, multisampled);
  }

}
