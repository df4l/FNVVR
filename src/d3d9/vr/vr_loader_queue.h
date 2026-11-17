#pragma once

namespace dxvk {

  /**
   * \brief Skips the model loader's walk over an empty delayed queue
   *
   * Every frame, ModelLoader::UpdateReferencesToQueue walks the 5039 buckets
   * of its delayed reference queue under the map's lock, even when the queue
   * holds nothing: about 3 % of the game thread in the profile of
   * 2026-11-11. Over an empty map the walk only returns true ("finished"),
   * so the call is replaced by that result when the map's own count is 0.
   * An entry another thread adds meanwhile is taken by the next walk, as it
   * would be when added behind the walk's position. See
   * findings/performance.md (B4).
   *
   * The call is taken only when it still reaches the game's function and
   * that function holds its original start, so a plugin's rewrite wins.
   */
  class VrLoaderQueue {

  public:

    /**
     * \brief Redirects the call of the secondary queue callback
     */
    static void install();

  };

}
