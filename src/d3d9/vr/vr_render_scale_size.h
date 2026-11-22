#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Smallest world render scale the performance mode uses
   */
  constexpr float VrMinRenderScale = 0.5f;

  /**
   * \brief Size of the world's targets at a render scale
   *
   * Rounded to a multiple of 8 pixels, so the quarter-size targets of the
   * game's blurs keep the same proportions, and never below 64 pixels.
   *
   * \param [in] screen The game's screen size (its back buffer)
   * \param [in] scale Share of each side, clamped to [VrMinRenderScale, 1]
   */
  VrExtent vrScaledWorldExtent(VrExtent screen, float scale);

  /**
   * \brief Scales a texture the game sizes after its screen
   *
   * The game sizes its world targets as the screen, or a half or a quarter
   * of it on each side (findings/performance.md §16). Such a size becomes
   * the same share of the scaled screen; any other size stays.
   *
   * \param [in] screen The game's screen size
   * \param [in] scaled The scaled screen size, from vrScaledWorldExtent
   * \param [in,out] width Texture width
   * \param [in,out] height Texture height
   * \returns \c true if the size was one of the screen's and was scaled
   */
  bool vrScaleScreenTexture(VrExtent screen, VrExtent scaled, uint32_t& width, uint32_t& height);

  /**
   * \brief Weight of the neighbours in the sharpening of a stretched world
   *
   * The lower the scale, the more the stretch softens the image and the
   * stronger the sharpening, up to a weight of -1/5.5 at 70 % and below.
   * Light at 95 %, none at the game's own size.
   *
   * \param [in] scale Share of each side the world was drawn at
   * \returns A negative weight, 0 for no sharpening
   */
  float vrSharpenWeight(float scale);

}
