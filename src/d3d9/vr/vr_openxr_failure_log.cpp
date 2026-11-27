#include "vr_openxr_failure_log.h"

namespace dxvk {

  VrXrFailureVerdict VrXrFailureLog::count(const std::string& message) {
    uint32_t& count = m_counts[message];

    if (count >= LoggedTimes)
      return VrXrFailureVerdict::Skip;

    count += 1;

    return count < LoggedTimes
      ? VrXrFailureVerdict::Log
      : VrXrFailureVerdict::LogLast;
  }


  void VrXrFailureLog::clear() {
    m_counts.clear();
  }

}
