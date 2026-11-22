#pragma once

#include <algorithm>
#include <array>
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
   * becomes the player's new value. Water reflections and grass have no
   * run-time copy: their game settings are written, and given back while
   * the game saves its INI files, menus and the end of the game included.
   *
   * Distance levels scale the fade distances of objects, items, actors,
   * lights and grass, and turn water reflections off from WaterOffDistanceLevel; GPU
   * levels scale those of specular highlights and shadows, then lower the
   * resolution the eyes' world is drawn at (applied by VrRenderScale) by
   * 5 % per step, so that each change is small.
   */
  class VrPerformanceLevers {

  public:

    VrPerformanceLevers() = default;

    ~VrPerformanceLevers();

    VrPerformanceLevers             (const VrPerformanceLevers&) = delete;
    VrPerformanceLevers& operator = (const VrPerformanceLevers&) = delete;

    static constexpr uint32_t DistanceSteps = 4;
    static constexpr uint32_t GpuSteps      = 8;
    static constexpr uint32_t WaterOffDistanceLevel = 2;

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
     * \brief Gives the lowered game settings back while the game saves them
     *
     * Called by the hook on the game's INI saves, followed by retakeSettings.
     */
    void lendSettings();

    /**
     * \brief Lowers again what lendSettings gave back
     */
    void retakeSettings();

    /**
     * \brief Scale of a distance level, 1 for level 0
     */
    static float distanceScale(uint32_t level) {
      constexpr std::array<float, DistanceSteps + 1> scales = { 1.0f, 0.85f, 0.72f, 0.6f, 0.5f };
      return scales[std::min<size_t>(level, DistanceSteps)];
    }

    /**
     * \brief Shadow and specular fade scale of a GPU level, 1 for level 0
     */
    static float gpuScale(uint32_t level) {
      constexpr std::array<float, GpuSteps + 1> scales = { 1.0f, 0.7f, 0.5f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f };
      return scales[std::min<size_t>(level, GpuSteps)];
    }

    /**
     * \brief World resolution of a GPU level, as a share of each side
     */
    static float renderScale(uint32_t level) {
      constexpr std::array<float, GpuSteps + 1> scales = { 1.0f, 1.0f, 1.0f, 0.95f, 0.9f, 0.85f, 0.8f, 0.75f, 0.7f };
      return scales[std::min<size_t>(level, GpuSteps)];
    }

    /**
     * \brief Whether a distance level turns water reflections off
     */
    static bool waterReflectionsOff(uint32_t level) {
      return level >= WaterOffDistanceLevel;
    }

  private:

    struct Value {
      float* address = nullptr;
      float  floor   = 0.0f;
      float  base    = 0.0f;
      float  written = 0.0f;
      bool   held    = false;
      /// A game setting, which the game saves in its INI files
      bool   setting = false;
      /// Given back while the game saves its INI files
      bool   lent    = false;
    };

    struct Flag {
      uint8_t* address = nullptr;
      uint8_t  base    = 0;
      bool     held    = false;
      bool     lent    = false;
    };

    std::vector<Value> m_distanceValues;
    std::vector<Value> m_gpuValues;
    Flag               m_waterReflections;
    bool               m_hooked = false;

    bool hookSettingsSaves();

    static void scale(Value& value, float factor);

    static void give(Value& value);

    static void clear(Flag& flag);

    static void give(Flag& flag);

  };

}
