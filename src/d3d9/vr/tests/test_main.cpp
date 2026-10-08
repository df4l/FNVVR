#include "test.h"

namespace dxvk::test {

  std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
  }


  int& failureCount() {
    static int count = 0;
    return count;
  }

}

int main() {
  int failedTests = 0;

  for (const auto& test : dxvk::test::registry()) {
    int before = dxvk::test::failureCount();
    test.func();

    bool ok = dxvk::test::failureCount() == before;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", test.name);

    if (!ok)
      failedTests++;
  }

  std::printf("%zu tests, %d failed\n", dxvk::test::registry().size(), failedTests);
  return failedTests ? 1 : 0;
}
