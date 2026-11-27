#include "test.h"

#include "../vr_openxr_failure_log.h"

using namespace dxvk;


TEST_CASE(openxr_failure_is_logged_a_few_times_then_skipped) {
  VrXrFailureLog log;

  for (uint32_t i = 1; i < VrXrFailureLog::LoggedTimes; i++)
    CHECK(log.count("xrWaitFrame failed") == VrXrFailureVerdict::Log);

  CHECK(log.count("xrWaitFrame failed") == VrXrFailureVerdict::LogLast);

  for (uint32_t i = 0; i < 100; i++)
    CHECK(log.count("xrWaitFrame failed") == VrXrFailureVerdict::Skip);
}


TEST_CASE(openxr_failures_are_counted_per_message) {
  VrXrFailureLog log;

  for (uint32_t i = 0; i < 10; i++)
    log.count("xrWaitFrame failed");

  CHECK(log.count("xrEndFrame failed") == VrXrFailureVerdict::Log);
}


TEST_CASE(openxr_failure_is_logged_again_after_clear) {
  VrXrFailureLog log;

  for (uint32_t i = 0; i < 10; i++)
    log.count("xrWaitFrame failed");

  log.clear();
  CHECK(log.count("xrWaitFrame failed") == VrXrFailureVerdict::Log);
}
