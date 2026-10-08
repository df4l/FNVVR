#include <windows.h>

#include "vr_emulator_keyboard.h"

namespace dxvk {

  namespace {

    // Mouse pixels per frame reported while a turn key is held
    constexpr float TurnPixelsPerFrame = 6.0f;

    // Also true for a key that was pressed and released since the previous
    // sample, so that a short tap, such as a synthesized one, is not missed
    bool isDown(int key) {
      return (GetAsyncKeyState(key) & 0x8001) != 0;
    }

    float axis(int positive, int negative) {
      return (isDown(positive) ? 1.0f : 0.0f) - (isDown(negative) ? 1.0f : 0.0f);
    }

  }


  VrEmulatorInput VrEmulatorKeyboard::sample() const {
    VrEmulatorInput input;
    input.moveRight   = axis(VK_RIGHT, VK_LEFT);
    input.moveForward = axis(VK_UP, VK_DOWN);
    input.moveUp      = axis(VK_PRIOR, VK_NEXT);
    input.mouseDeltaX = axis(VK_END, VK_HOME) * TurnPixelsPerFrame;
    input.mouseDeltaY = axis(VK_DELETE, VK_INSERT) * TurnPixelsPerFrame;
    input.recenter    = isDown(VK_BACK);
    return input;
  }

}
