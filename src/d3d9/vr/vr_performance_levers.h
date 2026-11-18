#pragma once

#include <cstdint>
#include <vector>

#include "vr_performance_governor.h"

namespace dxvk {

  /**
   * \brief Lowers the game's view distances for the performance mode
   *
   * Writes only the run-time copies the game reads every frame, never its
   * settings, so the player's saved values stay (findings/performance.md
   * §14.1). Each value is scaled from the player's own: a value that is no
   * longer the one written last was changed by the game's settings menu and
   * becomes the player's new value.
   *
   * Distance levels scale the fade distances of objects, items, actors and
   * lights; GPU levels those of specular highlights and shadows.
   */
  class VrPerformanceLevers {

  public:

    static constexpr uint32_t DistanceSteps = 4;
    static constexpr uint32_t GpuSteps      = 3;

    /**
     * \brief Checks the game code that holds the values
     * \returns \c false if no lever can be used
     */
    bool initialize();

    /**
     * \brief Scales the values for the levels
     *
     * Level 0 gives the player's values back.
     */
    void apply(const VrPerformanceLevels& levels);

    /**
     * \brief Gives every value back to the player's
     */
    void restore();

    /**
     * \brief Scale of a distance level, 1 for level 0
     */
    static float distanceScale(uint32_t level);

    /**
     * \brief Scale of a GPU level, 1 for level 0
     */
    static float gpuScale(uint32_t level);

  private:

    struct Value {
      float* address = nullptr;
      float  base    = 0.0f;
      float  written = 0.0f;
      bool   held    = false;
    };

    std::vector<Value> m_distanceValues;
    std::vector<Value> m_gpuValues;

    static void scale(Value& value, float factor);

    static void give(Value& value);

  };

}
