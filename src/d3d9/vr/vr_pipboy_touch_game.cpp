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
    using GetRttiFn   = const void* (__fastcall*)(const void* object, void* unused);
    using IsTopMenuFn = bool (__cdecl*)(uint32_t menuId);
    using TileSetFloatFn = void (__thiscall*)(void* tile, uint32_t trait, float value, bool propagate);
    using ZoomMapFn   = void (__thiscall*)(void* map, bool in, float factor);
    using PipBoyMouseFn = double (__thiscall*)(void* interfaceManager);

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

    const void* rttiOf(const void* object) {
      auto getRtti = field<GetRttiFn*>(static_cast<const uint8_t*>(object), 0)[VrGame::ObjectGetRttiSlot / sizeof(void*)];
      return getRtti(object, nullptr);
    }

    bool isKindOf(const void* object, uintptr_t rtti) {
      const void* r = rttiOf(object);

      for (uint32_t i = 0; r && i < 16; i++) {
        if (reinterpret_cast<uintptr_t>(r) == rtti)
          return true;

        r = field<const void*>(static_cast<const uint8_t*>(r), VrGame::RttiBase);
      }

      return false;
    }

    const char* rttiName(const void* object) {
      const void* r = object ? rttiOf(object) : nullptr;
      const char* name = r ? field<const char*>(static_cast<const uint8_t*>(r), VrGame::RttiName) : nullptr;
      return name ? name : "none";
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


  VrPipBoyTouchGame::ButtonPlate VrPipBoyTouchGame::buttonPlate(
    const uint8_t*              manager,
          uint32_t              index,
    const ScreenFrame&          frame) {
    ButtonPlate plate;

    auto extend = [&] (const VrVector3& p) {
      VrVector3 offset = p - frame.centre;
      float height = vrDot(offset, frame.normal);
      VrVector2 at = { vrDot(offset, frame.u), vrDot(offset, frame.v) };

      if (!plate.valid) {
        plate.valid  = true;
        plate.height = height;
        plate.min    = at;
        plate.max    = at;
      } else {
        plate.height = std::max(plate.height, height);
        plate.min    = { std::min(plate.min.x, at.x), std::min(plate.min.y, at.y) };
        plate.max    = { std::max(plate.max.x, at.x), std::max(plate.max.y, at.y) };
      }
    };

    auto button = field<const uint8_t*>(manager, VrGame::PipBoyTabButtons + 4 * index);

    if (!button)
      return plate;

    auto data = isKindOf(button, VrGame::NiGeometryRtti)
      ? field<const uint8_t*>(button, VrGame::GeometryModelData) : nullptr;
    data = data && isKindOf(data, VrGame::NiGeometryDataRtti) ? data : nullptr;

    auto positions = data ? field<const VrVector3*>(data, VrGame::GeometryDataVertices) : nullptr;
    uint32_t vertices = data ? field<uint16_t>(data, VrGame::GeometryDataVertexCount) : 0;

    if (positions && vertices) {
      WorldFrame world = worldFrame(button);

      for (uint32_t i = 0; i < vertices; i++)
        extend(world.toWorld(positions[i]));

      return plate;
    }

    // Without its vertices, the button's bound
    VrVector3 centre;
    float radius = 0.0f;

    if (buttonBound(manager, index, centre, radius)) {
      VrVector3 axes[] = { frame.normal, frame.u, frame.v };

      for (const VrVector3& axis : axes) {
        extend(centre + axis * radius);
        extend(centre - axis * radius);
      }
    }

    return plate;
  }


  uint8_t* VrPipBoyTouchGame::shownMap() {
    if (!reinterpret_cast<IsTopMenuFn>(VrGame::IsTopMenu)(VrGame::MapMenuId))
      return nullptr;

    auto map = *reinterpret_cast<uint8_t* const*>(VrGame::MapMenuInstance);

    if (!map || !field<void*>(map, VrGame::MapMenuMapTile))
      return nullptr;

    uint32_t page = field<uint32_t>(map, VrGame::MapMenuPage);
    return page == VrGame::MapMenuLocalMapPage || page == VrGame::MapMenuWorldMapPage ? map : nullptr;
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

    touch->m_hoverCalls = VrGameMemory::matches(VrGame::TileSetFloat,
      VrGame::TileSetFloatPrologue, sizeof(VrGame::TileSetFloatPrologue));

    touch->m_mapCalls = touch->m_hoverCalls
      && VrGameMemory::matches(VrGame::IsTopMenu, VrGame::IsTopMenuPrologue, sizeof(VrGame::IsTopMenuPrologue))
      && VrGameMemory::matches(VrGame::MapMenuZoom, VrGame::MapMenuZoomPrologue, sizeof(VrGame::MapMenuZoomPrologue))
      && VrGameMemory::matches(VrGame::PipBoyMouseX, VrGame::PipBoyMousePrologue, sizeof(VrGame::PipBoyMousePrologue))
      && VrGameMemory::matches(VrGame::PipBoyMouseY, VrGame::PipBoyMousePrologue, sizeof(VrGame::PipBoyMousePrologue));

    Logger::info("VR: The Pip-Boy's screen can be touched with the right index finger");

    if (!touch->m_mapCalls)
      Logger::warn("VR: The Pip-Boy map's calls were not found, the finger scrolls it as a list");

    if (!touch->m_hoverCalls)
      Logger::warn("VR: The interface's tile calls were not found, the last line the finger hovered stays lit");

    return touch;
  }


  void VrPipBoyTouchGame::beginFrame() {
    m_point         = m_framePoint;
    m_hasPoint      = m_frameMeasured;
    m_frameMeasured = false;
    m_point.map     = m_hasPoint && m_mapCalls && shownMap();
    m_picked        = false;
    m_hasRay        = false;
  }


  void VrPipBoyTouchGame::setAim(const VrPipBoyAim& aim, bool active) {
    // Only a finger leaving with the cursor still its own: the gamepad
    // taking the menus back moves its focus, which takes the highlight off
    if (m_active && active && m_aim.kind != VrPipBoyAimKind::None && aim.kind == VrPipBoyAimKind::None)
      clearHover();

    m_aim    = aim;
    m_active = active;
  }


  void VrPipBoyTouchGame::clearHover() {
    // The interface takes the highlight off the tile under the mouse only
    // when its next pick differs, and it picks only while the cursor moves:
    // the finger gone, the cursor rests and the last tile stays lit. Done
    // as InterfaceManager::ClearOverTileTarget without the menu's leave, so
    // that the item under the finger stays selected for the gamepad's
    // buttons (the focus, pMouseOverTarget, is left alone).
    auto interfaceManager = *reinterpret_cast<uint8_t* const*>(VrGame::InterfaceManager);

    if (!interfaceManager || !m_hoverCalls)
      return;

    auto& overTile = *reinterpret_cast<void**>(interfaceManager + VrGame::InterfaceOverTile);
    auto& overMenu = *reinterpret_cast<void**>(interfaceManager + VrGame::InterfaceOverMenu);

    if (!overTile)
      return;

    reinterpret_cast<TileSetFloatFn>(VrGame::TileSetFloat)(overTile, VrGame::TileMouseover, 0.0f, true);

    // Forgotten, so that the finger lights the same tile again when it
    // comes back to it
    overTile = nullptr;
    overMenu = nullptr;

    if (!m_loggedHover) {
      Logger::info("VR: The right index finger left the Pip-Boy's screen, the line it hovered is no longer lit");
      m_loggedHover = true;
    }
  }


  void VrPipBoyTouchGame::updateMap(bool drags, int32_t zoom) {
    uint8_t* map = m_mapCalls ? shownMap() : nullptr;
    void* tile = map ? field<void*>(map, VrGame::MapMenuMapTile) : nullptr;
    auto setFloat = reinterpret_cast<TileSetFloatFn>(VrGame::TileSetFloat);

    if (!tile) {
      m_dragsMap = false;
      return;
    }

    for (int32_t i = 0; i < std::abs(zoom); i++)
      reinterpret_cast<ZoomMapFn>(VrGame::MapMenuZoom)(map, zoom > 0, VrGame::MapMenuWheelZoom);

    if (!drags) {
      // As the gamepad's stick once it is let go
      if (m_dragsMap) {
        setFloat(tile, VrGame::TileDragStartX, -1.0f, true);
        setFloat(tile, VrGame::TileDragStartY, -1.0f, true);
      }

      m_dragsMap = false;
      return;
    }

    // The point of the last pick, in the map's units
    void* interfaceManager = *reinterpret_cast<void* const*>(VrGame::InterfaceManager);

    if (!interfaceManager)
      return;

    double x = reinterpret_cast<PipBoyMouseFn>(VrGame::PipBoyMouseX)(interfaceManager);
    double y = reinterpret_cast<PipBoyMouseFn>(VrGame::PipBoyMouseY)(interfaceManager);

    if (m_dragsMap) {
      // The map moves with the finger, as the stick's drag does the other way
      setFloat(tile, VrGame::TileDragStartX, 1.0f, true);
      setFloat(tile, VrGame::TileDragStartY, 1.0f, true);
      setFloat(tile, VrGame::TileDragDeltaX, float(x - m_mapMouseX), true);
      setFloat(tile, VrGame::TileDragDeltaX, 0.0f, true);
      setFloat(tile, VrGame::TileDragDeltaY, float(y - m_mapMouseY), true);
      setFloat(tile, VrGame::TileDragDeltaY, 0.0f, true);
    } else if (!m_loggedMap) {
      Logger::info("VR: The right index finger drags the Pip-Boy's map");
      m_loggedMap = true;
    }

    m_dragsMap  = true;
    m_mapMouseX = x;
    m_mapMouseY = y;
  }


  VrVector3 VrPipBoyTouchGame::measure(const VrVector3& fingertip, const VrVector3& elbow, const VrVector3& wrist) {
    m_hasRay = false;

    uint8_t* manager = pipBoy();
    auto geometry = manager ? field<const uint8_t*>(manager, VrGame::RenderedMenuScreen) : nullptr;
    const VrTouchScreen* fitted = geometry ? screen(geometry) : nullptr;

    if (!fitted) {
      m_inFront = false;
      m_resting = false;
      return VrVector3();
    }

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

    // The tab button the finger is over, within reach of its top
    ScreenFrame screenFrame;
    screenFrame.centre = centre;
    screenFrame.normal = normal;
    screenFrame.u      = frame.rotate * faced.uAxis;
    screenFrame.v      = frame.rotate * faced.vAxis;

    VrVector3 fromCentre = fingertip - centre;
    VrVector2 fingerAt   = { vrDot(fromCentre, screenFrame.u), vrDot(fromCentre, screenFrame.v) };
    float fingerHeight   = vrDot(fromCentre, normal);
    float buttonMargin   = ButtonMargin * VrGameUnitsPerMetre;

    for (uint32_t i = 0; i < VrGame::PipBoyTabButtonCount; i++) {
      ButtonPlate plate = buttonPlate(manager, i, screenFrame);

      bool over = plate.valid
        && fingerAt.x >= plate.min.x - buttonMargin && fingerAt.x <= plate.max.x + buttonMargin
        && fingerAt.y >= plate.min.y - buttonMargin && fingerAt.y <= plate.max.y + buttonMargin;

      float distance = (fingerHeight - plate.height) / VrGameUnitsPerMetre;

      if (over && distance <= ButtonReach && distance >= -RestDepth
       && (point.button < 0 || distance < point.buttonDistance)) {
        point.button         = int32_t(i);
        point.buttonDistance = distance;
      }
    }

    // The glass and the buttons stop a finger that came from the front,
    // until it is lifted or slides off them
    VrVector2 margin = {
      (faced.uvMax.x - faced.uvMin.x) * RestMargin,
      (faced.uvMax.y - faced.uvMin.y) * RestMargin };

    bool overScreen = point.uv.x >= faced.uvMin.x - margin.x && point.uv.x <= faced.uvMax.x + margin.x
                   && point.uv.y >= faced.uvMin.y - margin.y && point.uv.y <= faced.uvMax.y + margin.y
                   && point.distance >= -RestDepth;
    bool overButton = !overScreen && point.button >= 0;
    bool over = overScreen || overButton;
    float surfaceDistance = overScreen ? point.distance : point.buttonDistance;

    if (!m_frameMeasured) {
      m_framePoint    = point;
      m_frameMeasured = true;

      bool front = surfaceDistance >= FingerPad;
      m_resting  = over && !front && (m_resting || m_inFront);
      m_inFront  = over && front;
    }

    VrVector3 push;

    if (m_resting && over && surfaceDistance < FingerPad) {
      float depth = FingerPad - surfaceDistance;
      push = normal * (depth * VrGameUnitsPerMetre);

      bool& logged = overScreen ? m_loggedRest : m_loggedButtonRest;

      if (!logged) {
        Logger::info(str::format("VR: The right index finger rests on the Pip-Boy's ",
          overScreen ? "screen" : "tab button", ", the hand drawn ", depth, " m in front of its controller"));
        logged = true;
      }
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

    return push;
  }


  const VrTouchScreen* VrPipBoyTouchGame::screen(const uint8_t* geometry) {
    auto data = isKindOf(geometry, VrGame::NiGeometryRtti)
      ? field<const uint8_t*>(geometry, VrGame::GeometryModelData) : nullptr;
    data = data && isKindOf(data, VrGame::NiGeometryDataRtti) ? data : nullptr;

    auto positions = data ? field<const VrVector3*>(data, VrGame::GeometryDataVertices) : nullptr;
    auto uvs       = data ? field<const VrVector2*>(data, VrGame::GeometryDataTexCoords) : nullptr;
    uint32_t vertices = data ? field<uint16_t>(data, VrGame::GeometryDataVertexCount) : 0;

    if (!positions || !uvs || !vertices) {
      if (!m_loggedScreen) {
        Logger::warn(str::format("VR: The Pip-Boy's screen (", rttiName(geometry), ", data ", rttiName(data),
          ") has no vertices with texture coordinates, it cannot be touched"));
      }

      m_loggedScreen = true;
      return nullptr;
    }

    // The game scales the screen's texture coordinates once, in place
    bool same = data == m_cache.data && vertices == m_cache.vertices
      && !std::memcmp(&uvs[0], &m_cache.firstUv, sizeof(VrVector2))
      && !std::memcmp(&uvs[vertices - 1], &m_cache.lastUv, sizeof(VrVector2));

    if (!same) {
      m_cache.data     = data;
      m_cache.vertices = vertices;
      m_cache.firstUv  = uvs[0];
      m_cache.lastUv   = uvs[vertices - 1];
      m_cache.screen   = vrFitTouchScreen(positions, uvs, vertices);

      const VrTouchScreen& fit = m_cache.screen;

      if (!fit.valid) {
        Logger::warn(str::format("VR: The Pip-Boy's screen (", rttiName(geometry),
          ") is not a surface with texture coordinates, it cannot be touched"));
      } else {
        float scale = field<float>(geometry, VrGame::ObjectWorldScale) / VrGameUnitsPerMetre;
        Logger::info(str::format("VR: The Pip-Boy's screen (", rttiName(geometry), ") fitted from ", vertices,
          " vertices, texture ", fit.uvMin.x, ", ", fit.uvMin.y, " to ", fit.uvMax.x, ", ", fit.uvMax.y,
          ", ", vrLength(fit.alongU) * (fit.uvMax.x - fit.uvMin.x) * scale, " by ",
          vrLength(fit.alongV) * (fit.uvMax.y - fit.uvMin.y) * scale, " m, bulging ", fit.bulge * scale, " m"));
      }
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
