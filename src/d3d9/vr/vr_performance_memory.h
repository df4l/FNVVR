#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "vr_performance_governor.h"

namespace dxvk {

  /**
   * \brief The levels the performance mode settled at, per headset and rate
   *
   * Each headset at each refresh rate needs other levels to hold its rate:
   * the mode remembers, in \c fnvvr\\performance.ini, the levels it held
   * most in its last minute of play, and starts from them the next time,
   * instead of finding them again through stutters. The governor still
   * raises them when there is room and lowers them when there is not.
   *
   * One line per headset: \c model\@rate=distance gpu.
   */
  class VrPerformanceMemory {

  public:

    /**
     * \brief Key of a headset at a display period
     *
     * \param [in] model The headset's model name
     * \param [in] periodMs Its display period, in milliseconds
     */
    static std::string key(const std::string& model, double periodMs);

    /**
     * \brief The level held for the longest time
     *
     * \param [in] ms Time held at each level
     * \param [in] count Number of levels
     * \returns The level, the lowest of those held equally long
     */
    static uint32_t mostHeld(const double* ms, size_t count);

    /**
     * \brief Reads the file's text, replacing what was known
     *
     * Lines that cannot be read are left out.
     */
    void parse(const std::string& text);

    /**
     * \brief The file's text
     */
    std::string format() const;

    /**
     * \brief Finds the levels of a key
     * \returns \c false if the key is unknown
     */
    bool find(const std::string& key, VrPerformanceLevels& levels) const;

    /**
     * \brief Sets the levels of a key
     * \returns \c true if they changed
     */
    bool store(const std::string& key, const VrPerformanceLevels& levels);

  private:

    std::vector<std::pair<std::string, VrPerformanceLevels>> m_entries;

  };

}
