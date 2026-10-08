#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace dxvk::test {

  struct TestCase {
    const char*           name;
    std::function<void()> func;
  };

  std::vector<TestCase>& registry();
  int& failureCount();

  struct Registrar {
    Registrar(const char* name, std::function<void()> func) {
      registry().push_back({ name, std::move(func) });
    }
  };

}

#define TEST_CASE(name) \
  static void name(); \
  static dxvk::test::Registrar name##_registrar(#name, name); \
  static void name()

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      dxvk::test::failureCount()++; \
    } \
  } while (0)

#define CHECK_NEAR(a, b, eps) \
  do { \
    double va = (a), vb = (b); \
    if (!(std::fabs(va - vb) <= (eps))) { \
      std::fprintf(stderr, "  %s:%d: CHECK_NEAR(%s, %s) failed: %g vs %g\n", \
        __FILE__, __LINE__, #a, #b, va, vb); \
      dxvk::test::failureCount()++; \
    } \
  } while (0)
