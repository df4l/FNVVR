#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_interface_shape.h"

namespace dxvk {

  namespace {

    using ScreenSizeFn   = int32_t  (__cdecl*)();
    using RendererSizeFn = uint32_t (__fastcall*)(void* renderer, void* unused);
    using GroupSizeFn    = uint32_t (__fastcall*)(void* group, void* unused, uint32_t index);
    using RadialTestFn   = bool     (__fastcall*)(void* tile, void* unused, float x, float y);

    /// Bytes of a relative call and of the code a texture size site keeps
    constexpr uint8_t CallOpcode       = 0xE8;
    constexpr size_t  CallLength       = 5;
    constexpr uint8_t PushIndexMoveGroup[] = { 0x6A, 0x00, 0x8B, 0xCF };
    constexpr uint8_t Nop              = 0x90;

    /// Height of the interface in its units on a landscape screen
    constexpr double InterfaceHeight = 960.0;
    /// Width of the tiles that fade out, in units (retn1280.0 0x00706E80)
    constexpr double FadeOutWidth    = 1280.0;

    /// Read by the game's picks on rendered menus in place of its 4/3
    double g_pickAspect = VrInterfaceMinAspect;

    /// What the radial tiles' hit test reached before us
    uintptr_t g_radialContains = VrGame::RadialTileContains;

    double interfaceAspect() {
      uint32_t width  = uint32_t(std::max(reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenWidth)(), 0));
      uint32_t height = uint32_t(std::max(reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenHeight)(), 0));

      // The interface is laid out, so asks for its size, before anything
      // can be picked on it
      g_pickAspect = vrComputeRenderedMenuPickAspect(width, height);
      return vrComputeInterfaceAspect(width, height);
    }

    // InterfaceManager::GetScreenWidth: the game's landscape rule, 960 units
    // times the aspect
    float __cdecl interfaceWidth() {
      return float(InterfaceHeight * interfaceAspect());
    }

    // InterfaceManager::GetScreenHeight: always landscape
    float __cdecl interfaceHeight() {
      return float(InterfaceHeight);
    }

    // CalculateScaledHeight: 1280 units over the aspect, as the game computes
    // it from the render target
    float __cdecl scaledHeight() {
      return float(FadeOutWidth / interfaceAspect());
    }

    // The renderer's width as InitSceneGraph reads it for the interface
    // camera, widened to the interface's aspect
    uint32_t __fastcall cameraScreenWidth(void* renderer, void* unused) {
      uint32_t width  = reinterpret_cast<RendererSizeFn>(VrGame::RendererScreenWidth)(renderer, unused);
      uint32_t height = reinterpret_cast<RendererSizeFn>(VrGame::RendererScreenHeight)(renderer, unused);

      return vrComputeInterfaceExtent({ width, height }).width;
    }

    // TESRenderer::GetScreenWidth where the game derives a shape from the
    // window: the width of the 4:3 window the interface is laid out on
    int32_t __cdecl layoutScreenWidth() {
      int32_t width  = reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenWidth)();
      int32_t height = reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenHeight)();

      if (width <= 0 || height <= 0)
        return width;

      return int32_t(vrComputeInterfaceExtent({ uint32_t(width), uint32_t(height) }).width);
    }

    // Interface::GetRealScreenWidth for the loading menu's aspect
    double __cdecl layoutRealScreenWidth() {
      return double(layoutScreenWidth());
    }

    // RadialTile's hit test (the companion wheel's slices): the game scales
    // the cursor's height by the widths' ratio, so it is given a height
    // that lands at the right interface unit on a stretched interface
    bool __fastcall radialContains(void* tile, void* unused, float x, float y) {
      uint32_t width  = uint32_t(std::max(reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenWidth)(), 0));
      uint32_t height = uint32_t(std::max(reinterpret_cast<ScreenSizeFn>(VrGame::GameScreenHeight)(), 0));

      float scaledY = float(double(y) * vrComputeRadialPickScaleY(width, height));
      return reinterpret_cast<RadialTestFn>(g_radialContains)(tile, unused, x, scaledY);
    }

    // NiRenderTargetGroup::GetWidth (__thiscall, index) for the size of the
    // rendered menus' textures
    uint32_t __fastcall interfaceTextureWidth(void* group, void* unused, uint32_t index) {
      void** vtable = *reinterpret_cast<void***>(group);
      auto getWidth  = reinterpret_cast<GroupSizeFn>(vtable[VrGame::RenderTargetGroupGetWidth  / sizeof(void*)]);
      auto getHeight = reinterpret_cast<GroupSizeFn>(vtable[VrGame::RenderTargetGroupGetHeight / sizeof(void*)]);

      uint32_t width  = getWidth(group, unused, index);
      uint32_t height = getHeight(group, unused, index);
      return vrComputeInterfaceExtent({ width, height }).width;
    }

    // `push 0; mov ecx, edi; call interfaceTextureWidth`, then nops, in
    // place of the virtual call at a texture size site
    void buildTextureWidthCall(uintptr_t site, uint8_t (&code)[sizeof(VrGame::InterfaceTextureWidthCall)]) {
      std::memset(code, Nop, sizeof(code));
      std::memcpy(code, PushIndexMoveGroup, sizeof(PushIndexMoveGroup));

      uintptr_t call = site + sizeof(PushIndexMoveGroup);
      uint32_t offset = uint32_t(reinterpret_cast<uintptr_t>(&interfaceTextureWidth) - (call + CallLength));
      code[sizeof(PushIndexMoveGroup)] = CallOpcode;
      std::memcpy(&code[sizeof(PushIndexMoveGroup) + 1], &offset, sizeof(offset));
    }

    bool callsAllTo(const uintptr_t* sites, size_t count, uintptr_t target) {
      for (size_t i = 0; i < count; i++) {
        if (!VrGameMemory::callsTo(sites[i], target))
          return false;
      }

      return true;
    }

    bool redirectAll(const uintptr_t* sites, size_t count, uintptr_t target, const void* replacement) {
      for (size_t i = 0; i < count; i++) {
        if (!VrGameMemory::redirectCall(sites[i], target, replacement))
          return false;
      }

      return true;
    }

    /// The game's shape sites, checked then replaced together
    bool windowShapeSitesFound() {
      const uint8_t* textureCall = VrGame::InterfaceTextureWidthCall;
      size_t textureLength = sizeof(VrGame::InterfaceTextureWidthCall);

      return callsAllTo(VrGame::WindowShapeScreenWidthCallSites,
          std::size(VrGame::WindowShapeScreenWidthCallSites), VrGame::GameScreenWidth)
        && callsAllTo(VrGame::WindowShapeRendererWidthCallSites,
          std::size(VrGame::WindowShapeRendererWidthCallSites), VrGame::RendererScreenWidth)
        && VrGameMemory::callsTo(VrGame::LoadingMenuScreenWidthCallSite, VrGame::InterfaceRealScreenWidth)
        && std::all_of(std::begin(VrGame::InterfaceTextureWidthSites), std::end(VrGame::InterfaceTextureWidthSites),
          [=] (uintptr_t site) { return VrGameMemory::matches(site, textureCall, textureLength); });
    }

    bool replaceWindowShapeSites() {
      bool replaced = redirectAll(VrGame::WindowShapeScreenWidthCallSites,
          std::size(VrGame::WindowShapeScreenWidthCallSites), VrGame::GameScreenWidth,
          reinterpret_cast<const void*>(&layoutScreenWidth))
        && redirectAll(VrGame::WindowShapeRendererWidthCallSites,
          std::size(VrGame::WindowShapeRendererWidthCallSites), VrGame::RendererScreenWidth,
          reinterpret_cast<const void*>(&cameraScreenWidth))
        && VrGameMemory::redirectCall(VrGame::LoadingMenuScreenWidthCallSite, VrGame::InterfaceRealScreenWidth,
          reinterpret_cast<const void*>(&layoutRealScreenWidth));

      for (uintptr_t site : VrGame::InterfaceTextureWidthSites) {
        uint8_t code[sizeof(VrGame::InterfaceTextureWidthCall)];
        buildTextureWidthCall(site, code);

        replaced = replaced && VrGameMemory::replaceCode(site,
          VrGame::InterfaceTextureWidthCall, code, sizeof(code));
      }

      return replaced;
    }

  }


  bool VrInterfaceShape::install() {
    const uint8_t* sizeStart   = VrGame::InterfaceSizePrologue;
    const uint8_t* scaledStart = VrGame::InterfaceScaledHeightPrologue;
    size_t sizeLength   = sizeof(VrGame::InterfaceSizePrologue);
    size_t scaledLength = sizeof(VrGame::InterfaceScaledHeightPrologue);

    // fmul qword ptr [g_pickAspect]
    const uint8_t* pickStart = VrGame::PickAspectInstruction;
    size_t pickLength = sizeof(VrGame::PickAspectInstruction);
    uint8_t pickInstruction[sizeof(VrGame::PickAspectInstruction)] = { 0xDC, 0x0D };
    uint32_t pickOperand = uint32_t(reinterpret_cast<uintptr_t>(&g_pickAspect));
    std::memcpy(&pickInstruction[2], &pickOperand, sizeof(pickOperand));

    // Everything is checked before anything is patched: half of the shape
    // would draw the interface with a camera that does not fit its layout
    bool found = VrGameMemory::matches(VrGame::InterfaceWidth, sizeStart, sizeLength)
      && VrGameMemory::matches(VrGame::InterfaceHeight, sizeStart, sizeLength)
      && VrGameMemory::matches(VrGame::InterfaceScaledHeight, scaledStart, scaledLength)
      && VrGameMemory::callsTo(VrGame::InterfaceCameraWidthCallSite, VrGame::RendererScreenWidth)
      && VrGameMemory::callsTo(VrGame::InterfaceCameraHeightCallSite, VrGame::RendererScreenHeight)
      && VrGameMemory::matches(VrGame::RenderedMenuPickAspectSite, pickStart, pickLength)
      && VrGameMemory::matches(VrGame::PipBoyPickAspectSite, pickStart, pickLength)
      && windowShapeSitesFound();

    if (!found) {
      Logger::warn("VR: The game's interface size functions were not found or are changed by another module");
      return false;
    }

    bool replaced = VrGameMemory::redirectFunction(VrGame::InterfaceWidth, sizeStart, sizeLength,
        reinterpret_cast<const void*>(&interfaceWidth))
      && VrGameMemory::redirectFunction(VrGame::InterfaceHeight, sizeStart, sizeLength,
        reinterpret_cast<const void*>(&interfaceHeight))
      && VrGameMemory::redirectFunction(VrGame::InterfaceScaledHeight, scaledStart, scaledLength,
        reinterpret_cast<const void*>(&scaledHeight))
      && VrGameMemory::redirectCall(VrGame::InterfaceCameraWidthCallSite, VrGame::RendererScreenWidth,
        reinterpret_cast<const void*>(&cameraScreenWidth))
      && VrGameMemory::replaceCode(VrGame::RenderedMenuPickAspectSite, pickStart, pickInstruction, pickLength)
      && VrGameMemory::replaceCode(VrGame::PipBoyPickAspectSite, pickStart, pickInstruction, pickLength)
      && replaceWindowShapeSites();

    if (!replaced) {
      Logger::err("VR: The game's interface size functions could not be replaced");
      return false;
    }

    Logger::info("VR: The interface is laid out at 4:3 at least and stretched over a narrower window");

    if (!VrGameMemory::chainVirtual(VrGame::RadialTileContainsSlot, VrGame::RadialTileContains,
        reinterpret_cast<const void*>(&radialContains), &g_radialContains))
      Logger::warn("VR: The companion wheel's hit test was not found, its slices may not follow the pointer");

    return true;
  }

}
