#include "test.h"

#include "../vr_performance_governor.h"
#include "../vr_performance_memory.h"

using namespace dxvk;

TEST_CASE(performance_memory_keys_a_headset_by_its_rate) {
  CHECK(VrPerformanceMemory::key("Steam Frame", 1000.0 / 72.0) == "Steam Frame@72");
  CHECK(VrPerformanceMemory::key("Oculus Quest2", 1000.0 / 120.0) == "Oculus Quest2@120");
  CHECK(VrPerformanceMemory::key("A=B", 1000.0 / 90.0) == "A B@90");
}


TEST_CASE(performance_memory_round_trips_its_file) {
  VrPerformanceMemory memory;
  CHECK(memory.store("Steam Frame@72", VrPerformanceLevels { 2, 5 }));
  CHECK(!memory.store("Steam Frame@72", VrPerformanceLevels { 2, 5 }));
  CHECK(memory.store("Oculus Quest2@120", VrPerformanceLevels { 0, 0 }));

  VrPerformanceMemory read;
  read.parse(memory.format());

  VrPerformanceLevels levels;
  CHECK(read.find("Steam Frame@72", levels));
  CHECK(levels.distance == 2 && levels.gpu == 5);
  CHECK(read.find("Oculus Quest2@120", levels));
  CHECK(levels.distance == 0 && levels.gpu == 0);
  CHECK(!read.find("Steam Frame@90", levels));
}


TEST_CASE(performance_memory_skips_lines_it_cannot_read) {
  VrPerformanceMemory memory;
  memory.parse("# comment\r\nno equals\r\n=1 2\r\nHeadset@72=one two\r\nHeadset@90=-1 2\r\nHeadset@120=1 3\r\n");

  VrPerformanceLevels levels;
  CHECK(!memory.find("Headset@72", levels));
  CHECK(!memory.find("Headset@90", levels));
  CHECK(memory.find("Headset@120", levels));
  CHECK(levels.distance == 1 && levels.gpu == 3);
}


TEST_CASE(performance_memory_keeps_the_level_held_longest) {
  const double held[] = { 10.0, 30.0, 30.0, 5.0 };
  CHECK(VrPerformanceMemory::mostHeld(held, 4) == 1);

  const double idle[] = { 0.0, 0.0 };
  CHECK(VrPerformanceMemory::mostHeld(idle, 2) == 0);
}


TEST_CASE(performance_governor_starts_from_remembered_levels) {
  VrPerformanceGovernor governor(4, 8);
  governor.start(VrPerformanceLevels { 2, 12 });

  CHECK(governor.levels().distance == 2);
  CHECK(governor.levels().gpu == 8);

  governor.reset();
  CHECK(governor.levels().distance == 0 && governor.levels().gpu == 0);
}
