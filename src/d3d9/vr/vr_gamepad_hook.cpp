#include <windows.h>
#include <xinput.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_gamepad_head.h"
#include "vr_gamepad_hook.h"
#include "vr_virtual_gamepad.h"

namespace dxvk {

  namespace {

    using XInputGetStateFn = DWORD (WINAPI*)(DWORD index, XINPUT_STATE* state);

    // The game's own import thunk, which reaches XInputGetState
    XInputGetStateFn g_originalGetState = reinterpret_cast<XInputGetStateFn>(VrGame::XInputGetStateThunk);

    DWORD g_packetNumber = 0;

    VrGamepadHold g_hold;

    VrGamepadHead g_head;

    VrGamepadHook::Options g_options;

    bool g_loggedHidden = false;

    bool       g_controllersActive = false;
    bool       g_cursorMode        = false;
    VrPadState g_controllerPad;
    bool       g_loggedControllers = false;

    // Replaces the gamepad connection message boxes. The caller pops the
    // arguments, so ignoring them is safe.
    void __cdecl skipMessageBox() { }

    DWORD reportControllers(XINPUT_STATE* state) {
      if (!g_loggedControllers) {
        Logger::info("VR: The VR controllers are in use, the game sees them as a gamepad");
        g_loggedControllers = true;
      }

      *state = XINPUT_STATE();
      state->dwPacketNumber = ++g_packetNumber;

      XINPUT_GAMEPAD& pad = state->Gamepad;
      pad.wButtons      = g_controllerPad.buttons;
      pad.bLeftTrigger  = g_controllerPad.leftTrigger;
      pad.bRightTrigger = g_controllerPad.rightTrigger;
      pad.sThumbLX      = g_controllerPad.thumbLeftX;
      pad.sThumbLY      = g_controllerPad.thumbLeftY;
      pad.sThumbRX      = g_controllerPad.thumbRightX;
      pad.sThumbRY      = g_controllerPad.thumbRightY;
      return ERROR_SUCCESS;
    }

    // Also true for a key that was pressed and released since the previous
    // sample, so that a short tap is not missed
    bool isDown(int key) {
      return (GetAsyncKeyState(key) & 0x8001) != 0;
    }

    VrGamepadKeys sampleKeys() {
      VrGamepadKeys keys;
      keys.dpadUp    = isDown('I');
      keys.dpadDown  = isDown('K');
      keys.dpadLeft  = isDown('J');
      keys.dpadRight = isDown('L');
      keys.start     = isDown('P');
      keys.back      = isDown('O');
      keys.a         = isDown(VK_RETURN);
      keys.b         = isDown('U');
      keys.x         = isDown('H');
      keys.y         = isDown('Y');
      return keys;
    }

    void applyHeadControl(XINPUT_STATE* state) {
      XINPUT_GAMEPAD& pad = state->Gamepad;

      VrGamepadSample sample;
      sample.buttons      = pad.wButtons;
      sample.thumbLeftX   = pad.sThumbLX;
      sample.thumbLeftY   = pad.sThumbLY;
      sample.thumbRightX  = pad.sThumbRX;
      sample.thumbRightY  = pad.sThumbRY;
      sample.leftTrigger  = pad.bLeftTrigger;
      sample.rightTrigger = pad.bRightTrigger;

      g_head.apply(sample);

      pad.wButtons      = sample.buttons;
      pad.sThumbLX      = sample.thumbLeftX;
      pad.sThumbLY      = sample.thumbLeftY;
      pad.sThumbRX      = sample.thumbRightX;
      pad.sThumbRY      = sample.thumbRightY;
      pad.bLeftTrigger  = sample.leftTrigger;
      pad.bRightTrigger = sample.rightTrigger;
    }

    DWORD WINAPI getStateHook(DWORD index, XINPUT_STATE* state) {
      DWORD result = g_originalGetState(index, state);

      if (index != 0)
        return result;

      if (g_options.controllers && g_controllersActive)
        return reportControllers(state);

      if (g_options.hidePad) {
        if (result == ERROR_SUCCESS && !g_loggedHidden) {
          Logger::info("VR: A gamepad is connected, hiding it from the game");
          g_loggedHidden = true;
        }

        *state = XINPUT_STATE();
        return ERROR_DEVICE_NOT_CONNECTED;
      }

      if (result == ERROR_SUCCESS) {
        if (g_options.headControl)
          applyHeadControl(state);

        return result;
      }

      if (!g_options.virtualPad)
        return result;

      if (g_packetNumber == 0)
        Logger::info("VR: The game polled the gamepad, reporting the virtual one");

      *state = XINPUT_STATE();
      state->dwPacketNumber = ++g_packetNumber;
      state->Gamepad.wButtons = g_hold.update(vrGamepadButtons(sampleKeys()));
      return ERROR_SUCCESS;
    }

    // The interface's own check: in cursor mode the interface sees no
    // gamepad and switches to the mouse, while the game keeps the gamepad
    DWORD WINAPI getInterfaceStateHook(DWORD index, XINPUT_STATE* state) {
      DWORD result = getStateHook(index, state);

      if (index != 0 || !g_cursorMode || !g_controllersActive)
        return result;

      *state = XINPUT_STATE();
      return ERROR_DEVICE_NOT_CONNECTED;
    }

  }


  bool VrGamepadHook::install(const Options& options) {
    g_options = options;

    bool patched = VrGameMemory::redirectCall(VrGame::XInputPollCallSite,
      VrGame::XInputGetStateThunk, reinterpret_cast<const void*>(&getStateHook));

    // The interface checks for a gamepad on its own, so hiding one or
    // reporting the controllers has to cover those calls as well
    if (patched && (options.hidePad || options.controllers)) {
      struct InterfaceSite {
        uintptr_t   site;
        const void* hook;
      };

      // InterfaceManager::Update's check decides the interface's mode each frame
      const InterfaceSite interfaceSites[] = {
        { VrGame::XInputInterfaceUpdateCallSite, reinterpret_cast<const void*>(&getInterfaceStateHook) },
        { VrGame::XInputInterfaceSetupCallSite,  reinterpret_cast<const void*>(&getStateHook) },
        { VrGame::XInputStartMenuCallSite,       reinterpret_cast<const void*>(&getStateHook) },
      };

      for (const InterfaceSite& entry : interfaceSites) {
        if (!VrGameMemory::redirectCall(entry.site, VrGame::XInputGetStateThunk, entry.hook))
          Logger::warn(str::format("VR: Gamepad check at 0x", std::hex, entry.site, " was not found, the menu cursor may stay hidden"));
      }
    }

    // The VR controllers come and go with tracking, and the game would show
    // a message box each time the gamepad appears or disappears
    if (patched && options.controllers) {
      const uintptr_t messageSites[] = {
        VrGame::PadLostMessageCallSite,
        VrGame::PadConnectedMessageCallSite,
      };

      for (uintptr_t site : messageSites) {
        if (!VrGameMemory::redirectCall(site, VrGame::ShowMessageBox, reinterpret_cast<const void*>(&skipMessageBox)))
          Logger::warn(str::format("VR: Gamepad message at 0x", std::hex, site, " was not found, it stays enabled"));
      }
    }

    if (patched && options.hidePad)
      Logger::info(str::format("VR: Gamepad hook installed, gamepads are hidden from the game",
        options.controllers ? ", the VR controllers act as one" : ""));
    else if (patched)
      Logger::info(str::format("VR: Gamepad hook installed (virtual gamepad ", options.virtualPad ? "on" : "off",
        ", head control with LB + RB ", options.headControl ? "on" : "off", ")"));
    else
      Logger::info("VR: The game's gamepad polling call was not found, gamepad hook is disabled");

    return patched;
  }


  void VrGamepadHook::setControllerPad(bool active, const VrPadState& pad) {
    g_controllersActive = active;
    g_controllerPad     = pad;
  }


  void VrGamepadHook::setCursorMode(bool enabled) {
    g_cursorMode = enabled;
  }


  VrEmulatorInput VrGamepadHook::headInput() {
    return g_head.input();
  }

}
