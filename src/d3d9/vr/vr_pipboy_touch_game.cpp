#include <cmath>
#include <cstring>
#include <iterator>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_math.h"
#include "vr_pipboy_size.h"
#include "vr_pipboy_touch_game.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  namespace {

    using CheckButtonsFn = char (__fastcall*)(void* pipBoy, void* unused,
      uint32_t pressed, uint32_t released, float* x, float* y, uint32_t* tab);
    using RayFn = bool (__fastcall*)(void* camera, void* unused,
      int32_t x, int32_t y, float* origin, float* direction, void* target);
    using GetPipBoyFn = uint8_t* (__cdecl*)();

    VrPipBoyTouchGame* g_touch = nullptr;

    uintptr_t g_checkButtons = VrGame::CheckPipBoyButtons;
    uintptr_t g_ray          = VrGame::CameraWindowPointToRay;

    template<typename T>
    T field(const uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    VrVector3 readVector(const uint8_t* object, uintptr_t offset) {
      auto v = reinterpret_cast<const float*>(object + offset);
      return { v[0], v[1], v[2] };
    }

    VrGameRotation readRotation(const uint8_t* object, uintptr_t offset) {
      VrGameRotation r;
      auto m = reinterpret_cast<const float*>(object + offset);

      for (uint32_t i = 0; i < 3; i++) {
        for (uint32_t j = 0; j < 3; j++)
          r.m[i][j] = m[3 * i + j];
      }

      return r;
    }

    /**
     * \brief An object's world transform, mesh space to the drawing space
     */
    struct WorldFrame {
      VrGameRotation rotate;
      VrVector3      translate;
      float          scale = 1.0f;

      VrVector3 toWorld(const VrVector3& p) const {
        return rotate * (p * scale) + translate;
      }

      VrVector3 toLocal(const VrVector3& p) const {
        return vrTranspose(rotate) * (p - translate) * (1.0f / scale);
      }
    };

    WorldFrame worldFrame(const uint8_t* object) {
      WorldFrame frame;
      frame.rotate    = readRotation(object, VrGame::ObjectWorldRotation);
      frame.translate = readVector(object, VrGame::ObjectWorldTranslation);
      frame.scale     = field<float>(object, VrGame::ObjectWorldScale);
      return frame;
    }

    uint8_t* pipBoy() {
      return reinterpret_cast<GetPipBoyFn>(VrGame::GetPipBoy)();
    }

    /**
     * \brief A tab button's world bound: centre and radius
     */
    bool buttonBound(const uint8_t* manager, uint32_t index, VrVector3& centre, float& radius) {
      auto button = field<const uint8_t*>(manager, VrGame::PipBoyTabButtons + 4 * index);
      auto bound  = button ? field<const uint8_t*>(button, VrGame::ObjectWorldBound) : nullptr;

      if (!bound)
        return false;

      centre = readVector(bound, 0);
      radius = field<float>(bound, 12);
      return std::isfinite(radius) && radius >= 0.0f;
    }

  }


  VrPipBoyTouchGame::~VrPipBoyTouchGame() {
    // The patched calls stay in place and fall through to the game's functions
    g_touch = nullptr;
  }


  std::unique_ptr<VrPipBoyTouchGame> VrPipBoyTouchGame::install() {
    if (g_touch)
      return nullptr;

    bool found = VrGameMemory::canChainCall(VrGame::PipBoyButtonsPickCallSite, VrGame::CheckPipBoyButtons);

    for (uintptr_t site : VrGame::PipBoyScreenRayCallSites)
      found = found && VrGameMemory::canChainCall(site, VrGame::CameraWindowPointToRay);

    found = found
      && VrGameMemory::chainCalls(VrGame::PipBoyScreenRayCallSites, std::size(VrGame::PipBoyScreenRayCallSites),
           VrGame::CameraWindowPointToRay, reinterpret_cast<const void*>(&rayHook), &g_ray)
      && VrGameMemory::chainCall(VrGame::PipBoyButtonsPickCallSite, VrGame::CheckPipBoyButtons,
           reinterpret_cast<const void*>(&checkButtonsHook), &g_checkButtons);

    if (!found) {
      Logger::warn("VR: The Pip-Boy's mouse pick was not found, its screen cannot be touched");
      return nullptr;
    }

    std::unique_ptr<VrPipBoyTouchGame> touch(new VrPipBoyTouchGame());
    g_touch = touch.get();

    Logger::info("VR: The Pip-Boy's screen can be touched with the right index finger");
    return touch;
  }


  void VrPipBoyTouchGame::beginFrame() {
    m_point         = m_framePoint;
    m_hasPoint      = m_frameMeasured;
    m_frameMeasured = false;
    m_picked        = false;
    m_hasRay        = false;
  }


  void VrPipBoyTouchGame::setAim(const VrPipBoyAim& aim, bool active) {
    m_aim    = aim;
    m_active = active;
  }


  void VrPipBoyTouchGame::measure(const VrVector3& fingertip, const VrVector3& elbow, const VrVector3& wrist) {
    m_hasRay = false;

    uint8_t* manager = pipBoy();
    auto geometry = manager ? field<const uint8_t*>(manager, VrGame::RenderedMenuScreen) : nullptr;
    const VrTouchScreen* fitted = geometry ? screen(geometry) : nullptr;

    if (!fitted)
      return;

    // The screen faces away from the forearm's axis
    WorldFrame frame = worldFrame(geometry);
    VrVector3 centre = frame.toWorld(fitted->centre);
    VrVector3 out    = centre - vrClosestPointOnLine(elbow, wrist, centre);

    VrTouchScreen faced = vrDot(frame.rotate * fitted->normal, out) < 0.0f
      ? vrFlipTouchScreen(*fitted) : *fitted;

    VrVector3 normal = frame.rotate * faced.normal;
    float toMetres = frame.scale / VrGameUnitsPerMetre;

    VrTouchScreenPoint onScreen = vrPointOnScreen(faced, frame.toLocal(fingertip));

    VrPipBoyTouchPoint point;
    point.distance = onScreen.distance * toMetres;
    point.uv       = onScreen.uv;
    point.inside   = onScreen.inside;
    point.plane    = { onScreen.plane.x * toMetres, onScreen.plane.y * toMetres };
    point.surface  = onScreen.surface;

    // The nearest tab button within reach, from its bound
    for (uint32_t i = 0; i < VrGame::PipBoyTabButtonCount; i++) {
      VrVector3 buttonCentre;
      float radius = 0.0f;

      if (!buttonBound(manager, i, buttonCentre, radius))
        continue;

      float distance = (vrLength(fingertip - buttonCentre) - radius) / VrGameUnitsPerMetre;

      if (distance <= ButtonReach && (point.button < 0 || distance < point.buttonDistance)) {
        point.button         = int32_t(i);
        point.buttonDistance = distance;
      }
    }

    if (!m_frameMeasured) {
      m_framePoint    = point;
      m_frameMeasured = true;
    }

    // The pick looks along the screen's normal at the point or the button
    VrVector3 target;

    if (m_aim.kind == VrPipBoyAimKind::Screen) {
      target   = frame.toWorld(m_aim.surface);
      m_hasRay = true;
    } else if (m_aim.kind == VrPipBoyAimKind::Button && m_aim.button >= 0) {
      float radius = 0.0f;
      m_hasRay = buttonBound(manager, uint32_t(m_aim.button), target, radius);
    }

    if (m_hasRay) {
      m_rayOrigin    = target + normal * (RayBack * VrGameUnitsPerMetre);
      m_rayDirection = normal * -1.0f;
    }

    if (!m_loggedTouch && point.inside && point.distance < 0.1f) {
      Logger::info(str::format("VR: The right index finger is over the Pip-Boy's screen, ",
        point.distance, " m from it, at ", point.uv.x, ", ", point.uv.y));
      m_loggedTouch = true;
    }
  }


  const VrTouchScreen* VrPipBoyTouchGame::screen(const uint8_t* geometry) {
    auto data = field<const uint8_t*>(geometry, VrGame::GeometryModelData);

    bool shape = field<uintptr_t>(geometry, 0) == VrGame::TriShapeVtable && data
      && (field<uintptr_t>(data, 0) == VrGame::TriShapeDataVtable
       || field<uintptr_t>(data, 0) == VrGame::TriShapeDynamicDataVtable);

    auto positions = shape ? field<const VrVector3*>(data, VrGame::GeometryDataVertices) : nullptr;
    auto uvs       = shape ? field<const VrVector2*>(data, VrGame::GeometryDataTexCoords) : nullptr;
    auto triangles = shape ? field<const uint16_t*>(data, VrGame::TriShapeDataTriangles) : nullptr;

    if (!positions || !uvs || !triangles) {
      if (!m_loggedScreen)
        Logger::warn("VR: The Pip-Boy's screen is not a triangle shape with texture coordinates, it cannot be touched");

      m_loggedScreen = true;
      return nullptr;
    }

    uint32_t vertices  = field<uint16_t>(data, VrGame::GeometryDataVertexCount);
    uint32_t count     = field<uint16_t>(data, VrGame::TriShapeDataTriangleCount);

    // The game scales the screen's texture coordinates once, in place
    bool same = data == m_cache.data && vertices == m_cache.vertices && count == m_cache.triangles
      && vertices && !std::memcmp(&uvs[0], &m_cache.firstUv, sizeof(VrVector2))
      && !std::memcmp(&uvs[vertices - 1], &m_cache.lastUv, sizeof(VrVector2));

    if (!same) {
      m_cache.data      = data;
      m_cache.vertices  = vertices;
      m_cache.triangles = count;
      m_cache.screen    = vrFitTouchScreen(positions, uvs, vertices, triangles, count);

      if (vertices) {
        m_cache.firstUv = uvs[0];
        m_cache.lastUv  = uvs[vertices - 1];
      }

      const VrTouchScreen& fit = m_cache.screen;

      if (!fit.valid)
        Logger::warn("VR: The Pip-Boy's screen mesh is not flat with texture coordinates, it cannot be touched");
      else
        Logger::info(str::format("VR: The Pip-Boy's screen fitted from ", vertices, " vertices, texture ",
          fit.uvMin.x, ", ", fit.uvMin.y, " to ", fit.uvMax.x, ", ", fit.uvMax.y));
    }

    return m_cache.screen.valid ? &m_cache.screen : nullptr;
  }


  char __fastcall VrPipBoyTouchGame::checkButtonsHook(
          void*                 pipBoy,
          void*                 unused,
          uint32_t              pressed,
          uint32_t              released,
          float*                x,
          float*                y,
          uint32_t*             tab) {
    auto previous = reinterpret_cast<CheckButtonsFn>(g_checkButtons);
    VrPipBoyTouchGame* touch = g_touch;

    if (!touch || !touch->m_active)
      return previous(pipBoy, unused, pressed, released, x, y, tab);

    if (!touch->m_picked) {
      float pickX = 0.0f;
      float pickY = 0.0f;
      uint32_t pickTab = 0;
      char hit = 0;

      // Without a finger at the screen, the camera's ray through the
      // cursor would hit it anywhere
      if (touch->m_hasRay) {
        touch->m_rayOverride = true;
        hit = previous(pipBoy, unused, pressed, released, &pickX, &pickY, &pickTab);
        touch->m_rayOverride = false;
      }

      touch->m_picked  = true;
      touch->m_pickHit = hit != 0;
      touch->m_pickX   = pickX;
      touch->m_pickY   = pickY;
      touch->m_pickTab = pickTab;
    }

    *x   = touch->m_pickX;
    *y   = touch->m_pickY;
    *tab = touch->m_pickTab;
    return touch->m_pickHit ? 1 : 0;
  }


  bool __fastcall VrPipBoyTouchGame::rayHook(
          void*                 camera,
          void*                 unused,
          int32_t               x,
          int32_t               y,
          float*                origin,
          float*                direction,
          void*                 target) {
    VrPipBoyTouchGame* touch = g_touch;

    if (!touch || !touch->m_rayOverride)
      return reinterpret_cast<RayFn>(g_ray)(camera, unused, x, y, origin, direction, target);

    origin[0]    = touch->m_rayOrigin.x;
    origin[1]    = touch->m_rayOrigin.y;
    origin[2]    = touch->m_rayOrigin.z;
    direction[0] = touch->m_rayDirection.x;
    direction[1] = touch->m_rayDirection.y;
    direction[2] = touch->m_rayDirection.z;
    return true;
  }

}
