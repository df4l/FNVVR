#pragma once

#include <cstdint>

#include "vr_game_addresses.h"

namespace dxvk {

  /**
   * \brief The depth value the game clears its depth buffers to
   *
   * 1.0 normally; 0.0 with a reversed depth range such as LOD Flicker
   * Fix's, where nearer means greater. Depth buffers the mod clears for
   * the game's own draws must use this value, or those draws fail their
   * depth test everywhere.
   */
  inline float vrGameDepthClear() {
    auto renderer = *reinterpret_cast<const uintptr_t*>(VrGame::Renderer);

    return renderer
      ? *reinterpret_cast<const float*>(renderer + VrGame::RendererDepthClear)
      : 1.0f;
  }

  /**
   * \brief Whether the game's depth goes from 1 near to 0 far
   *
   * The depth row of the game's projection matches, so draws tested
   * against the game's depth with that row must swap their test.
   */
  inline bool vrGameDepthReversed() {
    return vrGameDepthClear() < 0.5f;
  }

}
