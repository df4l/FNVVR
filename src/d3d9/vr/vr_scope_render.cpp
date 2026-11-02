#include <chrono>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_dds.h"
#include "vr_game_addresses.h"
#include "vr_game_file.h"
#include "vr_game_memory.h"
#include "vr_scope_render.h"
#include "vr_weapon_drawn.h"

namespace dxvk {

  namespace {

    using WorldCameraFn = void* (__cdecl*)();
    using ResetBoundsFn = void (__fastcall*)(void* tes, void* unused, uint32_t landscape, uint32_t rooms);
    using VisibilityFn  = void (__fastcall*)(void* object, void* unused, void* camera);
    using RendererLockFn = void (__fastcall*)(void* renderer, void* unused);
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

    // BSRenderedTexture -> NiRenderedTexture -> its renderer data -> the device's texture
    bool renderedTextureSurface(void* rendered, Com<IDirect3DSurface9>& surface) {
      void* texture = rendered ? field<void*>(rendered, VrGame::RenderedTextureFirst) : nullptr;
      void* data    = texture ? field<void*>(texture, VrGame::TextureRendererData) : nullptr;
      auto  base    = data ? field<IDirect3DBaseTexture9*>(data, VrGame::TextureDataD3DTexture) : nullptr;

      Com<IDirect3DTexture9> d3dTexture;

      return base && base->GetType() == D3DRTYPE_TEXTURE
          && SUCCEEDED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&d3dTexture)))
          && SUCCEEDED(d3dTexture->GetSurfaceLevel(0, &surface));
    }

    bool adaptedLightSurface(Com<IDirect3DSurface9>& surface) {
      void* light = reinterpret_cast<void*>(VrGame::HdrAdaptedLight);

      return field<uint8_t>(light, VrGame::ImageSpaceTextureIsRendered)
          && renderedTextureSurface(field<void*>(light, VrGame::ImageSpaceTextureObject), surface);
    }

  }


  VrScopeRender::VrScopeRender(IDirect3DDevice9* device, uint32_t resolution)
  : m_device(device), m_painter(device), m_resolution(resolution) { }


  std::unique_ptr<VrScopeRender> VrScopeRender::install(IDirect3DDevice9* device, uint32_t resolution) {
    bool found = VrGameMemory::callsTo(VrGame::MenuBackgroundDrawWorldCall, VrGame::DrawWorld)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundBorrowCall, VrGame::BorrowRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundReturnCall, VrGame::ReturnRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundLockCall, VrGame::RendererLock)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundUnlockCall, VrGame::RendererUnlock)
      && VrGameMemory::callsTo(VrGame::ResetMultiBoundsCallSite, VrGame::ResetMultiBounds);

    if (!found) {
      Logger::warn("VR: The game's render to a texture was not found, the scope test is off");
      return nullptr;
    }

    Logger::info("VR: Scope test: a scope the mod knows shows the world on a lens");
    return std::unique_ptr<VrScopeRender>(new VrScopeRender(device, resolution));
  }


  float VrScopeRender::weaponSightFov() {
    int32_t type = -1;
    const uint8_t* weapon = VrWeaponDrawn::equippedWeapon(type);

    if (!weapon)
      return 0.0f;

    float fov = *reinterpret_cast<const float*>(weapon + VrGame::WeaponSightFov);
    return fov > 0.0f ? fov : 0.0f;
  }


  const char* VrScopeRender::weaponScopeModel() {
    int32_t type = -1;
    const uint8_t* weapon = VrWeaponDrawn::equippedWeapon(type);

    return weapon
      ? *reinterpret_cast<const char* const*>(weapon + VrGame::WeaponScopeModelPath)
      : nullptr;
  }


  void VrScopeRender::selectReticle(const char* model) {
    if (!model || !model[0]) {
      m_reticle = nullptr;
      return;
    }

    auto found = m_reticles.find(model);

    if (found == m_reticles.end())
      found = m_reticles.emplace(model, readReticle(model)).first;

    m_reticle = found->second.width ? &found->second : nullptr;
  }


  VrImage VrScopeRender::readReticle(const std::string& model) {
    VrImage image;
    std::vector<uint8_t> data;

    if (!VrGameFile::read(("Data\\Meshes\\" + model).c_str(), VrGame::ArchiveMeshes, data)) {
      Logger::warn(str::format("VR: The scope model '", model, "' could not be read, the lens has no reticle"));
      return image;
    }

    std::string texture = vrFindScopeReticleTexture(data.data(), data.size());

    if (texture.empty()) {
      Logger::info(str::format("VR: The scope model '", model, "' names no reticle"));
      return image;
    }

    if (!VrGameFile::read(("Data\\" + texture).c_str(), VrGame::ArchiveTextures, data)
     || !vrDecodeDds(data.data(), data.size(), image)) {
      Logger::warn(str::format("VR: The reticle '", texture, "' could not be read"));
      return VrImage();
    }

    Logger::info(str::format("VR: Scope model '", model, "' uses the reticle '", texture, "', ",
      image.width, "x", image.height));
    return image;
  }


  bool VrScopeRender::prepareView(float magnification, VrFov& fov) {
    // The game's image has the size of its window
    Com<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc = { };

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &target))
     || FAILED(target->GetDesc(&desc)) || !desc.Width || !desc.Height)
      return false;

    m_targetWidth  = desc.Width;
    m_targetHeight = desc.Height;
    m_area = vrComputeScopeArea(desc.Width, desc.Height, m_resolution);
    fov    = vrComputeScopeRenderFov(magnification, desc.Width, desc.Height, m_area);
    return true;
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

    // As TESMain::RenderMenuBackground does, except for the lock on the tiles:
    // the game only holds it over a render while its AI threads are stopped,
    // and they run here. Marking the render as the menu background keeps the
    // interface out of it.
    void* renderer = gameValue<void*>(VrGame::Renderer);
    void* manager  = gameValue<void*>(VrGame::TextureManager);

    uint8_t savedEndOfFrame  = gameValue<uint8_t>(VrGame::ImageSpaceEndOfFrameEnabled);
    uint8_t savedMenuBgFlag  = field<uint8_t>(main, VrGame::MainRenderingMenuBackground);

    gameValue<uint8_t>(VrGame::ImageSpaceEndOfFrameEnabled) = 1;
    field<uint8_t>(main, VrGame::MainRenderingMenuBackground) = 1;

    reinterpret_cast<RendererLockFn>(VrGame::RendererLock)(renderer, nullptr);

    void* destination = reinterpret_cast<BorrowFn>(VrGame::BorrowRenderedTexture)(
      manager, nullptr, renderer, VrGame::TextureMainMenuBackground, 0, 0, 0);

    bool copied = false;

    if (destination) {
      // The eyes keep adapting to what they see, not to this mostly black image
      bool adaptationSaved = saveAdaptation();

      reinterpret_cast<DrawWorldFn>(VrGame::DrawWorld)(main, nullptr, destination, 0, 1);
      copied = copyImage(destination);

      if (adaptationSaved)
        restoreAdaptation();

      reinterpret_cast<ReturnFn>(VrGame::ReturnRenderedTexture)(manager, nullptr, destination);
    }

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


  void VrScopeRender::drawLens(
          IDirect3DSurface9*  target,
    const VrScopeLens&        lens,
    const VrGameCameraPose&   eye,
    const VrFov&              eyeFov) {
    VrScopeLensDisc disc;

    if (!m_imageReady || !vrComputeScopeLensDisc(lens, eye, disc))
      return;

    std::array<float, 16> projection = vrComputeEyeProjection(eyeFov);

    m_painter.drawFan(target, m_image.ptr(), m_reticle, projection.data(),
      disc.data(), UINT(disc.size()), sizeof(VrScopeLensVertex));
  }


  bool VrScopeRender::copyImage(void* destination) {
    Com<IDirect3DSurface9> source;
    D3DSURFACE_DESC desc = { };

    if (!renderedTextureSurface(destination, source) || FAILED(source->GetDesc(&desc))) {
      if (!m_loggedTexture) {
        Logger::err("VR: Scope test: the rendered texture's image was not found");
        m_loggedTexture = true;
      }

      return false;
    }

    if (desc.Width != m_targetWidth || desc.Height != m_targetHeight) {
      if (!m_loggedTexture) {
        Logger::err(str::format("VR: Scope test: the rendered texture is ", desc.Width, "x", desc.Height,
          " and the window ", m_targetWidth, "x", m_targetHeight));
        m_loggedTexture = true;
      }

      return false;
    }

    uint32_t side = m_area.right - m_area.left;

    if (m_image == nullptr && FAILED(m_device->CreateTexture(side, side, 1,
        D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &m_image, nullptr)))
      return false;

    if (!m_loggedSize) {
      Logger::info(str::format("VR: Scope test: drawn in ", side, "x", side, " of ", desc.Width, "x", desc.Height,
        ", format ", uint32_t(desc.Format)));
      m_loggedSize = true;
    }

    RECT area = { LONG(m_area.left), LONG(m_area.top), LONG(m_area.right), LONG(m_area.bottom) };

    Com<IDirect3DSurface9> image;

    return SUCCEEDED(m_image->GetSurfaceLevel(0, &image))
        && SUCCEEDED(m_device->StretchRect(source.ptr(), &area, image.ptr(), nullptr, D3DTEXF_POINT));
  }


  bool VrScopeRender::saveAdaptation() {
    Com<IDirect3DSurface9> light;
    D3DSURFACE_DESC desc = { };

    // None before the first frame with HDR
    if (!adaptedLightSurface(light) || FAILED(light->GetDesc(&desc)))
      return false;

    if (m_savedAdaptation == nullptr && FAILED(m_device->CreateTexture(desc.Width, desc.Height, 1,
        D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &m_savedAdaptation, nullptr)))
      return false;

    Com<IDirect3DSurface9> saved;

    return SUCCEEDED(m_savedAdaptation->GetSurfaceLevel(0, &saved))
        && SUCCEEDED(m_device->StretchRect(light.ptr(), nullptr, saved.ptr(), nullptr, D3DTEXF_POINT));
  }


  void VrScopeRender::restoreAdaptation() {
    // The render replaced the texture by a new one
    Com<IDirect3DSurface9> light;
    Com<IDirect3DSurface9> saved;

    if (adaptedLightSurface(light) && SUCCEEDED(m_savedAdaptation->GetSurfaceLevel(0, &saved)))
      m_device->StretchRect(saved.ptr(), nullptr, light.ptr(), nullptr, D3DTEXF_POINT);
  }

}
