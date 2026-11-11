#include <algorithm>
#include <iterator>
#include <vector>

#include "test.h"

#include "../vr_engine_patch_table.h"
#include "../vr_game_addresses.h"

using namespace dxvk;
using namespace dxvk::engine_patches;

TEST_CASE(enginePatchRangesStayInsideTheByteTable) {
  for (const Range& range : Ranges) {
    CHECK(range.size > 0);
    CHECK(range.offset + 2 * range.size <= std::size(Bytes));
  }
}

TEST_CASE(enginePatchUnitsCoverEveryRangeOnce) {
  uint32_t next = 0;

  for (const Unit& unit : Units) {
    CHECK(unit.firstRange == next);
    CHECK(unit.rangeCount > 0);
    CHECK(uint32_t(unit.group) < GroupCount);
    next += unit.rangeCount;
  }

  CHECK(next == std::size(Ranges));
}

TEST_CASE(enginePatchRangesDoNotOverlap) {
  std::vector<Range> sorted(std::begin(Ranges), std::end(Ranges));
  std::sort(sorted.begin(), sorted.end(), [] (const Range& a, const Range& b) { return a.address < b.address; });

  for (size_t i = 1; i < sorted.size(); i++)
    CHECK(sorted[i - 1].address + sorted[i - 1].size <= sorted[i].address);
}

TEST_CASE(enginePatchesChangeEveryRange) {
  for (const Range& range : Ranges)
    CHECK(!std::equal(&Bytes[range.offset], &Bytes[range.offset + range.size], &Bytes[range.offset + range.size]));
}

TEST_CASE(enginePatchesLeaveTheFunctionsTheModReplaces) {
  const uintptr_t replaced[] = {
    VrGame::CrtFabs, VrGame::PointConstruct, VrGame::PointAdd, VrGame::PointSubtract, VrGame::PointScale,
    VrGame::MatrixPoint, VrGame::MatrixMatrix, VrGame::TransformTransform,
    VrGame::CurrentProcessGetter, VrGame::ProcessLevelGetter,
    VrGame::LightFrustumBound, VrGame::CrtCiSqrt, VrGame::CrtSqrt,
  };

  for (const Range& range : Ranges) {
    for (uintptr_t function : replaced)
      CHECK(function + 8 <= range.address || range.address + range.size <= function);
  }
}
