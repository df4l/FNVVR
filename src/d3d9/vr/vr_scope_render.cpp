#include <chrono>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_scope_render.h"
#include "vr_weapon_drawn.h"

namespace dxvk {

  namespace {

    using WorldCameraFn = void* (__cdecl*)();
    using ResetBoundsFn = void (__fastcall*)(void* tes, void* unused, uint32_t landscape, uint32_t rooms);
    using VisibilityFn  = void (__fastcall*)(void* object, void* unused, void* camera);
    using RendererLockFn = void (__fastcall*)(void* renderer, void* unused);
    using TileLockFn    = void (__cdecl*)();
    using BorrowFn      = void* (__fastcall*)(void* manager, void* unused, void* renderer,
      uint32_t type, uint32_t a, uint32_t b, uint32_t c);
    using ReturnFn      = void (__fastcall*)(void* manager, void* unused, void* texture);
    using DrawWorldFn   = void (__fastcall*)(void* main, void* unused, void* destination,
      uint32_t renderedMenuMode, uint32_t skipFirstPerson);

    template<typename T>
    T& gameValue(uintptr_t address) {
      return *reinterpret_cast<T*>(address);
    }

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
    }

    constexpr uint32_t WindowFrames = 900;

  }


  VrScopeRender::VrScopeRender(IDirect3DDevice9* device)
  : m_device(device) { }


  std::unique_ptr<VrScopeRender> VrScopeRender::install(IDirect3DDevice9* device) {
    bool found = VrGameMemory::callsTo(VrGame::MenuBackgroundDrawWorldCall, VrGame::DrawWorld)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundBorrowCall, VrGame::BorrowRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundReturnCall, VrGame::ReturnRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundLockCall, VrGame::RendererLock)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundUnlockCall, VrGame::RendererUnlock)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundTileLockCall, VrGame::TileLock)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundTileUnlockCall, VrGame::TileUnlock)
      && VrGameMemory::callsTo(VrGame::ResetMultiBoundsCallSite, VrGame::ResetMultiBounds);

    if (!found) {
      Logger::warn("VR: The game's render to a texture was not found, the scope test is off");
      return nullptr;
    }

    Logger::info("VR: Scope test: the world is also drawn from the right hand, shown in a corner of each eye");
    return std::unique_ptr<VrScopeRender>(new VrScopeRender(device));
  }


  float VrScopeRender::weaponSightFov() {
    int32_t type = -1;
    const uint8_t* weapon = VrWeaponDrawn::equippedWeapon(type);

    if (!weapon)
      return 0.0f;

    float fov = *reinterpret_cast<const float*>(weapon + VrGame::WeaponSightFov);
    return fov > 0.0f ? fov : 0.0f;
  }


  float VrScopeRender::aspect() const {
    // The image has the size of the game's window
    Com<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc = { };

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &target))
     || FAILED(target->GetDesc(&desc)) || !desc.Height)
      return 1.0f;

    return float(desc.Width) / float(desc.Height);
  }


  bool VrScopeRender::render(void* main) {
    if (gameValue<uint8_t>(VrGame::MultiThreadedRendering)) {
      if (!m_loggedThreads) {
        Logger::warn("VR: Scope test: not drawn while bMTRendering is set");
        m_loggedThreads = true;
      }

      return false;
    }

    using Clock = std::chrono::high_resolution_clock;
    auto start = Clock::now();

    // The results of the visibility pass do not add up over cameras, and the
    // barrel may point away from where the head looks
    void* camera = reinterpret_cast<WorldCameraFn>(VrGame::GetWorldCamera)();
    void* tes    = gameValue<void*>(VrGame::TesSingleton);
    void* scene  = gameValue<void*>(VrGame::ShadowSceneNodes);

    reinterpret_cast<ResetBoundsFn>(VrGame::ResetMultiBounds)(tes, nullptr, 1, 1);
    reinterpret_cast<VisibilityFn>(VrGame::OcclusionPlaneVisibility)(scene, nullptr, camera);
    reinterpret_cast<VisibilityFn>(VrGame::MultiBoundVisibility)(tes, nullptr, camera);

    auto culled = Clock::now();

    // As TESMain::RenderMenuBackground does. Marking the render as the menu
    // background keeps the interface out of it.
    void* renderer = gameValue<void*>(VrGame::Renderer);
    void* manager  = gameValue<void*>(VrGame::TextureManager);

    uint8_t savedEndOfFrame  = gameValue<uint8_t>(VrGame::ImageSpaceEndOfFrameEnabled);
    uint8_t savedMenuBgFlag  = field<uint8_t>(main, VrGame::MainRenderingMenuBackground);

    gameValue<uint8_t>(VrGame::ImageSpaceEndOfFrameEnabled) = 1;
    field<uint8_t>(main, VrGame::MainRenderingMenuBackground) = 1;

    reinterpret_cast<RendererLockFn>(VrGame::RendererLock)(renderer, nullptr);
    reinterpret_cast<TileLockFn>(VrGame::TileLock)();

    void* destination = reinterpret_cast<BorrowFn>(VrGame::BorrowRenderedTexture)(
      manager, nullptr, renderer, VrGame::TextureMainMenuBackground, 0, 0, 0);

    bool copied = false;

    if (destination) {
      reinterpret_cast<DrawWorldFn>(VrGame::DrawWorld)(main, nullptr, destination, 0, 1);
      copied = copyImage(destination);
      reinterpret_cast<ReturnFn>(VrGame::ReturnRenderedTexture)(manager, nullptr, destination);
    }

    reinterpret_cast<TileLockFn>(VrGame::TileUnlock)();
    reinterpret_cast<RendererLockFn>(VrGame::RendererUnlock)(renderer, nullptr);

    field<uint8_t>(main, VrGame::MainRenderingMenuBackground) = savedMenuBgFlag;
    gameValue<uint8_t>(VrGame::ImageSpaceEndOfFrameEnabled) = savedEndOfFrame;

    auto end = Clock::now();

    m_windowCull += std::chrono::duration<double, std::milli>(culled - start).count();
    m_windowDraw += std::chrono::duration<double, std::milli>(end - culled).count();

    if (++m_windowFrames == WindowFrames) {
      Logger::info(str::format("VR: Scope test over ", WindowFrames, " frames: culling ",
        m_windowCull / WindowFrames, " ms, drawing ", m_windowDraw / WindowFrames, " ms"));
      m_windowFrames = 0;
      m_windowCull   = 0.0;
      m_windowDraw   = 0.0;
    }

    m_imageReady = copied;
    return copied;
  }


  void VrScopeRender::drawInto(IDirect3DSurface9* target, const RECT& rect) {
    if (!m_imageReady)
      return;

    Com<IDirect3DSurface9> source;

    if (SUCCEEDED(m_image->GetSurfaceLevel(0, &source)))
      m_device->StretchRect(source.ptr(), nullptr, target, &rect, D3DTEXF_LINEAR);
  }


  bool VrScopeRender::copyImage(void* destination) {
    // BSRenderedTexture -> NiRenderedTexture -> its renderer data -> the device's texture
    void* texture = field<void*>(destination, VrGame::RenderedTextureFirst);
    void* data    = texture ? field<void*>(texture, VrGame::TextureRendererData) : nullptr;
    auto  base    = data ? field<IDirect3DBaseTexture9*>(data, VrGame::TextureDataD3DTexture) : nullptr;

    Com<IDirect3DTexture9> sourceTexture;
    Com<IDirect3DSurface9> source;
    D3DSURFACE_DESC desc = { };

    if (!base || base->GetType() != D3DRTYPE_TEXTURE
     || FAILED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&sourceTexture)))
     || FAILED(sourceTexture->GetSurfaceLevel(0, &source))
     || FAILED(source->GetDesc(&desc))) {
      if (!m_loggedTexture) {
        Logger::err("VR: Scope test: the rendered texture's image was not found");
        m_loggedTexture = true;
      }

      return false;
    }

    if (m_image == nullptr && FAILED(m_device->CreateTexture(desc.Width / 2, desc.Height / 2, 1,
        D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &m_image, nullptr)))
      return false;

    if (!m_loggedSize) {
      Logger::info(str::format("VR: Scope test: drawn at ", desc.Width, "x", desc.Height,
        ", format ", uint32_t(desc.Format)));
      m_loggedSize = true;
    }

    Com<IDirect3DSurface9> image;

    return SUCCEEDED(m_image->GetSurfaceLevel(0, &image))
        && SUCCEEDED(m_device->StretchRect(source.ptr(), nullptr, image.ptr(), nullptr, D3DTEXF_LINEAR));
  }

}
