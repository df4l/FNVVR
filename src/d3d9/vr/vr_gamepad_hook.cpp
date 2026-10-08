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

    bool g_virtualPad  = true;
    bool g_headControl = true;

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

      if (result == ERROR_SUCCESS) {
        if (g_headControl)
          applyHeadControl(state);

        return result;
      }

      if (!g_virtualPad)
        return result;

      if (g_packetNumber == 0)
        Logger::info("VR: The game polled the gamepad, reporting the virtual one");

      *state = XINPUT_STATE();
      state->dwPacketNumber = ++g_packetNumber;
      state->Gamepad.wButtons = g_hold.update(vrGamepadButtons(sampleKeys()));
      return ERROR_SUCCESS;
    }

  }


  bool VrGamepadHook::install(bool virtualPad, bool headControl) {
    g_virtualPad  = virtualPad;
    g_headControl = headControl;

    bool patched = VrGameMemory::redirectCall(VrGame::XInputPollCallSite,
      VrGame::XInputGetStateThunk, reinterpret_cast<const void*>(&getStateHook));

    if (patched)
      Logger::info(str::format("VR: Gamepad hook installed (virtual gamepad ", virtualPad ? "on" : "off",
        ", head control with LB + RB ", headControl ? "on" : "off", ")"));
    else
      Logger::info("VR: The game's gamepad polling call was not found, gamepad hook is disabled");

    return patched;
  }


  VrEmulatorInput VrGamepadHook::headInput() {
    return g_head.input();
  }

}
