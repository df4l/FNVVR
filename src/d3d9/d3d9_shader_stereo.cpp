#include "d3d9_shader_stereo.h"

#include "d3d9_shader.h"

#include <algorithm>

namespace dxvk {

  void D3D9ApplyStereoTransform(
          dxbc_spv::ir::Builder&    builder,
          dxbc_spv::ir::SsaDef      entryPoint) {
    using namespace dxbc_spv;

    ir::SsaDef position = { };

    for (auto iter = builder.getDeclarations().first;
              iter != builder.getDeclarations().second; iter++) {
      if (iter->getOpCode() == ir::OpCode::eDclOutputBuiltIn
       && ir::BuiltIn(iter->getOperand(iter->getFirstLiteralOperandIndex())) == ir::BuiltIn::ePosition)
        position = iter->getDef();
    }

    if (!position)
      return;

    std::array<ir::SsaDef, 4u> stores = { };
    std::array<ir::SsaDef, 4u> values = { };

    dxvk::small_vector<ir::SsaDef, 16u> uses;
    builder.getUses(position, uses);

    for (auto use : uses) {
      const auto& useOp = builder.getOp(use);

      if (useOp.getOpCode() != ir::OpCode::eOutputStore)
        continue;

      auto address = ir::SsaDef(useOp.getOperand(1u));

      if (!address || !builder.getOp(address).isConstant())
        return;

      auto component = uint32_t(builder.getOp(address).getOperand(0u));

      if (component >= stores.size() || stores[component])
        return;

      stores[component] = use;
      values[component] = ir::SsaDef(useOp.getOperand(2u));
    }

    for (auto store : stores) {
      if (!store)
        return;
    }

    // The new stores go after the last of the four, which must
    // follow the first one closely in the same block
    constexpr uint32_t MaxDistance = 64u;

    ir::SsaDef last = { };
    uint32_t found = 0u;
    uint32_t distance = 0u;

    for (auto def = stores[0u]; def && found < stores.size() && distance < MaxDistance; def = builder.getNext(def), distance++) {
      if (std::find(stores.begin(), stores.end(), def) != stores.end()) {
        last = def;
        found++;
      }
    }

    if (found < stores.size())
      return;

    auto rowType = ir::Type(ir::ScalarType::eF32, 4u);
    auto cbvType = ir::Type(ir::ScalarType::eF32, 4u).addArrayDimension(4u);

    auto cbv = builder.add(ir::Op::DclCbv(cbvType, entryPoint, 0u,
      D3D9ShaderResourceMapping::VSStereoTransform, 1u).setFlags(ir::OpFlag::eInBounds));
    builder.add(ir::Op::DebugName(cbv, "stereo"));

    auto cursor = builder.setCursor(last);

    auto descriptor = builder.add(ir::Op::DescriptorLoad(
      ir::ScalarType::eCbv, cbv, builder.makeConstant(0u)));

    std::array<ir::SsaDef, 4u> results = { };

    for (uint32_t r = 0u; r < results.size(); r++) {
      auto row = builder.add(ir::Op::BufferLoad(rowType, descriptor,
        builder.makeConstant(r), 16u).setFlags(ir::OpFlag::eInBounds));

      ir::SsaDef sum = { };

      for (uint32_t c = 0u; c < values.size(); c++) {
        auto factor = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, row, builder.makeConstant(c)));
        auto product = builder.add(ir::Op::FMul(ir::ScalarType::eF32, factor, values[c]).setFlags(ir::OpFlag::ePrecise));

        sum = sum
          ? builder.add(ir::Op::FAdd(ir::ScalarType::eF32, sum, product).setFlags(ir::OpFlag::ePrecise))
          : product;
      }

      results[r] = sum;
    }

    ir::SsaDef lastStore = { };

    for (uint32_t r = 0u; r < results.size(); r++)
      lastStore = builder.add(ir::Op::OutputStore(position, builder.makeConstant(r), results[r]));

    // The position is written once, transformed
    for (auto store : stores)
      builder.remove(store);

    bool cursorRemoved = std::find(stores.begin(), stores.end(), cursor) != stores.end();
    builder.setCursor(cursorRemoved ? lastStore : cursor);
  }

}
