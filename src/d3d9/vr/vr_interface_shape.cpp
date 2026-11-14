#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"
#include "vr_interface_shape.h"

namespace dxvk {

  namespace {

    using ScreenSizeFn   = int32_t  (__cdecl*)();
    using RendererSizeFn = uint32_t (__fastcall*)(void* renderer, void* unused);

    /// Height of the interface in its units on a landscape screen
    constexpr double InterfaceHeight = 960.0;
    /// Width of the tiles that fade out, in units (retn1280.0 0x00706E80)
    constexpr double FadeOutWidth    = 1280.0;

    /// Read by the game's picks on rendered menus in place of its 4/3
    double g_pickAspect = VrInterfaceMinAspect;

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
      && VrGameMemory::matches(VrGame::PipBoyPickAspectSite, pickStart, pickLength);

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
      && VrGameMemory::replaceCode(VrGame::PipBoyPickAspectSite, pickStart, pickInstruction, pickLength);

    if (!replaced) {
      Logger::err("VR: The game's interface size functions could not be replaced");
      return false;
    }

    Logger::info("VR: The interface is laid out at 4:3 at least and stretched over a narrower window");
    return true;
  }

}
