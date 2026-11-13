#include <algorithm>
#include <chrono>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_dds.h"
#include "vr_game_addresses.h"
#include "vr_game_depth.h"
#include "vr_game_file.h"
#include "vr_game_memory.h"
#include "vr_math.h"
#include "vr_scope_render.h"
#include "vr_scope_reticle.h"

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

    constexpr char ReplacementReticle[] = "textures\\weapons\\scope\\duplexscope_o.dds";

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
  : m_device(device), m_painter(device), m_resolution(resolution), m_tubeScreen(device, m_painter) { }


  std::unique_ptr<VrScopeRender> VrScopeRender::install(IDirect3DDevice9* device, uint32_t resolution) {
    bool found = VrGameMemory::callsTo(VrGame::MenuBackgroundDrawWorldCall, VrGame::DrawWorld)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundBorrowCall, VrGame::BorrowRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundReturnCall, VrGame::ReturnRenderedTexture)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundLockCall, VrGame::RendererLock)
      && VrGameMemory::callsTo(VrGame::MenuBackgroundUnlockCall, VrGame::RendererUnlock)
      && VrGameMemory::callsTo(VrGame::ResetMultiBoundsCallSite, VrGame::ResetMultiBounds);

    if (!found) {
      Logger::warn("VR: The game's render to a texture was not found, scopes show a dark glass");
      return nullptr;
    }

    Logger::info("VR: Scopes show the world on a lens");
    return std::unique_ptr<VrScopeRender>(new VrScopeRender(device, resolution));
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
    std::vector<uint8_t> data;
    std::string texture;

    if (VrGameFile::read(("Data\\Meshes\\" + model).c_str(), VrGame::ArchiveMeshes, data))
      texture = vrFindScopeReticleTexture(data.data(), data.size());
    else
      Logger::warn(str::format("VR: The scope model '", model, "' could not be read"));

    VrImage image;

    if (!texture.empty() && !readReticleTexture(texture, image)) {
      Logger::warn(str::format("VR: The reticle '", texture, "' could not be read"));
      texture.clear();
    }

    // A crosshair made of geometry (scope01.nif), or a model or texture a mod
    // replaced in a form not read here: one of the game's reticles stands in,
    // so that a scope always has one
    if (texture.empty()) {
      texture = ReplacementReticle;

      if (!readReticleTexture(texture, image)) {
        Logger::warn(str::format("VR: The reticle '", texture, "' could not be read, the lens has none"));
        return VrImage();
      }
    }

    Logger::info(str::format("VR: Scope model '", model, "' uses the reticle '", texture, "', ",
      image.width, "x", image.height));
    return image;
  }


  bool VrScopeRender::readReticleTexture(const std::string& texture, VrImage& image) {
    std::vector<uint8_t> data;

    if (!VrGameFile::read(("Data\\" + texture).c_str(), VrGame::ArchiveTextures, data)
     || !vrDecodeDds(data.data(), data.size(), image))
      return false;

    if (vrIsReticleSheet(texture))
      image = vrComposeReticleSheet(image);

    return true;
  }


  bool VrScopeRender::prepareView(float magnification, float apparentTangent, VrOpticKind kind, VrFov& fov) {
    // The game's image has the size of its window
    Com<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc = { };

    if (FAILED(m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &target))
     || FAILED(target->GetDesc(&desc)) || !desc.Width || !desc.Height)
      return false;

    m_targetWidth  = desc.Width;
    m_targetHeight = desc.Height;

    // The default side of the image is meant for a scope seen at its usual distance
    uint32_t side = kind == VrOpticKind::Binoculars ? m_resolution * 4 / 3
                  : kind == VrOpticKind::Screen     ? m_resolution / 2
                  : m_resolution;

    m_area = vrComputeScopeArea(desc.Width, desc.Height, side);
    fov    = vrComputeScopeRenderFov(magnification, apparentTangent, desc.Width, desc.Height, m_area);
    return true;
  }


  bool VrScopeRender::render(void* main) {
    m_imageReady = false;

    if (gameValue<uint8_t>(VrGame::MultiThreadedRendering)) {
      if (!m_loggedThreads) {
        Logger::warn("VR: Scopes show a dark glass while bMTRendering is set");
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
      Logger::info(str::format("VR: Scope image over ", WindowFrames, " frames: culling ",
        m_windowCull / WindowFrames, " ms, drawing ", m_windowDraw / WindowFrames, " ms"));
      m_windowFrames = 0;
      m_windowCull   = 0.0;
      m_windowDraw   = 0.0;
    }

    m_imageReady = copied;
    return copied;
  }


  void VrScopeRender::drawLens(
    const VrScopeLens&        lens,
    const VrGameCameraPose&   eye,
    const VrFov&              eyeFov,
          float               nearDistance,
          float               farDistance) {
    m_lensDrawn = false;

    Com<IDirect3DSurface9> depth;
    D3DSURFACE_DESC depthDesc = { };

    if (FAILED(m_device->GetDepthStencilSurface(&depth)) || depth == nullptr || FAILED(depth->GetDesc(&depthDesc))) {
      if (!m_loggedLensDepth) {
        Logger::warn("VR: The first-person model was drawn without a depth buffer, the lens is not drawn");
        m_loggedLensDepth = true;
      }

      return;
    }

    if (!prepareLensTarget(depthDesc))
      return;

    std::array<float, 16> projection = vrComputeEyeProjection(eyeFov, nearDistance, farDistance);

    // The depth as the game drew with it: Do_SetCameraData may have moved its
    // near plane, which changes this row, and the device keeps the matrix
    D3DMATRIX gameProjection = { };

    if (SUCCEEDED(m_device->GetTransform(D3DTS_PROJECTION, &gameProjection))
     && gameProjection._34 == 1.0f && gameProjection._44 == 0.0f) {
      projection[10] = gameProjection._33;
      projection[14] = gameProjection._43;
    }

    if (!m_loggedLensProjection) {
      Logger::info(str::format("VR: The scope's lens is tested with the depth ", projection[10], " + ", projection[14],
        " / z, the camera's near ", nearDistance, " far ", farDistance));
      m_loggedLensProjection = true;
    }

    // Binoculars have an eyepiece on each side, each eye looks through its
    // own. Drawn last, it goes over the other as it grows.
    EyeMesh meshes[2];
    UINT meshCount = 0;

    const VrScopeLens* single = lens.kind == VrOpticKind::Binoculars ? nullptr : &lens;

    VrScopeLensBounds bounds;
    bool hasBounds = false;

    auto addMesh = [&] (const VrScopeLens& seen, bool overDepth) {
      VrScopeLensBounds meshBounds;

      if (!vrComputeScopeLensMesh(seen, eye, meshes[meshCount].vertices)
       || !vrComputeScopeLensBounds(meshes[meshCount].vertices, eyeFov, meshBounds))
        return;

      bounds.left   = hasBounds ? std::min(bounds.left, meshBounds.left) : meshBounds.left;
      bounds.top    = hasBounds ? std::min(bounds.top, meshBounds.top) : meshBounds.top;
      bounds.right  = hasBounds ? std::max(bounds.right, meshBounds.right) : meshBounds.right;
      bounds.bottom = hasBounds ? std::max(bounds.bottom, meshBounds.bottom) : meshBounds.bottom;
      hasBounds = true;

      meshes[meshCount].overDepth = overDepth;
      meshCount += 1;
    };

    if (single) {
      addMesh(*single, false);
    } else {
      constexpr float OverDepthRaised = 0.5f;

      // The drawn near distance, as the depth row above gives it: where the
      // depth is 0, or 1 when the game's depth is reversed
      float nearest = nearDistance;

      if (!vrGameDepthReversed() && projection[10] > 0.0f)
        nearest = -projection[14] / projection[10];
      else if (vrGameDepthReversed() && projection[10] < 0.0f)
        nearest = projection[14] / (1.0f - projection[10]);

      addMesh(vrComputeBinocularsEyeLens(lens, eye, false, nearest), false);

      VrScopeLens own = vrComputeBinocularsEyeLens(lens, eye, true, nearest);
      addMesh(own, own.raised >= OverDepthRaised);
    }

    if (!meshCount)
      return;

    m_lensBounds = bounds;

    const void* vertexLists[2] = { meshes[0].vertices.data(), meshes[1].vertices.data() };
    bool overDepth[2] = { meshes[0].overDepth, meshes[1].overDepth };

    bool onScreen = lens.kind == VrOpticKind::Screen;

    VrPainter::LensDrawing drawing = { };
    drawing.target         = m_lensSurface.ptr();
    drawing.depth          = depth.ptr();
    drawing.image          = onScreen
      ? (m_screenReady ? m_tubeScreen.picture() : nullptr)
      : (m_imageReady ? m_image.ptr() : nullptr);
    drawing.imageMipmapped = onScreen;
    drawing.overlay        = m_reticle;
    drawing.projection     = projection.data();
    drawing.depthTest      = vrGameDepthReversed() ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL;
    drawing.meshes         = vertexLists;
    drawing.meshOverDepth  = overDepth;
    drawing.meshCount      = meshCount;
    drawing.vertexCount    = VrScopeLensVertexCount;
    drawing.vertexStride   = sizeof(VrScopeLensVertex);
    drawing.indices        = vrScopeLensIndices().data();
    drawing.triangles      = VrScopeLensTriangles;
    drawing.amplified      = lens.nightVision;

    if (!m_painter.drawLens(drawing))
      return;

    // A surface of its own if multisampled
    if (m_lensSamples != D3DMULTISAMPLE_NONE) {
      Com<IDirect3DSurface9> resolved;

      if (FAILED(m_lensImage->GetSurfaceLevel(0, &resolved))
       || FAILED(m_device->StretchRect(m_lensSurface.ptr(), nullptr, resolved.ptr(), nullptr, D3DTEXF_NONE)))
        return;
    }

    m_lensDrawn = true;
  }


  void VrScopeRender::updateScreen(bool viewIsNew, float shutterFlash) {
    IDirect3DTexture9* view = viewIsNew && m_imageReady ? m_image.ptr() : nullptr;

    m_screenReady = m_tubeScreen.update(view, shutterFlash);
  }


  void VrScopeRender::presentLens(IDirect3DSurface9* target) {
    if (!m_lensDrawn)
      return;

    m_painter.composite(target, m_lensImage.ptr(),
      m_lensBounds.left, m_lensBounds.top, m_lensBounds.right, m_lensBounds.bottom);

    m_lensDrawn = false;
  }


  bool VrScopeRender::prepareLensTarget(const D3DSURFACE_DESC& depth) {
    if (m_lensSurface != nullptr) {
      D3DSURFACE_DESC current = { };
      m_lensSurface->GetDesc(&current);

      if (current.Width == depth.Width && current.Height == depth.Height && m_lensSamples == depth.MultiSampleType)
        return true;

      m_lensSurface = nullptr;
      m_lensImage   = nullptr;
    }

    m_lensSamples = depth.MultiSampleType;

    bool made = SUCCEEDED(m_device->CreateTexture(depth.Width, depth.Height, 1,
      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_lensImage, nullptr));

    if (made) {
      made = depth.MultiSampleType == D3DMULTISAMPLE_NONE
        ? SUCCEEDED(m_lensImage->GetSurfaceLevel(0, &m_lensSurface))
        : SUCCEEDED(m_device->CreateRenderTarget(depth.Width, depth.Height, D3DFMT_A8R8G8B8,
            depth.MultiSampleType, depth.MultiSampleQuality, FALSE, &m_lensSurface, nullptr));
    }

    if (!made) {
      m_lensImage   = nullptr;
      m_lensSurface = nullptr;

      if (!m_loggedLensTarget) {
        Logger::err(str::format("VR: Failed to make the ", depth.Width, "x", depth.Height, " target of the scope's lens"));
        m_loggedLensTarget = true;
      }

      return false;
    }

    Logger::info(str::format("VR: The scope's lens is drawn on ", depth.Width, "x", depth.Height,
      ", ", uint32_t(depth.MultiSampleType), " samples, against depth format ", uint32_t(depth.Format)));
    return true;
  }


  bool VrScopeRender::copyImage(void* destination) {
    Com<IDirect3DSurface9> source;
    D3DSURFACE_DESC desc = { };

    if (!renderedTextureSurface(destination, source) || FAILED(source->GetDesc(&desc))) {
      if (!m_loggedTexture) {
        Logger::err("VR: The scope's rendered texture has no image");
        m_loggedTexture = true;
      }

      return false;
    }

    if (desc.Width != m_targetWidth || desc.Height != m_targetHeight) {
      if (!m_loggedTexture) {
        Logger::err(str::format("VR: The scope's rendered texture is ", desc.Width, "x", desc.Height,
          " and the window ", m_targetWidth, "x", m_targetHeight));
        m_loggedTexture = true;
      }

      return false;
    }

    uint32_t side = m_area.right - m_area.left;

    if (m_image != nullptr) {
      D3DSURFACE_DESC current = { };
      m_image->GetLevelDesc(0, &current);

      // Another kind of optic in hand
      if (current.Width != side)
        m_image = nullptr;
    }

    if (m_image == nullptr && FAILED(m_device->CreateTexture(side, side, 1,
        D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &m_image, nullptr)))
      return false;

    if (!m_loggedSize) {
      Logger::info(str::format("VR: Scope image drawn in ", side, "x", side, " of ", desc.Width, "x", desc.Height,
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
