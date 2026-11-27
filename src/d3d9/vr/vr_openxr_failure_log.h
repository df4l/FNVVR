#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace dxvk {

  /**
   * \brief How a failed OpenXR call is logged
   */
  enum class VrXrFailureVerdict : uint32_t {
    Log,      ///< Logged as it is
    LogLast,  ///< Logged with a note that the next ones will not be
    Skip,     ///< Not logged
  };

  /**
   * \brief Limits how often the same failure is logged
   *
   * Most OpenXR calls run on every frame: a failure that persists would
   * otherwise write several lines per frame from the game thread. Each
   * message is logged a few times, then no more until the counts are
   * cleared (when a new session starts).
   */
  class VrXrFailureLog {

  public:

    static constexpr uint32_t LoggedTimes = 3;

    /**
     * \brief Counts one failure and tells how to log it
     * \param [in] message The full message, result included
     */
    VrXrFailureVerdict count(const std::string& message);

    void clear();

  private:

    std::unordered_map<std::string, uint32_t> m_counts;

  };

}
