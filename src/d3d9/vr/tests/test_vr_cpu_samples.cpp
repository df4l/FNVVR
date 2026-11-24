#include <string>

#include "test.h"

#include "../vr_cpu_samples.h"

using namespace dxvk;

namespace {

  /// Seven bytes before a return address, the instruction at their end
  bool follows(std::initializer_list<uint8_t> instruction) {
    uint8_t bytes[7] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
    size_t start = 7 - instruction.size();

    for (uint8_t value : instruction)
      bytes[start++] = value;

    return vrFollowsCall(bytes);
  }

  bool contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
  }

}

TEST_CASE(cpuSamplesRecogniseTheCallForms) {
  CHECK(follows({ 0xE8, 0x10, 0x20, 0x30, 0x40 }));        // call rel32
  CHECK(follows({ 0xFF, 0xD0 }));                          // call eax
  CHECK(follows({ 0xFF, 0x12 }));                          // call [edx]
  CHECK(follows({ 0xFF, 0x50, 0x08 }));                    // call [eax+8]
  CHECK(follows({ 0xFF, 0x14, 0x81 }));                    // call [ecx+eax*4]
  CHECK(follows({ 0xFF, 0x54, 0x24, 0x10 }));              // call [esp+0x10]
  CHECK(follows({ 0xFF, 0x15, 0x00, 0x10, 0x40, 0x00 }));  // call [abs32]
  CHECK(follows({ 0xFF, 0x90, 0x00, 0x01, 0x00, 0x00 }));  // call [eax+0x100]
  CHECK(follows({ 0xFF, 0x94, 0x81, 0x00, 0x01, 0x00, 0x00 }));  // call [ecx+eax*4+0x100]
}

TEST_CASE(cpuSamplesRejectOtherCode) {
  CHECK(!follows({ 0x90, 0x90 }));
  CHECK(!follows({ 0xFF, 0xE0 }));                         // jmp eax
  CHECK(!follows({ 0xFF, 0x60, 0x08 }));                   // jmp [eax+8]
  CHECK(!follows({ 0xFF, 0x25, 0x00, 0x10, 0x40, 0x00 }));  // jmp [abs32]
  CHECK(!follows({ 0xE9, 0x10, 0x20, 0x30, 0x40 }));       // jmp rel32
}

TEST_CASE(cpuSamplesNamePhases) {
  CHECK(vrCpuPhaseName(uint8_t(VrCpuPhase::Game)) == "game");
  CHECK(vrCpuPhaseName(uint8_t(VrCpuPhase::Wait)) == "wait");
  CHECK(vrCpuPhaseName(vrCpuEyePhase(0, VrRenderPass::Scene)) == "left scene");
  CHECK(vrCpuPhaseName(vrCpuEyePhase(1, VrRenderPass::Eye)) == "right rest");
  CHECK(vrCpuPhaseName(200) == "?");
}

TEST_CASE(cpuSamplesFindModules) {
  VrCpuModules modules;
  modules.set({ { 0x10000000, 0x10100000, "d3d9.dll" }, { 0x00400000, 0x01000000, "FalloutNV.exe" } });

  CHECK(modules.find(0x00400000) == 0);
  CHECK(modules.find(0x00FFFFFF) == 0);
  CHECK(modules.find(0x01000000) == VrCpuModules::Unknown);
  CHECK(modules.find(0x10000010) == 1);
  CHECK(modules.find(0x00100000) == VrCpuModules::Unknown);
}

TEST_CASE(cpuSamplesCountAndShare) {
  VrCpuModules modules;
  modules.set({ { 0x00400000, 0x01000000, "FalloutNV.exe" }, { 0x10000000, 0x10100000, "d3d9.dll" } });

  VrCpuSample game;
  game.eip = 0x00500000;
  game.callers = { 0x00600010, 0 };

  VrCpuSample wait;
  wait.eip = 0x7B000000;
  wait.phase = uint8_t(VrCpuPhase::Wait);

  VrCpuSampleTable table;
  table.add(game);
  table.add(game);
  table.add(game);
  table.add(wait);

  CHECK(table.total() == 4);
  CHECK(table.counts().size() == 2);
  CHECK(table.counts().at(game) == 3);

  std::string shares = table.describeShares(modules, 6);
  CHECK(contains(shares, "FalloutNV.exe 75.0 %"));
  CHECK(contains(shares, "other 25.0 %"));
  CHECK(contains(shares, "game 75.0 %"));
  CHECK(contains(shares, "wait 25.0 %"));

  table.reset();
  CHECK(table.total() == 0);
}
